// FirstVoxelHUD_Drawing.cpp
// Drawing helpers for AFirstVoxelHUD.
// FIX: All GetWorld() calls use HUD->GetWorld() — this is a free-function
//      namespace, not a UObject method, so the unqualified call won't compile.

#include "FirstVoxelHUD_Drawing.h"
#include "FirstVoxelHUD.h"
#include "FirstVoxelCharacter.h"
#include "Voxel/Core/World/VoxelWorld.h"
#include "Voxel/Core/VoxelChunk.h"
#include "UI/VoxelPauseMenu.h"
#include "Engine/Canvas.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "GameFramework/PlayerController.h"

namespace
{
    void DrawRow(AHUD* HUD, float X, float& Y, float LineH,
                 const FString& Badge, const FColor& BadgeCol,
                 const FString& Action, const FColor& ActCol, UFont* Font)
    {
        HUD->DrawText(Badge,  FLinearColor(BadgeCol), X,         Y, Font);
        HUD->DrawText(Action, FLinearColor(ActCol),   X + 110.f, Y, Font);
        Y += LineH;
    }

    AVoxelWorld* FindVoxelWorldForHUD(AFirstVoxelHUD* HUD)
    {
        TArray<AActor*> WA;
        UGameplayStatics::GetAllActorsOfClass(HUD, AVoxelWorld::StaticClass(), WA);
        return (WA.Num() > 0) ? Cast<AVoxelWorld>(WA[0]) : nullptr;
    }
}

// =============================================================================
//  DrawTitleScreen
// =============================================================================
void FirstVoxelHUDDraw::DrawTitleScreen(AFirstVoxelHUD* HUD, UFont* Font)
{
    UCanvas* Canvas = HUD->GetCanvas();
    if (!Canvas) return;

    AVoxelWorld* TitleWorld = FindVoxelWorldForHUD(HUD);
    HUD->DrawRect(FLinearColor(0.f,0.f,0.f,0.55f), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);

    const float CX2 = Canvas->SizeX * 0.5f;
    const float CY2 = Canvas->SizeY * 0.5f;

    float TW, TH;
    HUD->GetTextSize(TEXT("M E N U"), TW, TH, Font, 2.f);
    HUD->DrawText(TEXT("M E N U"), FLinearColor(1.f,0.85f,0.2f,1.f), CX2-TW*0.5f, CY2-130.f, Font, 2.f);
    HUD->DrawRect(FLinearColor(0.35f,0.35f,0.45f,0.7f), CX2-220.f, CY2-85.f, 440.f, 1.f);

    // Slot existence
    bool bSlotExists[MaxSaveSlots] = {};
    if (TitleWorld)
        for (int32 s=0; s<MaxSaveSlots; ++s)
            bSlotExists[s] = FPaths::FileExists(FPaths::Combine(
                FPaths::ProjectSavedDir(), TEXT("VoxelSaves"),
                FString::Printf(TEXT("%s_%s.sav"), *TitleWorld->GetName(), *DefaultSlotNames[s])));
    const bool bAnySaved = bSlotExists[0]||bSlotExists[1]||bSlotExists[2]||bSlotExists[3]||bSlotExists[4];

    struct TitleItem { FString Label; FColor Col; };
    TArray<TitleItem> Items;

    if (!HUD->bShowWorldOptions)
    {
        Items.Add({ TEXT("Generate New World"), FColor(220,220,220) });
        Items.Add({ TEXT("World Options..."),   FColor(220,220,220) });
        Items.Add({ bAnySaved ? TEXT("Load World (Slot 1)") : TEXT("Load World  (no saves)"),
                    bAnySaved ? FColor(220,220,220) : FColor(100,100,100) });
    }
    else
    {
        Items.Add({ TEXT("< Back"), FColor(200,200,200) });
        bool bS=true,bSk=true,bCv=true,bF=true,bD=true,bPk=true,bCl=true,bMs=true,bCr=true,bFC=false;
        if (TitleWorld)
        {
            const auto& P=TitleWorld->GenerationConfig.Performance;
            bS=P.bEnableSurface;bSk=P.bEnableSkylands;bCv=P.bEnableCaves;
            bF=P.bEnableForest;bD=P.bEnableDesert;bPk=P.bEnablePeaks;
            bCl=P.bEnableCliffs;bMs=P.bEnableMesa;bCr=P.bEnableCraters;
            bFC=TitleWorld->bSpawnInNaturalCrater;
        }
        auto yn=[](bool b)->const TCHAR*{ return b?TEXT("YES"):TEXT("NO"); };
        const FColor G(150,220,150),B(150,150,220);
        Items.Add({FString::Printf(TEXT("Surface:   %s"),yn(bS)),G});
        Items.Add({FString::Printf(TEXT("Skylands:  %s"),yn(bSk)),G});
        Items.Add({FString::Printf(TEXT("Caves:     %s"),yn(bCv)),G});
        Items.Add({FString::Printf(TEXT("Forest:    %s"),yn(bF)),G});
        Items.Add({FString::Printf(TEXT("Desert:    %s"),yn(bD)),G});
        Items.Add({FString::Printf(TEXT("Peaks:     %s"),yn(bPk)),G});
        Items.Add({FString::Printf(TEXT("Cliffs:    %s"),yn(bCl)),G});
        Items.Add({FString::Printf(TEXT("Mesa:      %s"),yn(bMs)),G});
        Items.Add({FString::Printf(TEXT("Craters:   %s"),yn(bCr)),G});
        Items.Add({FString::Printf(TEXT("Spawn in Natural Crater: %s"),yn(bFC)),B});
    }

    const int32 ItemCount = Items.Num();
    APlayerController* PC = HUD->GetOwningPlayerController();

    if (PC && !PC->bShowMouseCursor)
    {
        FInputModeGameAndUI Mode;
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        Mode.SetHideCursorDuringCapture(false);
        PC->SetInputMode(Mode);
        PC->SetShowMouseCursor(true);
    }

    if (PC)
    {
        if (PC->WasInputKeyJustPressed(EKeys::Up)  ||PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Up))
            HUD->TitleSelection=(HUD->TitleSelection-1+ItemCount)%ItemCount;
        if (PC->WasInputKeyJustPressed(EKeys::Down)||PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Down))
            HUD->TitleSelection=(HUD->TitleSelection+1)%ItemCount;
    }

    bool bMouseOver=false;
    if (PC) { float MX,MY; if (PC->GetMousePosition(MX,MY)) {
        float BY=CY2-60.f;
        for (int32 i=0;i<ItemCount;++i) {
            if (MX>=CX2-180.f&&MX<=CX2+180.f&&MY>=BY-4.f&&MY<=BY+31.f)
                if (!(!HUD->bShowWorldOptions&&i==2&&!bAnySaved)) { HUD->TitleSelection=i; bMouseOver=true; }
            BY+=50.f; } } }

    float ItemY=CY2-60.f;
    for (int32 i=0;i<ItemCount;++i)
    {
        const bool bSel=(i==HUD->TitleSelection);
        if (bSel) HUD->DrawRect(FLinearColor(0.15f,0.65f,0.25f,0.35f),CX2-180.f,ItemY-4.f,360.f,35.f);
        HUD->DrawText(Items[i].Label, bSel?FLinearColor::Green:FLinearColor(Items[i].Col), CX2-160.f,ItemY,Font,1.4f);
        ItemY+=50.f;
    }
    HUD->DrawRect(FLinearColor(0.35f,0.35f,0.45f,0.7f),CX2-220.f,ItemY+4.f,440.f,1.f);
    const FString Hint=TEXT("[Enter] Select   [Arrows] Navigate   [Click] Mouse");
    float HW,HH; HUD->GetTextSize(Hint,HW,HH,Font,1.f);
    HUD->DrawText(Hint,FLinearColor(0.55f,0.8f,1.f,1.f),CX2-HW*0.5f,ItemY+18.f,Font,1.f);

    if (PC)
    {
        const bool bConf=PC->WasInputKeyJustPressed(EKeys::Enter)||PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom)
                       ||(bMouseOver&&PC->WasInputKeyJustPressed(EKeys::LeftMouseButton));
        if (bConf)
        {
            auto Restore=[PC](){ PC->SetShowMouseCursor(false); PC->SetInputMode(FInputModeGameOnly()); };
            if (!HUD->bShowWorldOptions)
            {
                if (HUD->TitleSelection==0&&TitleWorld) { HUD->bShowTitleScreen=false; HUD->bShowLoadingScreen=true; Restore(); TitleWorld->GenerateWorld(); }
                else if (HUD->TitleSelection==1)        { HUD->bShowWorldOptions=true; HUD->TitleSelection=0; }
                else if (HUD->TitleSelection==2&&bAnySaved&&TitleWorld)
                { int32 Slot=0; for(int32 s=0;s<MaxSaveSlots;++s)if(bSlotExists[s]){Slot=s;break;}
                  HUD->bShowTitleScreen=false; Restore(); TitleWorld->ClearWorld();
                  TitleWorld->LoadFromFile(DefaultSlotNames[Slot]); TitleWorld->GenerateWorldDeferred(); }
            }
            else
            {
                if (HUD->TitleSelection==0) { HUD->bShowWorldOptions=false; HUD->TitleSelection=1; }
                else if (TitleWorld)
                {
                    auto& Perf=TitleWorld->GenerationConfig.Performance;
                    switch(HUD->TitleSelection)
                    { case 1:Perf.bEnableSurface=!Perf.bEnableSurface;break; case 2:Perf.bEnableSkylands=!Perf.bEnableSkylands;break;
                      case 3:Perf.bEnableCaves=!Perf.bEnableCaves;break;     case 4:Perf.bEnableForest=!Perf.bEnableForest;break;
                      case 5:Perf.bEnableDesert=!Perf.bEnableDesert;break;   case 6:Perf.bEnablePeaks=!Perf.bEnablePeaks;break;
                      case 7:Perf.bEnableCliffs=!Perf.bEnableCliffs;break;   case 8:Perf.bEnableMesa=!Perf.bEnableMesa;break;
                      case 9:Perf.bEnableCraters=!Perf.bEnableCraters;break;
                      case 10:TitleWorld->bSpawnInNaturalCrater=!TitleWorld->bSpawnInNaturalCrater;break;
                      default:break; }
                }
            }
        }
    }
}

// =============================================================================
//  DrawLoadingScreen
// =============================================================================
void FirstVoxelHUDDraw::DrawLoadingScreen(AFirstVoxelHUD* HUD, UFont* Font)
{
    UCanvas* Canvas = HUD->GetCanvas();
    AVoxelWorld* W  = FindVoxelWorldForHUD(HUD);
    if (!Canvas || !W) return;

    HUD->DrawRect(FLinearColor(0,0,0,0.5f),0,0,Canvas->SizeX,Canvas->SizeY);
    const float CX=Canvas->SizeX*0.5f, CY=Canvas->SizeY*0.5f;
    const float ClusterY = CY - 240.f;

    float TW,TH;
    HUD->GetTextSize(TEXT("G E N E R A T I N G   W O R L D . . ."),TW,TH,Font,1.5f);
    HUD->DrawText(TEXT("G E N E R A T I N G   W O R L D . . ."),FLinearColor::White,CX-TW*0.5f,ClusterY,Font,1.5f);

    const float Prog=W->GetGenerationProgress();
    const float BW=400.f,BH=20.f,BX=CX-200.f,BY=ClusterY+50.f;
    HUD->DrawRect(FLinearColor(0.1f,0.1f,0.1f,0.8f),BX-2.f,BY-2.f,BW+4.f,BH+4.f);
    HUD->DrawRect(FLinearColor(0.15f,0.18f,0.22f,1.f),BX,BY,BW,BH);
    HUD->DrawRect(FLinearColor(0.25f,0.85f,0.35f,1.f),BX,BY,BW*Prog,BH);

    const int32 H=W->GetQueueHead(),T=W->GetQueueCount();
    FString Status;
    Status = W->GetGenerationStatusString();

    float SW,SH2; HUD->GetTextSize(Status,SW,SH2,Font);
    HUD->DrawText(Status,FLinearColor::White,CX-SW*0.5f,BY+BH+8.f,Font);

    // Chunk grid minimap
    const float BoxSz=14.f,BoxPad=3.f; const int32 Radius=11;
    const float GW=(Radius*2+1)*(BoxSz+BoxPad),GX=CX-GW*0.5f,GY=BY+BH+40.f;
    const auto& Loaded=*W->GetLoadedChunks();
    for (int32 cy=-Radius;cy<=Radius;++cy) for (int32 cx=-Radius;cx<=Radius;++cx)
    {
        FLinearColor Col(0.1f,0.1f,0.12f,0.4f);
        if (const AVoxelChunk*const* pC=Loaded.Find(FIntVector(cx,cy,0)))
        {
            if ((*pC)->IsReady())           Col=FLinearColor(0.2f,0.8f,0.3f,0.9f);
            else if ((*pC)->IsGenerating()) Col=FLinearColor(0.9f,0.8f,0.2f,0.9f);
            else                            Col=FLinearColor(0.4f,0.4f,0.45f,0.7f);
        }
        HUD->DrawRect(Col,GX+(cx+Radius)*(BoxSz+BoxPad),GY+(cy+Radius)*(BoxSz+BoxPad),BoxSz,BoxSz);
    }

    if (!W->IsWaitingForInitialSpawn())
    {
        HUD->bShowLoadingScreen=false;
        if (APlayerController* PC=HUD->GetOwningPlayerController())
        { PC->SetShowMouseCursor(false); PC->SetInputMode(FInputModeGameOnly()); }
    }
}

// =============================================================================
//  DrawGameplayOverlay
// =============================================================================
void FirstVoxelHUDDraw::DrawGameplayOverlay(AFirstVoxelHUD* HUD, UFont* Font)
{
    UCanvas* Canvas = HUD->GetCanvas();
    if (!Canvas) return;

    // Crosshair
    const float CX=Canvas->SizeX*0.5f, CY=Canvas->SizeY*0.5f;
    HUD->DrawRect(FLinearColor::Green,CX-10.f,CY-1.f,20.f,2.f);
    HUD->DrawRect(FLinearColor::Green,CX-1.f,CY-10.f,2.f,20.f);

    AFirstVoxelCharacter* Char = Cast<AFirstVoxelCharacter>(HUD->GetOwningPawn());
    float BrushRad = 300.f; bool bGP = false;
    if (Char) { BrushRad=Char->InteractionRadius; bGP=Char->bLastInputWasGamepad; }

    const float X=20.f; float Y=20.f; const float LH=19.f;
    const FColor CT(255,220,50,255),CKB(120,200,255,255),CGP(255,150,80,255),CA(220,220,220,255),CB2(80,255,80,255);
    const FColor& BC = bGP ? CGP : CKB;

    HUD->DrawText(FString(TEXT("=== CONTROLS ==="))+(bGP?TEXT(" [Gamepad]"):TEXT(" [KB+Mouse]")),FLinearColor(CT),X,Y,Font); Y+=LH+4.f;

    if (bGP)
    {
        DrawRow(HUD,X,Y,LH,TEXT("[RT]"),BC,TEXT(": Dig"),CA,Font);
        DrawRow(HUD,X,Y,LH,TEXT("[LT]"),BC,TEXT(": Build"),CA,Font);
        DrawRow(HUD,X,Y,LH,TEXT("[RB]/[LB]"),BC,TEXT(": Brush +/-"),CA,Font);
        HUD->DrawText(FString::Printf(TEXT("Brush: [%.f]"),BrushRad),FLinearColor(CB2),X,Y,Font); Y+=LH+4.f;
        static const FString TN[]={TEXT("DIG"),TEXT("BUILD"),TEXT("SMOOTH"),TEXT("FLATTEN")};
        const int32 TI=Char?(int32)Char->CurrentTool:0;
        HUD->DrawText(FString::Printf(TEXT("Tool: [%s]"),*TN[FMath::Clamp(TI,0,3)]),FLinearColor(255,210,100),X,Y,Font); Y+=LH+6.f;
        DrawRow(HUD,X,Y,LH,TEXT("[B]"),BC,TEXT(": Toggle Flight"),CA,Font);
        DrawRow(HUD,X,Y,LH,TEXT("[A]"),BC,TEXT(": Jump/Fly Up"),CA,Font);
        DrawRow(HUD,X,Y,LH,TEXT("[X]"),BC,TEXT(": Fly Down"),CA,Font);
        DrawRow(HUD,X,Y,LH,TEXT("[Y]"),BC,TEXT(": Map"),CA,Font);
        DrawRow(HUD,X,Y,LH,TEXT("[Start]"),BC,TEXT(": Pause"),CA,Font);
    }
    else
    {
        DrawRow(HUD,X,Y,LH,TEXT("[LMB]"),BC,TEXT(": Dig"),CA,Font);
        DrawRow(HUD,X,Y,LH,TEXT("[RMB]"),BC,TEXT(": Build"),CA,Font);
        DrawRow(HUD,X,Y,LH,TEXT("[Scroll]"),BC,TEXT(": Brush +/-"),CA,Font);
        HUD->DrawText(FString::Printf(TEXT("Brush: [%.f]"),BrushRad),FLinearColor(CB2),X,Y,Font); Y+=LH+4.f;
        static const FString TN[]={TEXT("DIG"),TEXT("BUILD"),TEXT("SMOOTH"),TEXT("FLATTEN")};
        const int32 TI=Char?(int32)Char->CurrentTool:0;
        HUD->DrawText(FString::Printf(TEXT("Tool: [%s]"),*TN[FMath::Clamp(TI,0,3)]),FLinearColor(255,210,100),X,Y,Font); Y+=LH+6.f;
        DrawRow(HUD,X,Y,LH,TEXT("[F]"),BC,TEXT(": Toggle Flight"),CA,Font);
        DrawRow(HUD,X,Y,LH,TEXT("[Space]"),BC,TEXT(": Jump/Fly Up"),CA,Font);
        DrawRow(HUD,X,Y,LH,TEXT("[Ctrl]"),BC,TEXT(": Fly Down"),CA,Font);
        DrawRow(HUD,X,Y,LH,TEXT("[M]"),BC,TEXT(": Map"),CA,Font);
        DrawRow(HUD,X,Y,LH,TEXT("[P]"),BC,TEXT(": Pause"),CA,Font);
    }

    // FPS — use HUD->GetWorld() not bare GetWorld()
    UWorld* W = HUD->GetWorld();
    if (W)
    {
        const float DT = W->GetDeltaSeconds();
        const float FPS = (DT > 0.f) ? 1.f/DT : 0.f;
        const FString FT = FString::Printf(TEXT("FPS: %.1f"),FPS);
        float FW2,FH2; HUD->GetTextSize(FT,FW2,FH2,Font);
        const float FX=Canvas->SizeX-FW2-20.f;
        HUD->DrawRect(FLinearColor(0,0,0,0.45f),FX-4.f,18.f,FW2+8.f,FH2+4.f);
        HUD->DrawText(FT,FLinearColor(0.3f,1.f,0.3f,1.f),FX,20.f,Font);
    }

    if (Char)
    {
        const FVector Loc=Char->GetActorLocation();
        const FString CoordT=FString::Printf(TEXT("X: %.1f  Y: %.1f  Z: %.1f"),Loc.X,Loc.Y,Loc.Z);
        float CW2,CH2; HUD->GetTextSize(CoordT,CW2,CH2,Font);
        const float CoordX=Canvas->SizeX-CW2-20.f;
        HUD->DrawRect(FLinearColor(0,0,0,0.45f),CoordX-4.f,48.f,CW2+8.f,CH2+4.f);
        HUD->DrawText(CoordT,FLinearColor(0.8f,0.8f,1.f,1.f),CoordX,50.f,Font);

        const FString AltT=FString::Printf(TEXT("Alt: %.0f cm"),Loc.Z);
        float AW,AH; HUD->GetTextSize(AltT,AW,AH,Font);
        const float AX=Canvas->SizeX-AW-20.f;
        HUD->DrawRect(FLinearColor(0,0,0,0.45f),AX-4.f,73.f,AW+8.f,AH+4.f);
        HUD->DrawText(AltT,FLinearColor(1.f,0.8f,0.3f,1.f),AX,75.f,Font);
    }
}

// =============================================================================
//  DrawToolWheel
// =============================================================================
void FirstVoxelHUDDraw::DrawToolWheel(AFirstVoxelHUD* HUD, UFont* Font)
{
    UCanvas* Canvas = HUD->GetCanvas();
    AFirstVoxelCharacter* Char = Cast<AFirstVoxelCharacter>(HUD->GetOwningPawn());
    if (!Canvas || !Char || !Char->bToolWheelOpen) return;

    HUD->DrawRect(FLinearColor(0,0,0,0.45f),0,0,Canvas->SizeX,Canvas->SizeY);
    const float CX=Canvas->SizeX*0.5f,CY=Canvas->SizeY*0.5f,R=160.f;
    static const FString TN[]={TEXT("DIG"),TEXT("BUILD"),TEXT("SMOOTH"),TEXT("FLATTEN")};
    const FVector2D Off[]={{-R,0},{0,-R},{R,0},{0,R}};
    const int32 SelIdx=(int32)Char->CurrentTool;

    for (int32 i=0;i<4;++i)
    {
        const bool bSel=(i==SelIdx);
        float TW,TH; HUD->GetTextSize(TN[i],TW,TH,Font);
        const float DX=CX+Off[i].X-TW*0.5f,DY=CY+Off[i].Y-TH*0.5f;
        if (bSel) HUD->DrawRect(FLinearColor(0.2f,0.8f,0.2f,0.4f),DX-12.f,DY-6.f,TW+24.f,TH+12.f);
        HUD->DrawText(TN[i],bSel?FLinearColor::Green:FLinearColor::White,DX,DY,Font);
    }
    HUD->DrawRect(FLinearColor::White,CX-3.f,CY-3.f,6.f,6.f);

    if (!Char->bLastInputWasGamepad)
    {
        float MX,MY;
        APlayerController* PC=HUD->GetOwningPlayerController();
        if (PC && PC->GetMousePosition(MX,MY))
            HUD->DrawLine(CX,CY,MX,MY,FLinearColor::Yellow,2.f);
    }
}
