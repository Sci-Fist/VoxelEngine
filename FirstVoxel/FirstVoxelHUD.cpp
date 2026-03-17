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
    /**
     * Draw one HUD row: a coloured badge (key/button label) followed by an action description.
     * Font is passed in so the caller can cache GetSmallFont() once per DrawHUD call
     * instead of allocating a new font descriptor on every row.
     */
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

    // Cache the font pointer once per frame rather than calling GetSmallFont() on every DrawRow.
    // GEngine->GetSmallFont() allocates a new descriptor each call which adds up at 60 fps.
    UFont* const SmallFont = GEngine ? GEngine->GetSmallFont() : nullptr;
    AVoxelWorld* TitleWorld = nullptr;
    {
        TArray<AActor*> WA;
        UGameplayStatics::GetAllActorsOfClass(this, AVoxelWorld::StaticClass(), WA);
        if (WA.Num() > 0) TitleWorld = Cast<AVoxelWorld>(WA[0]);
    }

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

        float Progress = TitleWorld->GetGenerationProgress();
        float BarWidth = 400.f;
        float BarHeight = 20.f;
        float BarX = CX2 - BarWidth * 0.5f;
        float BarY = CY2 + 10.f;

        DrawRect(FLinearColor(0.1f, 0.1f, 0.1f, 0.8f), BarX - 2.f, BarY - 2.f, BarWidth + 4.f, BarHeight + 4.f);
        DrawRect(FLinearColor(0.15f, 0.18f, 0.22f, 1.0f), BarX, BarY, BarWidth, BarHeight);
        DrawRect(FLinearColor(0.25f, 0.85f, 0.35f, 1.0f), BarX, BarY, BarWidth * Progress, BarHeight);

        if (!TitleWorld->IsWaitingForInitialSpawn())
        {
            bShowLoadingScreen = false;
            if (APlayerController* PC = GetOwningPlayerController())
            {
                PC->SetShowMouseCursor(false);
                FInputModeGameOnly GM; PC->SetInputMode(GM);
            }
        }
        return; 
    }

    if (bShowTitleScreen)
    {
        // ── Full-screen slightly dark background for debugging see-through ───
        DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.35f), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);

        const float CX2 = Canvas->SizeX * 0.5f;
        const float CY2 = Canvas->SizeY * 0.5f;

        // ── Title ──────────────────────────────────────────────────────
        float TitleW, TitleH;
        GetTextSize(TEXT("V O X E L   E N G I N E"), TitleW, TitleH, SmallFont, 2.f);
        DrawText(TEXT("V O X E L   E N G I N E"),
                 FLinearColor(1.f, 0.85f, 0.2f, 1.f),
                 CX2 - TitleW * 0.5f, CY2 - 130.f, SmallFont, 2.f);

        // Divider
        DrawRect(FLinearColor(0.35f, 0.35f, 0.45f, 0.7f), CX2 - 220.f, CY2 - 85.f, 440.f, 1.f);

        // ── Menu options ─────────────────────────────────────────────

        bool bSlotExists[5] = {};
        if (TitleWorld)
        {
            for (int32 s = 0; s < 5; ++s)
            {
                FString SlotName = DefaultSlotNames[s];
                FString Path = FPaths::Combine(
                    FPaths::ProjectSavedDir(), TEXT("VoxelSaves"),
                    FString::Printf(TEXT("%s_%s.sav"), *TitleWorld->GetName(), *SlotName));
                bSlotExists[s] = FPaths::FileExists(Path);
            }
        }

        const bool bAnySaved = bSlotExists[0] || bSlotExists[1] || bSlotExists[2] ||
                               bSlotExists[3] || bSlotExists[4];

        struct TitleItem { FString Label; FColor Col; };
        const TitleItem Items[] = {
            { TEXT("Generate New World"),     FColor(220,220,220) },
            { bAnySaved
                ? TEXT("Load World (Slot 1)")
                : TEXT("Load World  (no saves)"),
              bAnySaved ? FColor(220,220,220) : FColor(100,100,100) },
        };

        // ── Input polling & Mouse Hover setup ─────────────────────────────
        APlayerController* PC = GetOwningPlayerController();
        if (PC)
        {
            if (!PC->bShowMouseCursor)
            {
                PC->SetShowMouseCursor(true);
            }

            // Arrow / DPad Navigation
            if (PC->WasInputKeyJustPressed(EKeys::Up) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Up))
                TitleSelection = (TitleSelection - 1 + 2) % 2;
            if (PC->WasInputKeyJustPressed(EKeys::Down) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Down))
                TitleSelection = (TitleSelection + 1) % 2;

            // Mouse Coordinates Hover
            float MouseX, MouseY;
            if (PC->GetMousePosition(MouseX, MouseY))
            {
                float BaseY = CY2 - 64.f;
                for (int32 i = 0; i < 2; ++i)
                {
                    if (MouseY >= BaseY && MouseY <= BaseY + 35.f && MouseX >= CX2 - 180.f && MouseX <= CX2 + 180.f)
                    {
                        if (i == 0 || bAnySaved) // prevent hovering Load if empty
                            TitleSelection = i;
                    }
                    BaseY += 50.f;
                }
            }
        }

        // ── Render Items with Selection Backdrops ───────────────────────
        float ItemY = CY2 - 60.f;
        for (int32 i = 0; i < 2; ++i)
        {
            const bool bSel = (i == TitleSelection);
            if (bSel)
            {
                // Highlight backing box
                DrawRect(FLinearColor(0.15f, 0.65f, 0.25f, 0.35f), CX2 - 180.f, ItemY - 4.f, 360.f, 35.f);
            }

            FLinearColor TextCol = bSel ? FLinearColor::Green : FLinearColor(Items[i].Col);
            DrawText(Items[i].Label, TextCol, CX2 - 160.f, ItemY, SmallFont, 1.4f);
            ItemY += 50.f;
        }

        DrawRect(FLinearColor(0.35f, 0.35f, 0.45f, 0.7f), CX2 - 220.f, ItemY + 4.f, 440.f, 1.f);
        DrawText(TEXT("[P] / [Start] Settings   [Arrows/DPad] Select"),
                 FLinearColor(0.55f, 0.8f, 1.f, 1.f), CX2 - 160.f, ItemY + 18.f, SmallFont, 1.0f);

        if (PC)
        {
            bool bConfirm = PC->WasInputKeyJustPressed(EKeys::Enter) ||
                            PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom) ||
                            PC->WasInputKeyJustPressed(EKeys::LeftMouseButton);

            if (bConfirm)
            {
                if (TitleSelection == 0) // Generate
                {
                    if (TitleWorld)
                    {
                        TitleWorld->GenerateWorld();
                        bShowTitleScreen = false;
                        bShowLoadingScreen = true;
                    }
                }
                else if (TitleSelection == 1 && bAnySaved) // Load
                {
                    if (TitleWorld)
                    {
                        int32 FirstSlot = 0;
                        for (int32 s = 0; s < 5; ++s) { if (bSlotExists[s]) { FirstSlot = s; break; } }

                        TitleWorld->ClearWorld();
                        TitleWorld->LoadFromFile(DefaultSlotNames[FirstSlot]);
                        TitleWorld->GenerateWorldDeferred();
                        bShowTitleScreen = false;
                        PC->SetShowMouseCursor(false);
                        FInputModeGameOnly GM; PC->SetInputMode(GM);
                    }
                }
            }
        }
        return;
    }

    // ── Crosshair ──────────────────────────────────────────────────────────
    const float CX = Canvas->SizeX * 0.5f;
    const float CY = Canvas->SizeY * 0.5f;
    DrawRect(FLinearColor::Green, CX - 10.f, CY - 1.f,  20.f, 2.f);
    DrawRect(FLinearColor::Green, CX -  1.f, CY - 10.f,  2.f, 20.f);

    // ── Read character state ───────────────────────────────────────────────
    float BrushRad = 300.f;
    bool  bGamepad = false;

    if (APawn* Pawn = GetOwningPawn())
    {
        if (AFirstVoxelCharacter* Char = Cast<AFirstVoxelCharacter>(Pawn))
        {
            BrushRad = Char->InteractionRadius;
            bGamepad = Char->bLastInputWasGamepad;
        }
    }

    // ── Layout constants ──────────────────────────────────────────────────
    const float X     = 20.f;
    float       Y     = 20.f;
    const float LineH = 19.f;

    const FColor ColTitle  (255, 220,  50, 255);  // gold
    const FColor ColBadgeKB(120, 200, 255, 255);  // light blue  – keyboard key
    const FColor ColBadgeGP(255, 150,  80, 255);  // orange      – gamepad button
    const FColor ColAction (220, 220, 220, 255);  // near-white
    const FColor ColBrush  ( 80, 255,  80, 255);  // green        – brush size

    // ── Title row ──────────────────────────────────────────────────────────
    const FString DeviceTag = bGamepad ? TEXT(" [Gamepad]") : TEXT(" [KB+Mouse]");
    DrawText(FString(TEXT("=== CONTROLS ===")) + DeviceTag,
             FLinearColor(ColTitle), X, Y, SmallFont);
    Y += LineH + 4.f;

    // ── Control rows ──────────────────────────────────────────────────────
    const FColor& BC = bGamepad ? ColBadgeGP : ColBadgeKB;

    if (bGamepad)
    {
        // Default voxel character hints
        DrawRow(this, X, Y, LineH, TEXT("[RT]"),           BC, TEXT(": Dig"),              ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[LT]"),           BC, TEXT(": Build"),            ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[RB] / [LB]"),    BC, TEXT(": Brush Radius +/-"), ColAction, SmallFont);
        DrawText(FString::Printf(TEXT("Brush Size : [%.f]"), BrushRad), FLinearColor(ColBrush), X, Y, SmallFont); Y += LineH + 6.f;
        DrawRow(this, X, Y, LineH, TEXT("[B]"),            BC, TEXT(": Toggle Flight"),    ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[A]"),            BC, TEXT(": Jump / Fly Up"),    ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[X]"),            BC, TEXT(": Fly Down"),         ColAction, SmallFont);

        DrawRow(this, X, Y, LineH, TEXT("[Y]"),             BC, TEXT(": Map"),              ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Start]"),          BC, TEXT(": Pause"),            ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Left Stick]"),   BC, TEXT(": Move"),             ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Right Stick]"),  BC, TEXT(": Look"),             ColAction, SmallFont);
    }
    else
    {
        // Default voxel character hints
        DrawRow(this, X, Y, LineH, TEXT("[LMB]"),          BC, TEXT(": Dig (hold)"),       ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[RMB]"),          BC, TEXT(": Build (hold)"),     ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Scroll]"),       BC, TEXT(": Brush Radius +/-"), ColAction, SmallFont);
        DrawText(FString::Printf(TEXT("Brush Size : [%.f]"), BrushRad), FLinearColor(ColBrush), X, Y, SmallFont); Y += LineH + 6.f;
        DrawRow(this, X, Y, LineH, TEXT("[F]"),            BC, TEXT(": Toggle Flight"),    ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Space]"),        BC, TEXT(": Jump / Fly Up"),    ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Ctrl]"),         BC, TEXT(": Fly Down"),         ColAction, SmallFont);

        DrawRow(this, X, Y, LineH, TEXT("[M]"),            BC, TEXT(": Map"),              ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[P]"),            BC, TEXT(": Pause"),            ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[WASD]"),         BC, TEXT(": Move"),             ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Mouse]"),        BC, TEXT(": Look"),             ColAction, SmallFont);
    }

    // ── Tool Wheel Overlay ────────────────────────────────────────────────
    AFirstVoxelCharacter* Char = Cast<AFirstVoxelCharacter>(GetOwningPawn());
    if (Char && Char->bToolWheelOpen)
    {
        // 1. Dim background
        DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);

        const float CenterX = Canvas->SizeX * 0.5f;
        const float CenterY = Canvas->SizeY * 0.5f;
        const float Radius  = 160.f;

        // 2. Labels mapped directly to EVoxelToolMode index
        // Order: 0/Dig, 1/Build, 2/Smooth, 3/Flatten
        FString ToolNames[] = { TEXT("DIG"), TEXT("BUILD"), TEXT("SMOOTH"), TEXT("FLATTEN") };
        FVector2D Offsets[] = {
            FVector2D(-Radius, 0.f), // 0: Dig (Left)
            FVector2D(0.f, -Radius), // 1: Build (Top)
            FVector2D(Radius, 0.f),  // 2: Smooth (Right)
            FVector2D(0.f, Radius)   // 3: Flatten (Bottom)
        };

        const int32 SelectedIdx = static_cast<int32>(Char->CurrentTool);

        for (int32 i = 0; i < 4; ++i)
        {
            const bool bSel = (i == SelectedIdx);
            FLinearColor TextCol = bSel ? FLinearColor::Green : FLinearColor::White;
            
            float TextW, TextH;
            GetTextSize(ToolNames[i], TextW, TextH, SmallFont);

            const float DrawX = CenterX + Offsets[i].X - (TextW * 0.5f);
            const float DrawY = CenterY + Offsets[i].Y - (TextH * 0.5f);

            if (bSel)
            {
                // Simple highlight box
                DrawRect(FLinearColor(0.2f, 0.8f, 0.2f, 0.4f), 
                         DrawX - 12.f, DrawY - 6.f, TextW + 24.f, TextH + 12.f);
            }

            DrawText(ToolNames[i], TextCol, DrawX, DrawY, SmallFont);
        }

        // Draw central pointer
        DrawRect(FLinearColor::White, CenterX - 3.f, CenterY - 3.f, 6.f, 6.f);

        if (!Char->bLastInputWasGamepad)
        {
            float MouseX, MouseY;
            APlayerController* PC = GetOwningPlayerController();
            if (PC && PC->GetMousePosition(MouseX, MouseY))
            {
                DrawLine(CenterX, CenterY, MouseX, MouseY, FLinearColor::Yellow, 2.f);
            }
        }
    }
}
