// FirstVoxelHUD.cpp
// Draws crosshair + context-aware control labels.
// Automatically switches between Keyboard/Mouse and Gamepad hints
// based on AFirstVoxelCharacter::bLastInputWasGamepad.
#include "FirstVoxelHUD.h"
#include "Engine/Canvas.h"
#include "FirstVoxelCharacter.h"
// #include "Variant_Combat/CombatCharacter.h"
// #include "Variant_SideScrolling/SideScrollingCharacter.h"
// #include "Variant_Platforming/PlatformingCharacter.h"


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

void AFirstVoxelHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;

    // Cache the font pointer once per frame rather than calling GetSmallFont() on every DrawRow.
    // GEngine->GetSmallFont() allocates a new descriptor each call which adds up at 60 fps.
    UFont* const SmallFont = GEngine ? GEngine->GetSmallFont() : nullptr;
    if (!SmallFont) return;

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

        DrawRow(this, X, Y, LineH, TEXT("[Y] / Start"),    BC, TEXT(": Map"),              ColAction, SmallFont);
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
