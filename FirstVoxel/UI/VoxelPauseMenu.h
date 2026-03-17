// =============================================================================
// VoxelPauseMenu.h
// =============================================================================
//
// Full-screen pause menu drawn entirely in C++ via the canvas HUD system.
// Opened with P (keyboard) or Start/Options (gamepad) during gameplay.
// Also provides Save World / Load World functionality using named save slots.
//
// -- MENU STRUCTURE -----------------------------------------------------------
//
//   PAUSED
//   ─────────────────────────────
//   [Resume]
//   [Save World]      → opens slot name panel
//   [Load World]      → opens slot select panel
//   [Return to Title]
//   ─────────────────────────────
//
// -- SAVE SLOT SYSTEM ---------------------------------------------------------
//
//  Up to MaxSaveSlots named slots are supported.  Each slot stores:
//    - The terrain modification DataMap (player digs/builds)
//    - The generation seed (so the same world can be regenerated on load)
//  Save data lives in  Saved/VoxelSaves/<WorldName>_<SlotName>.sav
//
// -- USAGE FROM HUD -----------------------------------------------------------
//
//   AFirstVoxelHUD creates one UVoxelPauseMenu and calls:
//     PauseMenu->Open()    when P / Start is pressed
//     PauseMenu->Close()   on Resume or Return to Title
//     PauseMenu->Draw()    inside DrawHUD() every frame while open
//
// -- INPUT -------------------------------------------------------------------
//
//  All navigation is polled inside Draw() via APlayerController::WasInputKeyJustPressed
//  so no additional Input Action assets are required.
//  Keyboard: Arrow Up/Down to navigate, Enter to confirm, Escape to close.
//  Gamepad:  DPad Up/Down, Face A to confirm, Face B / Start to close.
// =============================================================================
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Misc/Paths.h"
#include "InputCoreTypes.h"   // FKey
#include "VoxelPauseMenu.generated.h"

class AVoxelWorld;
class AHUD;
class UFont;

// Max named save slots shown in the UI
static constexpr int32 MaxSaveSlots = 5;

// Pre-defined slot names shown in the menu
static const FString DefaultSlotNames[MaxSaveSlots] = {
    TEXT("Slot 1"),
    TEXT("Slot 2"),
    TEXT("Slot 3"),
    TEXT("Slot 4"),
    TEXT("Slot 5")
};

// Which sub-panel is active
UENUM()
enum class EPauseMenuPanel : uint8
{
    Main,      // top-level: Resume / Save / Load / Title
    SaveSlot,  // slot picker for saving
    LoadSlot,  // slot picker for loading
    Confirm    // "Are you sure?" dialog (Return to Title)
};

UCLASS()
class FIRSTVOXEL_API UVoxelPauseMenu : public UObject
{
    GENERATED_BODY()

public:
    // ---- Lifecycle ----

    /** Initialise with the owning HUD.  Call once after construction. */
    void Init(AHUD* InHUD);

    /** Show the pause menu and pause the game. */
    void Open();

    /** Hide the pause menu and unpause the game. */
    void Close();

    bool IsOpen() const { return bOpen; }

    /**
     * Draw the menu.  Call from AFirstVoxelHUD::DrawHUD() every frame.
     * Handles its own input polling so no separate Tick is needed.
     */
    void Draw();

private:
    // ---- State ----
    bool              bOpen        = false;
    EPauseMenuPanel   ActivePanel  = EPauseMenuPanel::Main;

    // Main menu: 0=Resume  1=Save  2=Load  3=Title
    int32 MainSelection  = 0;
    static constexpr int32 MainItemCount = 4;

    // Slot panels
    int32 SlotSelection  = 0;   // which slot is highlighted in Save/Load sub-panel

    // ---- Refs ----
    UPROPERTY()
    TObjectPtr<AHUD> OwnerHUD = nullptr;

    // ---- Helpers ----
    AVoxelWorld* FindVoxelWorld() const;
    UFont*       GetFont()        const;

    // ---- Per-panel draw + input ----
    void DrawMain();
    void DrawSlotPanel(bool bIsSave);
    void DrawConfirm();

    // Input helpers (polled, not event-driven)
    bool JustPressed(const FKey Key) const;
    void HandleMainInput();
    void HandleSlotInput(bool bIsSave);
    void HandleConfirmInput();

    // Slot helpers
    FString SlotFileName(int32 SlotIdx) const;
    bool    SlotExists  (int32 SlotIdx) const;

    // Drawing primitives (thin wrappers around AHUD canvas calls)
    void DrawBackground()                                  const;
    void DrawCenteredText(const FString& Text, float Y,
                          FLinearColor Color, float Scale = 1.f) const;
    void DrawMenuItem(const FString& Label, float Y,
                      bool bSelected, bool bDimmed = false) const;
    void DrawDivider(float Y)                              const;

    // Layout constants (computed from canvas size in Draw())
    mutable float CX = 0.f;   // canvas centre X
    mutable float CY = 0.f;   // canvas centre Y
};
