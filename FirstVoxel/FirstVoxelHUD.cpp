// FirstVoxelHUD.cpp
// Draws crosshair + context-aware control labels.
// Automatically switches between Keyboard/Mouse and Gamepad hints
// based on AFirstVoxelCharacter::bLastInputWasGamepad.
#include "FirstVoxelHUD.h"
#include "Engine/Canvas.h"
#include "FirstVoxelCharacter.h"
#include "Variant_Combat/CombatCharacter.h"
#include "Variant_SideScrolling/SideScrollingCharacter.h"
#include "Variant_Platforming/PlatformingCharacter.h"


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

    // [Expert Refactor] Adaptive Variant Character Hint Lists
    ACombatCharacter* CombatChar = nullptr;
    ASideScrollingCharacter* SideChar = nullptr;
    APlatformingCharacter* PlatChar = nullptr;

    if (APawn* Pawn = GetOwningPawn())
    {
        CombatChar = Cast<ACombatCharacter>(Pawn);
        SideChar = Cast<ASideScrollingCharacter>(Pawn);
        PlatChar = Cast<APlatformingCharacter>(Pawn);
    }

    if (bGamepad)
    {
        if (CombatChar)
        {
            DrawRow(this, X, Y, LineH, TEXT("[RT]"),           BC, TEXT(": Combo Attack"),     ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[LT]"),           BC, TEXT(": Charged Attack"),   ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[B]"),            BC, TEXT(": Toggle Camera"),    ColAction, SmallFont);
        }
        else if (SideChar)
        {
            DrawRow(this, X, Y, LineH, TEXT("[A]"),            BC, TEXT(": Jump / Wall Jump"), ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[B]"),            BC, TEXT(": Drop from Platform"), ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[X]"),            BC, TEXT(": Interact"),         ColAction, SmallFont);
        }
        else if (PlatChar)
        {
            DrawRow(this, X, Y, LineH, TEXT("[RT]"),           BC, TEXT(": Dash Action"),      ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[A]"),            BC, TEXT(": Multi-Jump / Wall"), ColAction, SmallFont);
        }
        else // Default voxel character hints
        {
            DrawRow(this, X, Y, LineH, TEXT("[RT]"),           BC, TEXT(": Dig"),              ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[LT]"),           BC, TEXT(": Build"),            ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[RB] / [LB]"),    BC, TEXT(": Brush Radius +/-"), ColAction, SmallFont);
            DrawText(FString::Printf(TEXT("Brush Size : [%.f]"), BrushRad), FLinearColor(ColBrush), X, Y, SmallFont); Y += LineH + 6.f;
            DrawRow(this, X, Y, LineH, TEXT("[B]"),            BC, TEXT(": Toggle Flight"),    ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[A]"),            BC, TEXT(": Jump / Fly Up"),    ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[X]"),            BC, TEXT(": Fly Down"),         ColAction, SmallFont);
        }

        DrawRow(this, X, Y, LineH, TEXT("[Y] / Start"),    BC, TEXT(": Map"),              ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Left Stick]"),   BC, TEXT(": Move"),             ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Right Stick]"),  BC, TEXT(": Look"),             ColAction, SmallFont);
    }
    else
    {
        if (CombatChar)
        {
            DrawRow(this, X, Y, LineH, TEXT("[LMB]"),          BC, TEXT(": Combo Attack"),     ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[RMB]"),          BC, TEXT(": Charged Attack"),   ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[F]"),            BC, TEXT(": Toggle Camera"),    ColAction, SmallFont);
        }
        else if (SideChar)
        {
            DrawRow(this, X, Y, LineH, TEXT("[Space]"),        BC, TEXT(": Jump / Wall Jump"), ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[Ctrl]"),         BC, TEXT(": Drop from Platform"), ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[E]"),            BC, TEXT(": Interact"),         ColAction, SmallFont);
        }
        else if (PlatChar)
        {
            DrawRow(this, X, Y, LineH, TEXT("[Shift]"),        BC, TEXT(": Dash"),             ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[Space]"),        BC, TEXT(": Multi-Jump / Wall"), ColAction, SmallFont);
        }
        else
        {
            DrawRow(this, X, Y, LineH, TEXT("[LMB]"),          BC, TEXT(": Dig (hold)"),       ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[RMB]"),          BC, TEXT(": Build (hold)"),     ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[Scroll]"),       BC, TEXT(": Brush Radius +/-"), ColAction, SmallFont);
            DrawText(FString::Printf(TEXT("Brush Size : [%.f]"), BrushRad), FLinearColor(ColBrush), X, Y, SmallFont); Y += LineH + 6.f;
            DrawRow(this, X, Y, LineH, TEXT("[F]"),            BC, TEXT(": Toggle Flight"),    ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[Space]"),        BC, TEXT(": Jump / Fly Up"),    ColAction, SmallFont);
            DrawRow(this, X, Y, LineH, TEXT("[Ctrl]"),         BC, TEXT(": Fly Down"),         ColAction, SmallFont);
        }

        DrawRow(this, X, Y, LineH, TEXT("[M]"),            BC, TEXT(": Map"),              ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[WASD]"),         BC, TEXT(": Move"),             ColAction, SmallFont);
        DrawRow(this, X, Y, LineH, TEXT("[Mouse]"),        BC, TEXT(": Look"),             ColAction, SmallFont);
    }
}
