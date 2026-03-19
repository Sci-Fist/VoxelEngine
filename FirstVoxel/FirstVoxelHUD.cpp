// FirstVoxelHUD.cpp
// Draws crosshair + context-aware control labels + pause menu.
// Automatically switches between Keyboard/Mouse and Gamepad hints
// based on AFirstVoxelCharacter::bLastInputWasGamepad.
#include "FirstVoxelHUD.h"
#include "UI/VoxelPauseMenu.h"   // includes DefaultSlotNames, MaxSaveSlots
#include "Engine/Canvas.h"
#include "FirstVoxelCharacter.h"
#include "Voxel/Core/World/VoxelWorld.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

// ---------------------------------------------------------------------------
// Small helper: draw a label + value pair with a coloured key/button badge
// ---------------------------------------------------------------------------
namespace
{
    void DrawRow(AHUD* HUD, float X, float& Y, float LineH,
                 const FString& Badge,  const FColor& BadgeCol,
                 const FString& Action, const FColor& ActionCol,
                 UFont* Font)
    {
        HUD->DrawText(Badge,  FLinearColor(BadgeCol),  X,         Y, Font);
        HUD->DrawText(Action, FLinearColor(ActionCol), X + 110.f, Y, Font);
        Y += LineH;
    }
}

// ============================================================
//  BeginPlay — create pause menu
// ============================================================
void AFirstVoxelHUD::BeginPlay()
{
    Super::BeginPlay();
    PauseMenu = NewObject<UVoxelPauseMenu>(this);
    PauseMenu->Init(this);
}

bool AFirstVoxelHUD::IsPaused() const
{
    return PauseMenu && PauseMenu->IsOpen();
}

void AFirstVoxelHUD::TogglePause()
{
    if (!PauseMenu) return;
    if (PauseMenu->IsOpen())
        PauseMenu->Close();
    else
        PauseMenu->Open();
}

void AFirstVoxelHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;

    // ── Pause menu takes full priority when open ──────────────────────────
    if (PauseMenu && PauseMenu->IsOpen())
    {
        PauseMenu->Draw();
        return;
    }

    UFont* const SmallFont = GEngine ? GEngine->GetSmallFont() : nullptr;
    if (!CachedVoxelWorld)
    {
        TArray<AActor*> WA;
        UGameplayStatics::GetAllActorsOfClass(this, AVoxelWorld::StaticClass(), WA);
        if (WA.Num() > 0) CachedVoxelWorld = Cast<AVoxelWorld>(WA[0]);
    }
    AVoxelWorld* TitleWorld = CachedVoxelWorld;

    // ── Loading screen ────────────────────────────────────────────────────
    if (bShowLoadingScreen && TitleWorld)
    {
        DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);

        const float CX2 = Canvas->SizeX * 0.5f;
        const float CY2 = Canvas->SizeY * 0.5f;

        float TextW, TextH;
        GetTextSize(TEXT("G E N E R A T I N G   W O R L D . . ."), TextW, TextH, SmallFont, 1.5f);
        DrawText(TEXT("G E N E R A T I N G   W O R L D . . ."),
                 FLinearColor(1.f, 1.f, 1.f, 1.f),
                 CX2 - TextW * 0.5f, CY2 - 40.f, SmallFont, 1.5f);

        const float Progress  = TitleWorld->GetGenerationProgress();
        const float BarWidth  = 400.f;
        const float BarHeight = 20.f;
        const float BarX      = CX2 - BarWidth * 0.5f;
        const float BarY      = CY2 + 10.f;

        DrawRect(FLinearColor(0.1f,  0.1f,  0.1f,  0.8f),  BarX - 2.f, BarY - 2.f, BarWidth + 4.f, BarHeight + 4.f);
        DrawRect(FLinearColor(0.15f, 0.18f, 0.22f, 1.0f),  BarX, BarY, BarWidth, BarHeight);
        DrawRect(FLinearColor(0.25f, 0.85f, 0.35f, 1.0f),  BarX, BarY, BarWidth * Progress, BarHeight);

        // ── Descriptive Text ──────────────────────────────────────────
        const int32 Head = TitleWorld->GetQueueHead();
        const int32 Total = TitleWorld->GetQueueCount();
        FString StatusText = FString::Printf(TEXT("Chunks: %d / %d"), Head, Total);
        
        if (TitleWorld->IsWaitingForInitialSpawn())
        {
            StatusText = FString::Printf(TEXT("Preparing Spawning Area: %d / %d"), Head, Total);
        }
        else if (Head >= Total && Total > 0)
        {
            StatusText = TEXT("Finalizing geometry... Ready shortly.");
        }
        else if (Total <= 0)
        {
            StatusText = TEXT("Requesting generation coordinates...");
        }
        
        float StatusW, StatusH;
        GetTextSize(StatusText, StatusW, StatusH, SmallFont, 1.0f);
        DrawText(StatusText, FLinearColor::White, CX2 - StatusW * 0.5f, BarY + BarHeight + 8.f, SmallFont, 1.0f);

        // ── Dynamic Map Matrix ─────────────────────────────────────────
        const float BoxSz  = 14.f;
        const float BoxPad = 3.f;
        const int32 Radius = 11; // 23x23 grid
        
        const float GridWidth  = (Radius * 2 + 1) * (BoxSz + BoxPad);
        const float GridX      = CX2 - GridWidth * 0.5f;
        const float GridY      = BarY + BarHeight + 40.f;

        const auto& Loaded = *TitleWorld->GetLoadedChunks();

        for (int32 cy = -Radius; cy <= Radius; ++cy)
        {
            for (int32 cx = -Radius; cx <= Radius; ++cx)
            {
                const FIntVector Coord(cx, cy, 0); 
                FLinearColor BoxCol = FLinearColor(0.1f, 0.1f, 0.12f, 0.4f); // Empty background

                if (const AVoxelChunk* const* pChunk = Loaded.Find(Coord))
                {
                    const AVoxelChunk* Chunk = *pChunk;
                    if (Chunk)
                    {
                        if (Chunk->IsReady())
                            BoxCol = FLinearColor(0.2f, 0.8f, 0.3f, 0.9f); // Green: Loaded
                        else if (Chunk->IsGenerating())
                            BoxCol = FLinearColor(0.9f, 0.8f, 0.2f, 0.9f); // Yellow: Active
                        else
                            BoxCol = FLinearColor(0.4f, 0.4f, 0.45f, 0.7f); // Grey: Initialized
                    }
                }
                
                const float DrawX = GridX + (cx + Radius) * (BoxSz + BoxPad);
                const float DrawY = GridY + (cy + Radius) * (BoxSz + BoxPad);
                DrawRect(BoxCol, DrawX, DrawY, BoxSz, BoxSz);
            }
        }

        if (!TitleWorld->IsWaitingForInitialSpawn())
        {
            bShowLoadingScreen = false;
            if (APlayerController* PC = GetOwningPlayerController())
            {
                PC->SetShowMouseCursor(false);
                FInputModeGameOnly GM;
                PC->SetInputMode(GM);
            }
        }
        return;
    }

    // ── Title / Menu screen ───────────────────────────────────────────────
    if (bShowTitleScreen)
    {
        DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);

        const float CX2 = Canvas->SizeX * 0.5f;
        const float CY2 = Canvas->SizeY * 0.5f;

        // ── Title text ────────────────────────────────────────────────
        float TitleW, TitleH;
        GetTextSize(TEXT("M E N U"), TitleW, TitleH, SmallFont, 2.f);
        DrawText(TEXT("M E N U"),
                 FLinearColor(1.f, 0.85f, 0.2f, 1.f),
                 CX2 - TitleW * 0.5f, CY2 - 130.f, SmallFont, 2.f);

        DrawRect(FLinearColor(0.35f, 0.35f, 0.45f, 0.7f), CX2 - 220.f, CY2 - 85.f, 440.f, 1.f);

        // ── Slot existence check ──────────────────────────────────────
        bool bSlotExists[5] = {};
        if (TitleWorld)
        {
            for (int32 s = 0; s < 5; ++s)
            {
                FString Path = FPaths::Combine(
                    FPaths::ProjectSavedDir(), TEXT("VoxelSaves"),
                    FString::Printf(TEXT("%s_%s.sav"), *TitleWorld->GetName(), *DefaultSlotNames[s]));
                bSlotExists[s] = FPaths::FileExists(Path);
            }
        }
        const bool bAnySaved = bSlotExists[0] || bSlotExists[1] || bSlotExists[2] ||
                               bSlotExists[3] || bSlotExists[4];

        struct TitleItem { FString Label; FColor Col; };
        TArray<TitleItem> Items;

        if (!bShowWorldOptions)
        {
            Items.Add({ TEXT("Generate New World"), FColor(220,220,220) });
            Items.Add({ TEXT("World Options..."), FColor(220,220,220) });
            Items.Add({ bAnySaved
                ? TEXT("Load World (Slot 1)")
                : TEXT("Load World  (no saves)"),
              bAnySaved ? FColor(220,220,220) : FColor(100,100,100) });
        }
        else
        {
            Items.Add({ TEXT("< Back"), FColor(200,200,200) });

            bool bSurf = true; bool bSky = true; bool bCave = true;
            bool bFor = true; bool bDes = true; bool bPk = true;
            bool bClf = true; bool bMsa = true; bool bCrat = true;
            bool bForceCrat = false;

            if (TitleWorld)
            {
                const auto& Perf = TitleWorld->GenerationConfig.Performance;
                bSurf = Perf.bEnableSurface;
                bSky  = Perf.bEnableSkylands;
                bCave = Perf.bEnableCaves;
                bFor  = Perf.bEnableForest;
                bDes  = Perf.bEnableDesert;
                bPk   = Perf.bEnablePeaks;
                bClf  = Perf.bEnableCliffs;
                bMsa  = Perf.bEnableMesa;
                bCrat = Perf.bEnableCraters;
                bForceCrat = TitleWorld->bForceCraterSpawn;
            }

            Items.Add({ FString::Printf(TEXT("Surface:   %s"), bSurf ? TEXT("YES") : TEXT("NO")), FColor(150,220,150) });
            Items.Add({ FString::Printf(TEXT("Skylands:  %s"), bSky  ? TEXT("YES") : TEXT("NO")), FColor(150,220,150) });
            Items.Add({ FString::Printf(TEXT("Caves:     %s"), bCave ? TEXT("YES") : TEXT("NO")), FColor(150,220,150) });
            Items.Add({ FString::Printf(TEXT("Forest:    %s"), bFor  ? TEXT("YES") : TEXT("NO")), FColor(150,220,150) });
            Items.Add({ FString::Printf(TEXT("Desert:    %s"), bDes  ? TEXT("YES") : TEXT("NO")), FColor(150,220,150) });
            Items.Add({ FString::Printf(TEXT("Peaks:     %s"), bPk   ? TEXT("YES") : TEXT("NO")), FColor(150,220,150) });
            Items.Add({ FString::Printf(TEXT("Cliffs:    %s"), bClf  ? TEXT("YES") : TEXT("NO")), FColor(150,220,150) });
            Items.Add({ FString::Printf(TEXT("Mesa:      %s"), bMsa  ? TEXT("YES") : TEXT("NO")), FColor(150,220,150) });
            Items.Add({ FString::Printf(TEXT("Craters:   %s"), bCrat ? TEXT("YES") : TEXT("NO")), FColor(150,220,150) });
            Items.Add({ FString::Printf(TEXT("Force Crater Spawn: %s"), bForceCrat ? TEXT("YES") : TEXT("NO")), FColor(150,150,220) });
        }

        const int32 ItemCount = Items.Num();

        APlayerController* PC = GetOwningPlayerController();

        // ── Ensure cursor is visible for this screen ──────────────────
        // IMPORTANT: We set FInputModeGameAndUI (not UIOnly) so that
        // WasInputKeyJustPressed works for ALL keys AND the mouse is visible.
        // FInputModeUIOnly routes mouse clicks to Slate widgets only — since
        // this menu is drawn on the HUD canvas (not a Slate widget), LMB clicks
        // never reach WasInputKeyJustPressed in UIOnly mode.
        // FInputModeGameAndUI keeps the input in the game chain so the HUD
        // can poll WasInputKeyJustPressed(LMB) directly.
        if (PC && !PC->bShowMouseCursor)
        {
            FInputModeGameAndUI Mode;
            Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            Mode.SetHideCursorDuringCapture(false);
            PC->SetInputMode(Mode);
            PC->SetShowMouseCursor(true);
        }

        // ── Navigation (keyboard / gamepad) ──────────────────────────
        if (PC)
        {
            if (PC->WasInputKeyJustPressed(EKeys::Up)   || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Up))
                TitleSelection = (TitleSelection - 1 + ItemCount) % ItemCount;
            if (PC->WasInputKeyJustPressed(EKeys::Down) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Down))
                TitleSelection = (TitleSelection + 1) % ItemCount;
        }

        // ── Mouse hover detection ─────────────────────────────────────
        bool bMouseOverButton = false;
        if (PC)
        {
            float MouseX, MouseY;
            if (PC->GetMousePosition(MouseX, MouseY))
            {
                float BaseY = CY2 - 60.f;   // matches ItemY below
                for (int32 i = 0; i < ItemCount; ++i)
                {
                    const float HitL = CX2 - 180.f;
                    const float HitR = CX2 + 180.f;
                    const float HitT = BaseY - 4.f;
                    const float HitB = BaseY + 31.f;  // ~35px hit zone per item

                    if (MouseX >= HitL && MouseX <= HitR &&
                        MouseY >= HitT && MouseY <= HitB)
                    {
                        if (!bShowWorldOptions || i == 0 || TitleWorld)
                        {
                            if (!bShowWorldOptions && i == 2 && !bAnySaved)
                            {
                                // skip load if no saves
                            }
                            else
                            {
                                TitleSelection    = i;
                                bMouseOverButton  = true;
                            }
                        }
                    }
                    BaseY += 50.f;
                }
            }
        }

        // ── Render menu items ─────────────────────────────────────────
        float ItemY = CY2 - 60.f;
        for (int32 i = 0; i < ItemCount; ++i)
        {
            const bool bSel = (i == TitleSelection);
            if (bSel)
                DrawRect(FLinearColor(0.15f, 0.65f, 0.25f, 0.35f), CX2 - 180.f, ItemY - 4.f, 360.f, 35.f);

            const FLinearColor TextCol = bSel
                ? FLinearColor::Green
                : FLinearColor(Items[i].Col);

            DrawText(Items[i].Label, TextCol, CX2 - 160.f, ItemY, SmallFont, 1.4f);
            ItemY += 50.f;
        }

        DrawRect(FLinearColor(0.35f, 0.35f, 0.45f, 0.7f), CX2 - 220.f, ItemY + 4.f, 440.f, 1.f);

        // Show appropriate hint depending on device
        const FString HintStr = TEXT("[Enter] Select   [Arrows] Navigate   [Click] Mouse");
        float HintW, HintH;
        GetTextSize(HintStr, HintW, HintH, SmallFont, 1.0f);
        DrawText(HintStr, FLinearColor(0.55f, 0.8f, 1.f, 1.f),
                 CX2 - HintW * 0.5f, ItemY + 18.f, SmallFont, 1.0f);

        // ── Confirm action ────────────────────────────────────────────
        // Accepts: Enter key, Gamepad A, or left mouse button while over a button.
        // NOTE: WasInputKeyJustPressed(LMB) works here because we use
        // FInputModeGameAndUI, which keeps LMB in the game input chain.
        if (PC)
        {
            const bool bKeyConfirm   = PC->WasInputKeyJustPressed(EKeys::Enter) ||
                                       PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom);
            const bool bClickConfirm = bMouseOverButton &&
                                       PC->WasInputKeyJustPressed(EKeys::LeftMouseButton);
            const bool bConfirm      = bKeyConfirm || bClickConfirm;

            if (bConfirm)
            {
                // Restore full game input so the character can move/look/dig
                auto RestoreGameInput = [PC]()
                {
                    PC->SetShowMouseCursor(false);
                    FInputModeGameOnly GM;
                    PC->SetInputMode(GM);
                };

                if (!bShowWorldOptions)
                {
                    if (TitleSelection == 0) // Generate New World
                    {
                        if (TitleWorld)
                        {
                            bShowTitleScreen   = false;
                            bShowLoadingScreen = true;
                            RestoreGameInput();
                            TitleWorld->GenerateWorld();
                        }
                    }
                    else if (TitleSelection == 1) // World Options...
                    {
                        bShowWorldOptions = true;
                        TitleSelection = 0;
                    }
                    else if (TitleSelection == 2 && bAnySaved) // Load World
                    {
                        if (TitleWorld)
                        {
                            int32 FirstSlot = 0;
                            for (int32 s = 0; s < 5; ++s) { if (bSlotExists[s]) { FirstSlot = s; break; } }

                            bShowTitleScreen = false;
                            RestoreGameInput();
                            TitleWorld->ClearWorld();
                            TitleWorld->LoadFromFile(DefaultSlotNames[FirstSlot]);
                            TitleWorld->GenerateWorldDeferred();
                        }
                    }
                }
                else
                {
                    if (TitleSelection == 0) // < Back
                    {
                        bShowWorldOptions = false;
                        TitleSelection = 1; // highlight "World Options..."
                    }
                    else if (TitleWorld)
                    {
                        auto& Perf = TitleWorld->GenerationConfig.Performance;
                        if (TitleSelection == 1)      Perf.bEnableSurface = !Perf.bEnableSurface;
                        else if (TitleSelection == 2) Perf.bEnableSkylands = !Perf.bEnableSkylands;
                        else if (TitleSelection == 3) Perf.bEnableCaves = !Perf.bEnableCaves;
                        else if (TitleSelection == 4) Perf.bEnableForest = !Perf.bEnableForest;
                        else if (TitleSelection == 5) Perf.bEnableDesert = !Perf.bEnableDesert;
                        else if (TitleSelection == 6) Perf.bEnablePeaks = !Perf.bEnablePeaks;
                        else if (TitleSelection == 7) Perf.bEnableCliffs = !Perf.bEnableCliffs;
                        else if (TitleSelection == 8) Perf.bEnableMesa = !Perf.bEnableMesa;
                        else if (TitleSelection == 9) Perf.bEnableCraters = !Perf.bEnableCraters;
                        else if (TitleSelection == 10) TitleWorld->bForceCraterSpawn = !TitleWorld->bForceCraterSpawn;
                    }
                }
            }
        }
        return;
    }

    // ── Crosshair ─────────────────────────────────────────────────────────
    const float CX = Canvas->SizeX * 0.5f;
    const float CY = Canvas->SizeY * 0.5f;
    DrawRect(FLinearColor::Green, CX - 10.f, CY -  1.f, 20.f,  2.f);
    DrawRect(FLinearColor::Green, CX -  1.f, CY - 10.f,  2.f, 20.f);

    // ── Read character state ──────────────────────────────────────────────
    float BrushRad = 300.f;
    bool  bGamepad = false;

    AFirstVoxelCharacter* Char = nullptr;
    if (APawn* Pawn = GetOwningPawn())
    {
        Char = Cast<AFirstVoxelCharacter>(Pawn);
        if (Char)
        {
            BrushRad = Char->InteractionRadius;
            bGamepad = Char->bLastInputWasGamepad;
        }
    }

    // ── Layout constants ──────────────────────────────────────────────────
    const float X     = 20.f;
    float       Y     = 20.f;
    const float LineH = 19.f;

    const FColor ColTitle  (255, 220,  50, 255);
    const FColor ColBadgeKB(120, 200, 255, 255);
    const FColor ColBadgeGP(255, 150,  80, 255);
    const FColor ColAction (220, 220, 220, 255);
    const FColor ColBrush  ( 80, 255,  80, 255);

    const FString DeviceTag = bGamepad ? TEXT(" [Gamepad]") : TEXT(" [KB+Mouse]");
    DrawText(FString(TEXT("=== CONTROLS ===")) + DeviceTag,
             FLinearColor(ColTitle), X, Y, SmallFont);
    Y += LineH + 4.f;

    const FColor& BC = bGamepad ? ColBadgeGP : ColBadgeKB;

    if (bGamepad)
    {
        DrawRow(this, X, Y, LineH, TEXT("[RT]"),          BC, TEXT(": Dig"),              ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[LT]"),          BC, TEXT(": Build"),            ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[RB] / [LB]"),   BC, TEXT(": Brush Radius +/-"), ColAction, SmallFont);
        DrawText(FString::Printf(TEXT("Brush Size : [%.f]"), BrushRad), FLinearColor(ColBrush), X, Y, SmallFont); Y += LineH + 4.f;
        
        static const FString ToolNames[] = { TEXT("DIG"), TEXT("BUILD"), TEXT("SMOOTH"), TEXT("FLATTEN") };
        const int32 ToolIdx = Char ? static_cast<int32>(Char->CurrentTool) : 0;
        DrawText(FString::Printf(TEXT("Current Tool : [%s]"), *ToolNames[FMath::Clamp(ToolIdx, 0, 3)]), FLinearColor(255, 210, 100), X, Y, SmallFont); Y += LineH + 6.f;
        DrawRow(this, X, Y, LineH, TEXT("[B]"),           BC, TEXT(": Toggle Flight"),    ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[A]"),           BC, TEXT(": Jump / Fly Up"),    ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[X]"),           BC, TEXT(": Fly Down"),         ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Y]"),           BC, TEXT(": Map"),              ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Start]"),       BC, TEXT(": Pause"),            ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Left Stick]"),  BC, TEXT(": Move"),             ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Right Stick]"), BC, TEXT(": Look"),             ColAction, SmallFont);
    }
    else
    {
        DrawRow(this, X, Y, LineH, TEXT("[LMB]"),   BC, TEXT(": Dig (hold)"),       ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[RMB]"),   BC, TEXT(": Build (hold)"),     ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Scroll]"),BC, TEXT(": Brush Radius +/-"), ColAction, SmallFont);
        DrawText(FString::Printf(TEXT("Brush Size : [%.f]"), BrushRad), FLinearColor(ColBrush), X, Y, SmallFont); Y += LineH + 4.f;

        static const FString ToolNames[] = { TEXT("DIG"), TEXT("BUILD"), TEXT("SMOOTH"), TEXT("FLATTEN") };
        const int32 ToolIdx = Char ? static_cast<int32>(Char->CurrentTool) : 0;
        DrawText(FString::Printf(TEXT("Current Tool : [%s]"), *ToolNames[FMath::Clamp(ToolIdx, 0, 3)]), FLinearColor(255, 210, 100), X, Y, SmallFont); Y += LineH + 6.f;
        DrawRow(this, X, Y, LineH, TEXT("[F]"),     BC, TEXT(": Toggle Flight"),    ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Space]"), BC, TEXT(": Jump / Fly Up"),    ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Ctrl]"),  BC, TEXT(": Fly Down"),         ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[M]"),     BC, TEXT(": Map"),              ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[P]"),     BC, TEXT(": Pause"),            ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[WASD]"),  BC, TEXT(": Move"),             ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Mouse]"), BC, TEXT(": Look"),             ColAction, SmallFont);
    }

    // ── Tool Wheel Overlay ────────────────────────────────────────────────
    AFirstVoxelCharacter* ToolChar = Cast<AFirstVoxelCharacter>(GetOwningPawn());
    if (ToolChar && ToolChar->bToolWheelOpen)
    {
        DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);

        const float CenterX = Canvas->SizeX * 0.5f;
        const float CenterY = Canvas->SizeY * 0.5f;
        const float Radius  = 160.f;

        FString ToolNames[] = { TEXT("DIG"), TEXT("BUILD"), TEXT("SMOOTH"), TEXT("FLATTEN") };
        FVector2D Offsets[] = {
            FVector2D(-Radius, 0.f),
            FVector2D(0.f, -Radius),
            FVector2D(Radius, 0.f),
            FVector2D(0.f, Radius)
        };

        const int32 SelectedIdx = static_cast<int32>(Char->CurrentTool);

        for (int32 i = 0; i < 4; ++i)
        {
            const bool bSel = (i == SelectedIdx);
            float TextW, TextH;
            GetTextSize(ToolNames[i], TextW, TextH, SmallFont);

            const float DrawX = CenterX + Offsets[i].X - (TextW * 0.5f);
            const float DrawY = CenterY + Offsets[i].Y - (TextH * 0.5f);

            if (bSel)
                DrawRect(FLinearColor(0.2f, 0.8f, 0.2f, 0.4f),
                         DrawX - 12.f, DrawY - 6.f, TextW + 24.f, TextH + 12.f);

            DrawText(ToolNames[i],
                     bSel ? FLinearColor::Green : FLinearColor::White,
                     DrawX, DrawY, SmallFont);
        }

        DrawRect(FLinearColor::White, CenterX - 3.f, CenterY - 3.f, 6.f, 6.f);

        if (!Char->bLastInputWasGamepad)
        {
            float MouseX, MouseY;
            APlayerController* PC2 = GetOwningPlayerController();
            if (PC2 && PC2->GetMousePosition(MouseX, MouseY))
                DrawLine(CenterX, CenterY, MouseX, MouseY, FLinearColor::Yellow, 2.f);
        }
    }

    // ── FPS Counter (Top Right) ──────────────────────────────────────────
    if (Canvas && GEngine)
    {
        const float Delta = GetWorld()->GetDeltaSeconds();
        const float FPS = Delta > 0.f ? 1.0f / Delta : 0.f;
        FString FPSText = FString::Printf(TEXT("FPS: %.1f"), FPS);
        
        float TextW, TextH;
        GetTextSize(FPSText, TextW, TextH, SmallFont);
        
        const float DrawX = Canvas->SizeX - TextW - 20.f;
        const float DrawY = 20.f;

        DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), DrawX - 4.f, DrawY - 2.f, TextW + 8.f, TextH + 4.f);
        DrawText(FPSText, FLinearColor(0.3f, 1.f, 0.3f, 1.f), DrawX, DrawY, SmallFont);
    }

    // ── Coordinates and Altitude Display (Below FPS) ─────────────────────
    if (Canvas && GEngine)
    {
        AFirstVoxelCharacter* Char = Cast<AFirstVoxelCharacter>(GetOwningPawn());
        if (Char)
        {
            FVector Location = Char->GetActorLocation();
            
            // Coordinates display (X, Y, Z)
            FString CoordText = FString::Printf(TEXT("X: %.1f  Y: %.1f  Z: %.1f"), 
                Location.X, Location.Y, Location.Z);
            
            float CoordW, CoordH;
            GetTextSize(CoordText, CoordW, CoordH, SmallFont);
            
            const float CoordX = Canvas->SizeX - CoordW - 20.f;
            const float CoordY = 50.f; // Below FPS counter

            DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), CoordX - 4.f, CoordY - 2.f, CoordW + 8.f, CoordH + 4.f);
            DrawText(CoordText, FLinearColor(0.8f, 0.8f, 1.0f, 1.f), CoordX, CoordY, SmallFont);

            // Altitude display (Y-up coordinate)
            FString AltText = FString::Printf(TEXT("Altitude: %.1f cm"), Location.Z);
            
            float AltW, AltH;
            GetTextSize(AltText, AltW, AltH, SmallFont);
            
            const float AltX = Canvas->SizeX - AltW - 20.f;
            const float AltY = 75.f; // Below coordinates

            DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), AltX - 4.f, AltY - 2.f, AltW + 8.f, AltH + 4.f);
            DrawText(AltText, FLinearColor(1.0f, 0.8f, 0.3f, 1.f), AltX, AltY, SmallFont);
        }
    }
}
