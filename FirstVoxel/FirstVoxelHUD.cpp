// FirstVoxelHUD.cpp
//
// LOADING SCREEN FIX:
//
// Old gate:  if (bShowLoadingScreen && CachedVoxelWorld) → DrawLoadingScreen
// Old dismiss inside DrawLoadingScreen:
//   if (!W->IsWaitingForInitialSpawn()) { bShowLoadingScreen = false; }
// This dismissed on frame 1 because bWaitingForInitialSpawn is false during
// the async discovery phase (before ProcessInitialPlayerSpawn runs). The
// screen disappeared before any terrain generated.
//
// New gate:  if (bShowLoadBar && CachedVoxelWorld) → DrawLoadingScreen
// VoxelWorld sets bShowLoadBar = true at BeginPlay / GenerateWorldDeferred,
// and bShowLoadBar = false only when all spawn chunks are collision-ready and
// the player has safely landed. This is the authoritative "still generating"
// signal. DrawLoadingScreen no longer auto-dismisses — VoxelWorld drives it.
//
// bShowLoadingScreen kept for title-screen -> loading transition (set when
// clicking Generate in the menu) but it now only initiates; dismissal is
// handled by bShowLoadBar.

#include "FirstVoxelHUD.h"
#include "UI/VoxelPauseMenu.h"
#include "UI/VoxelSaveSlotConfig.h"
#include "Engine/Canvas.h"
#include "FirstVoxelCharacter.h"
#include "Voxel/Core/World/VoxelWorld.h"
#include "Voxel/Core/World/Spawn/VoxelSpawnHandlerComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

namespace
{
    void DrawRow(AHUD* HUD, float X, float& Y, float LineH,
                 const FString& Badge, const FColor& BC,
                 const FString& Action, const FColor& AC, UFont* Font)
    {
        HUD->DrawText(Badge,  FLinearColor(BC), X,       Y, Font);
        HUD->DrawText(Action, FLinearColor(AC), X+110.f, Y, Font);
        Y += LineH;
    }
}

void AFirstVoxelHUD::BeginPlay()
{
    Super::BeginPlay();
    PauseMenu = NewObject<UVoxelPauseMenu>(this);
    PauseMenu->Init(this);
}

bool AFirstVoxelHUD::IsPaused() const { return PauseMenu && PauseMenu->IsOpen(); }
void AFirstVoxelHUD::TogglePause()
{
    if (!PauseMenu) return;
    PauseMenu->IsOpen() ? PauseMenu->Close() : PauseMenu->Open();
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawHUD
// ─────────────────────────────────────────────────────────────────────────────
void AFirstVoxelHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;
    if (PauseMenu && PauseMenu->IsOpen()) { PauseMenu->Draw(); return; }

    UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;

    if (!CachedVoxelWorld)
    {
        TArray<AActor*> WA;
        UGameplayStatics::GetAllActorsOfClass(this, AVoxelWorld::StaticClass(), WA);
        if (WA.Num() > 0) CachedVoxelWorld = Cast<AVoxelWorld>(WA[0]);
    }

    // FIX: gate on bShowLoadBar (VoxelWorld-controlled) instead of
    // bShowLoadingScreen (manually set, dismissed too early on frame 1).
    if (bShowLoadBar && CachedVoxelWorld) { DrawLoadingScreen(Font); return; }
    if (bShowTitleScreen)                  { DrawTitleScreen(Font);   return; }

    DrawGameplayHUD(Font);
    DrawToolWheel(Font);
    DrawStatsOverlay(Font);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawLoadingScreen
// ─────────────────────────────────────────────────────────────────────────────
void AFirstVoxelHUD::DrawLoadingScreen(UFont* Font)
{
    AVoxelWorld* W = CachedVoxelWorld;
    // Removed fullscreen dimming so player can watch generation
    // DrawRect(FLinearColor(0,0,0,0.55f), 0,0, Canvas->SizeX, Canvas->SizeY);

    const float CX = Canvas->SizeX*0.5f, CY = Canvas->SizeY*0.5f;
    const FString Title = TEXT("G E N E R A T I N G   W O R L D . . .");
    float TW,TH; GetTextSize(Title,TW,TH,Font,1.5f);
    DrawText(Title, FLinearColor::White, CX-TW*0.5f, CY-40.f, Font, 1.5f);

    // Progress bar
    const float BW=400.f, BH=20.f, BX=CX-200.f, BY=CY+10.f;
    DrawRect(FLinearColor(0.1f,0.1f,0.1f,0.8f),   BX-2.f,BY-2.f,BW+4.f,BH+4.f);
    int32 Head = 0, Total = 0;
    
    // FIX: query SpawnHandler for accurate local load screen tallies
    if (SH)
    {
        if (W->IsWaitingForInitialSpawn())
        {
            Head  = SH->GetVisualReadyCount();
            Total = SH->GetTotalVisualCount();
        }
        else
        {
            Head  = W->GetQueueHead();
            Total = W->GetQueueCount();
        }
    }
    else
    {
        Head  = W->GetQueueHead();
        Total = W->GetQueueCount();
    }

    const float Pct = Total > 0 ? (float)Head / (float)Total : 0.f;

    DrawRect(FLinearColor(0.15f,0.18f,0.22f,1.f),  BX,BY,BW,BH);
    DrawRect(FLinearColor(0.25f,0.85f,0.35f,1.f),  BX,BY,BW*FMath::Clamp(Pct,0.f,1.f),BH);

    // Status line
    FString Status;
    if (W->IsWaitingForInitialSpawn())
        Status = FString::Printf(TEXT("Preparing Spawn Area: %d / %d"), Head, Total);
    else if (Total > 0 && Head < Total)
        Status = FString::Printf(TEXT("Building World: %d / %d chunks"), Head, Total);
    else if (Total <= 0)
        Status = TEXT("Requesting world coordinates...");
    else
        Status = TEXT("Finalizing terrain...");
    float SW, TextH; GetTextSize(Status, SW, TextH, Font, 1.f);
    DrawText(Status, FLinearColor::White, CX - SW * 0.5f, BY + BH + 8.f, Font, 1.f);

    // Chunk-map grid
    const float BoxSz=8.f, BoxPad=2.f, Radius=35.f;
    const float GridW=(Radius*2+1)*(BoxSz+BoxPad), GridX=CX-GridW*0.5f, GridY=BY+BH+40.f;
    const auto& Loaded = *W->GetLoadedChunks();
    const int32 iRadius = FMath::RoundToInt(Radius);
    for (int32 cy=-iRadius; cy<=iRadius; ++cy)
    for (int32 cx=-iRadius; cx<=iRadius; ++cx)
    {
        FLinearColor Col(0.1f,0.1f,0.12f,0.4f);
        if (const AVoxelChunk* const* P = Loaded.Find(FIntVector(cx,cy,0)))
            if (const AVoxelChunk* C = *P)
                Col = C->IsReady()      ? FLinearColor(0.2f,0.8f,0.3f,0.9f)
                    : C->IsGenerating() ? FLinearColor(0.9f,0.8f,0.2f,0.9f)
                                        : FLinearColor(0.4f,0.4f,0.45f,0.7f);
        DrawRect(Col, GridX+(cx+iRadius)*(BoxSz+BoxPad), GridY+(cy+iRadius)*(BoxSz+BoxPad), BoxSz, BoxSz);
    }

    // FIX: NO auto-dismiss here.
    // VoxelWorld Tick sets bShowLoadBar = false when spawn chunks are ready.
    // DrawHUD will then route to gameplay HUD automatically next frame.
    // Old code: if (!W->IsWaitingForInitialSpawn()) { bShowLoadingScreen = false; }
    //           → triggered on frame 1, screen gone before terrain generated.
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawTitleScreen
// ─────────────────────────────────────────────────────────────────────────────
void AFirstVoxelHUD::DrawTitleScreen(UFont* Font)
{
    DrawRect(FLinearColor(0,0,0,0.55f), 0,0, Canvas->SizeX, Canvas->SizeY);
    const float CX=Canvas->SizeX*0.5f, CY=Canvas->SizeY*0.5f;

    float TW,TH; GetTextSize(TEXT("M E N U"),TW,TH,Font,2.f);
    DrawText(TEXT("M E N U"), FLinearColor(1.f,0.85f,0.2f,1.f), CX-TW*0.5f, CY-130.f, Font, 2.f);
    DrawRect(FLinearColor(0.35f,0.35f,0.45f,0.7f), CX-220.f, CY-85.f, 440.f, 1.f);

    bool bSlotExists[MaxSaveSlots] = {};
    if (CachedVoxelWorld)
        for (int32 s=0;s<MaxSaveSlots;++s)
        {
            FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VoxelSaves"),
                FString::Printf(TEXT("%s_%s.sav"), *CachedVoxelWorld->GetName(), *DefaultSlotNames[s]));
            bSlotExists[s] = FPaths::FileExists(Path);
        }
    const bool bAnySaved = bSlotExists[0]||bSlotExists[1]||bSlotExists[2]||bSlotExists[3]||bSlotExists[4];

    struct FItem { FString Label; FColor Col; };
    TArray<FItem> Items;
    if (!bShowWorldOptions)
    {
        Items.Add({TEXT("Generate New World"), FColor(220,220,220)});
        Items.Add({TEXT("World Options..."),   FColor(220,220,220)});
        Items.Add({bAnySaved ? TEXT("Load World (Slot 1)") : TEXT("Load World  (no saves)"),
                   bAnySaved ? FColor(220,220,220)         : FColor(100,100,100)});
    }
    else
    {
        Items.Add({TEXT("< Back"), FColor(200,200,200)});
        const auto& Perf = CachedVoxelWorld ? CachedVoxelWorld->GenerationConfig.Performance
                                             : FVoxelPerformanceConfig{};
        Items.Add({FString::Printf(TEXT("Surface:   %s"), Perf.bEnableSurface  ?TEXT("YES"):TEXT("NO")), FColor(150,220,150)});
        Items.Add({FString::Printf(TEXT("Skylands:  %s"), Perf.bEnableSkylands ?TEXT("YES"):TEXT("NO")), FColor(150,220,150)});
        Items.Add({FString::Printf(TEXT("Caves:     %s"), Perf.bEnableCaves    ?TEXT("YES"):TEXT("NO")), FColor(150,220,150)});
        Items.Add({FString::Printf(TEXT("Forest:    %s"), Perf.bEnableForest   ?TEXT("YES"):TEXT("NO")), FColor(150,220,150)});
        Items.Add({FString::Printf(TEXT("Desert:    %s"), Perf.bEnableDesert   ?TEXT("YES"):TEXT("NO")), FColor(150,220,150)});
        Items.Add({FString::Printf(TEXT("Peaks:     %s"), Perf.bEnablePeaks    ?TEXT("YES"):TEXT("NO")), FColor(150,220,150)});
        Items.Add({FString::Printf(TEXT("Cliffs:    %s"), Perf.bEnableCliffs   ?TEXT("YES"):TEXT("NO")), FColor(150,220,150)});
        Items.Add({FString::Printf(TEXT("Mesa:      %s"), Perf.bEnableMesa     ?TEXT("YES"):TEXT("NO")), FColor(150,220,150)});
        Items.Add({FString::Printf(TEXT("Craters:   %s"), Perf.bEnableCraters  ?TEXT("YES"):TEXT("NO")), FColor(150,220,150)});
        const bool bCrat = CachedVoxelWorld ? CachedVoxelWorld->bSpawnInNaturalCrater : false;
        Items.Add({FString::Printf(TEXT("Spawn in Crater: %s"), bCrat?TEXT("YES"):TEXT("NO")), FColor(150,150,220)});
    }
    const int32 N = Items.Num();

    APlayerController* PC = GetOwningPlayerController();
    if (PC && !PC->bShowMouseCursor)
    {
        FInputModeGameAndUI M;
        M.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        M.SetHideCursorDuringCapture(false);
        PC->SetInputMode(M);
        PC->SetShowMouseCursor(true);
    }

    if (PC)
    {
        if (PC->WasInputKeyJustPressed(EKeys::Up)  ||PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Up))   TitleSelection=(TitleSelection-1+N)%N;
        if (PC->WasInputKeyJustPressed(EKeys::Down)||PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Down)) TitleSelection=(TitleSelection+1)%N;
    }

    bool bMouseOver = false;
    if (PC) { float MX,MY; if (PC->GetMousePosition(MX,MY)) {
        float BaseY=CY-60.f;
        for (int32 i=0;i<N;++i) {
            if (MX>=CX-180.f&&MX<=CX+180.f&&MY>=BaseY-4.f&&MY<=BaseY+31.f)
                if (!(!bShowWorldOptions&&i==2&&!bAnySaved)) { TitleSelection=i; bMouseOver=true; }
            BaseY+=50.f;
        }
    }}

    float ItemY=CY-60.f;
    for (int32 i=0;i<N;++i)
    {
        const bool bSel=(i==TitleSelection);
        if (bSel) DrawRect(FLinearColor(0.15f,0.65f,0.25f,0.35f),CX-180.f,ItemY-4.f,360.f,35.f);
        DrawText(Items[i].Label, bSel?FLinearColor::Green:FLinearColor(Items[i].Col), CX-160.f,ItemY,Font,1.4f);
        ItemY+=50.f;
    }
    DrawRect(FLinearColor(0.35f,0.35f,0.45f,0.7f),CX-220.f,ItemY+4.f,440.f,1.f);
    FString Hint=TEXT("[Enter] Select   [Arrows] Navigate   [Click] Mouse");
    float HW,HH; GetTextSize(Hint,HW,HH,Font,1.f);
    DrawText(Hint, FLinearColor(0.55f,0.8f,1.f,1.f), CX-HW*0.5f, ItemY+18.f, Font, 1.f);

    if (PC)
    {
        const bool bConfirm = PC->WasInputKeyJustPressed(EKeys::Enter)
            || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom)
            || (bMouseOver && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton));

        auto Restore=[PC](){ PC->SetShowMouseCursor(false); PC->SetInputMode(FInputModeGameOnly()); };

        if (bConfirm && !bShowWorldOptions)
        {
            if (TitleSelection==0 && CachedVoxelWorld)
            {
                // Loading screen now driven by bShowLoadBar (set by VoxelWorld).
                // bShowLoadingScreen kept for menu state bookkeeping only.
                bShowTitleScreen   = false;
                bShowLoadingScreen = true;
                bShowLoadBar       = true; // ADDED THIS TO ENSURE IT'S NOT SKIPPED
                Restore();
                CachedVoxelWorld->GenerateWorld();
            }
            else if (TitleSelection==1) { bShowWorldOptions=true; TitleSelection=0; }
            else if (TitleSelection==2 && bAnySaved && CachedVoxelWorld)
            {
                int32 S=0; for(int32 s=0;s<MaxSaveSlots;++s){ if(bSlotExists[s]){S=s;break;} }
                bShowTitleScreen=false; Restore();
                CachedVoxelWorld->ClearWorld();
                CachedVoxelWorld->LoadFromFile(DefaultSlotNames[S]);
                CachedVoxelWorld->GenerateWorldDeferred();
            }
        }
        else if (bConfirm && bShowWorldOptions && CachedVoxelWorld)
        {
            auto& Perf = CachedVoxelWorld->GenerationConfig.Performance;
            if      (TitleSelection==0)  { bShowWorldOptions=false; TitleSelection=1; }
            else if (TitleSelection==1)  Perf.bEnableSurface  =!Perf.bEnableSurface;
            else if (TitleSelection==2)  Perf.bEnableSkylands =!Perf.bEnableSkylands;
            else if (TitleSelection==3)  Perf.bEnableCaves    =!Perf.bEnableCaves;
            else if (TitleSelection==4)  Perf.bEnableForest   =!Perf.bEnableForest;
            else if (TitleSelection==5)  Perf.bEnableDesert   =!Perf.bEnableDesert;
            else if (TitleSelection==6)  Perf.bEnablePeaks    =!Perf.bEnablePeaks;
            else if (TitleSelection==7)  Perf.bEnableCliffs   =!Perf.bEnableCliffs;
            else if (TitleSelection==8)  Perf.bEnableMesa     =!Perf.bEnableMesa;
            else if (TitleSelection==9)  Perf.bEnableCraters  =!Perf.bEnableCraters;
            else if (TitleSelection==10) CachedVoxelWorld->bSpawnInNaturalCrater=!CachedVoxelWorld->bSpawnInNaturalCrater;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawGameplayHUD
// ─────────────────────────────────────────────────────────────────────────────
void AFirstVoxelHUD::DrawGameplayHUD(UFont* Font)
{
    const float CX=Canvas->SizeX*0.5f, CY=Canvas->SizeY*0.5f;
    DrawRect(FLinearColor::Green, CX-10.f,CY-1.f,  20.f, 2.f);
    DrawRect(FLinearColor::Green, CX- 1.f,CY-10.f,  2.f,20.f);

    float BrushRad=300.f; bool bGP=false;
    AFirstVoxelCharacter* Char=nullptr;
    if (APawn* P=GetOwningPawn()) { Char=Cast<AFirstVoxelCharacter>(P); if(Char){BrushRad=Char->InteractionRadius;bGP=Char->bLastInputWasGamepad;} }

    const float X=20.f; float Y=20.f; const float LH=19.f;
    const FColor ColT(255,220,50),ColKB(120,200,255),ColGP(255,150,80),ColA(220,220,220),ColBr(80,255,80);
    const FColor& BC=bGP?ColGP:ColKB;
    DrawText(FString(TEXT("=== CONTROLS ==="))+(bGP?TEXT(" [Gamepad]"):TEXT(" [KB+Mouse]")), FLinearColor(ColT), X,Y,Font); Y+=LH+4.f;

    static const FString TN[]={"DIG","BUILD","SMOOTH","FLATTEN"};
    const int32 TIdx=Char?static_cast<int32>(Char->CurrentTool):0;

    if (bGP)
    {
        DrawRow(this,X,Y,LH,TEXT("[RT]"),       BC,TEXT(": Dig"),             ColA,Font);
        DrawRow(this,X,Y,LH,TEXT("[LT]"),       BC,TEXT(": Build"),           ColA,Font);
        DrawRow(this,X,Y,LH,TEXT("[RB]/[LB]"),  BC,TEXT(": Brush Radius +/-"),ColA,Font);
        DrawText(FString::Printf(TEXT("Brush: [%.f]"),BrushRad),FLinearColor(ColBr),X,Y,Font); Y+=LH+4.f;
        DrawText(FString::Printf(TEXT("Tool: [%s]"),*TN[FMath::Clamp(TIdx,0,3)]),FLinearColor(255,210,100),X,Y,Font); Y+=LH+6.f;
        DrawRow(this,X,Y,LH,TEXT("[B]"),    BC,TEXT(": Toggle Flight"),ColA,Font);
        DrawRow(this,X,Y,LH,TEXT("[A]"),    BC,TEXT(": Jump/Fly Up"),  ColA,Font);
        DrawRow(this,X,Y,LH,TEXT("[X]"),    BC,TEXT(": Fly Down"),     ColA,Font);
        DrawRow(this,X,Y,LH,TEXT("[Y]"),    BC,TEXT(": Map"),          ColA,Font);
        DrawRow(this,X,Y,LH,TEXT("[X/Sq]"), BC,TEXT(": Camera Mode"),  ColA,Font);
        DrawRow(this,X,Y,LH,TEXT("[Start]"),BC,TEXT(": Pause"),        ColA,Font);
    }
    else
    {
        DrawRow(this,X,Y,LH,TEXT("[LMB]"),   BC,TEXT(": Dig (hold)"),  ColA,Font);
        DrawRow(this,X,Y,LH,TEXT("[RMB]"),   BC,TEXT(": Build (hold)"),ColA,Font);
        DrawRow(this,X,Y,LH,TEXT("[Scroll]"),BC,TEXT(": Brush +/-"),   ColA,Font);
        DrawText(FString::Printf(TEXT("Brush: [%.f]"),BrushRad),FLinearColor(ColBr),X,Y,Font); Y+=LH+4.f;
        DrawText(FString::Printf(TEXT("Tool: [%s]"),*TN[FMath::Clamp(TIdx,0,3)]),FLinearColor(255,210,100),X,Y,Font); Y+=LH+6.f;
        DrawRow(this,X,Y,LH,TEXT("[F]"),    BC,TEXT(": Toggle Flight"),ColA,Font);
        DrawRow(this,X,Y,LH,TEXT("[Space]"),BC,TEXT(": Jump/Fly Up"),  ColA,Font);
        DrawRow(this,X,Y,LH,TEXT("[Ctrl]"), BC,TEXT(": Fly Down"),     ColA,Font);
        DrawRow(this,X,Y,LH,TEXT("[M]"),    BC,TEXT(": Map"),          ColA,Font);
        DrawRow(this,X,Y,LH,TEXT("[V]"),    BC,TEXT(": Camera Mode"),  ColA,Font);
        DrawRow(this,X,Y,LH,TEXT("[P]"),    BC,TEXT(": Pause"),        ColA,Font);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawToolWheel
// ─────────────────────────────────────────────────────────────────────────────
void AFirstVoxelHUD::DrawToolWheel(UFont* Font)
{
    AFirstVoxelCharacter* C=Cast<AFirstVoxelCharacter>(GetOwningPawn());
    if (!C || !C->bToolWheelOpen) return;

    DrawRect(FLinearColor(0,0,0,0.45f), 0,0, Canvas->SizeX, Canvas->SizeY);
    const float CX=Canvas->SizeX*0.5f, CY=Canvas->SizeY*0.5f, R=160.f;
    static const FString TN[]={"DIG","BUILD","SMOOTH","FLATTEN"};
    static const FVector2D Offs[]={{-R,0},{0,-R},{R,0},{0,R}};
    const int32 SelIdx=static_cast<int32>(C->CurrentTool);
    for (int32 i=0;i<4;++i)
    {
        float TW,TH; GetTextSize(TN[i],TW,TH,Font);
        const float DX=CX+Offs[i].X-TW*0.5f, DY=CY+Offs[i].Y-TH*0.5f;
        if (i==SelIdx) DrawRect(FLinearColor(0.2f,0.8f,0.2f,0.4f),DX-12.f,DY-6.f,TW+24.f,TH+12.f);
        DrawText(TN[i], i==SelIdx?FLinearColor::Green:FLinearColor::White, DX,DY,Font);
    }
    DrawRect(FLinearColor::White,CX-3.f,CY-3.f,6.f,6.f);
    APlayerController* PC=GetOwningPlayerController();
    if (PC && !C->bLastInputWasGamepad) {
        float MX,MY; if (PC->GetMousePosition(MX,MY)) DrawLine(CX,CY,MX,MY,FLinearColor::Yellow,2.f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawStatsOverlay
// ─────────────────────────────────────────────────────────────────────────────
void AFirstVoxelHUD::DrawStatsOverlay(UFont* Font)
{
    if (!Canvas || !GEngine) return;

    const float FPS = GetWorld()->GetDeltaSeconds() > 0.f ? 1.f/GetWorld()->GetDeltaSeconds() : 0.f;
    FString FPSStr = FString::Printf(TEXT("FPS: %.1f"), FPS);
    float FW,FH; GetTextSize(FPSStr,FW,FH,Font);
    const float FX=Canvas->SizeX-FW-20.f, FY=20.f;
    DrawRect(FLinearColor(0,0,0,0.45f),FX-4.f,FY-2.f,FW+8.f,FH+4.f);
    DrawText(FPSStr, FLinearColor(0.3f,1.f,0.3f,1.f), FX,FY,Font);

    AFirstVoxelCharacter* Ch=Cast<AFirstVoxelCharacter>(GetOwningPawn());
    if (!Ch) return;
    const FVector Loc=Ch->GetActorLocation();

    FString CoordStr=FString::Printf(TEXT("X: %.1f  Y: %.1f  Z: %.1f"),Loc.X,Loc.Y,Loc.Z);
    float CW2,CH2; GetTextSize(CoordStr,CW2,CH2,Font);
    const float CXr=Canvas->SizeX-CW2-20.f, CYr=50.f;
    DrawRect(FLinearColor(0,0,0,0.45f),CXr-4.f,CYr-2.f,CW2+8.f,CH2+4.f);
    DrawText(CoordStr, FLinearColor(0.8f,0.8f,1.f,1.f), CXr,CYr,Font);

    FString AltStr=FString::Printf(TEXT("Altitude: %.1f cm"),Loc.Z);
    float AW,AH; GetTextSize(AltStr,AW,AH,Font);
    const float AXr=Canvas->SizeX-AW-20.f, AYr=75.f;
    DrawRect(FLinearColor(0,0,0,0.45f),AXr-4.f,AYr-2.f,AW+8.f,AH+4.f);
    DrawText(AltStr, FLinearColor(1.f,0.8f,0.3f,1.f), AXr,AYr,Font);
}
