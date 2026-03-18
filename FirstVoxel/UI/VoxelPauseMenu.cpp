// =============================================================================
// VoxelPauseMenu.cpp
// =============================================================================
//
// Implements the pause menu drawn via the HUD canvas.
// All input is polled each Draw() call so no Input Action assets are needed.
//
// Save / Load delegate directly to AVoxelWorld::SaveToFile / LoadFromFile,
// then call GenerateWorldDeferred() on load so the terrain rebuilds
// with the loaded modifications applied.
// =============================================================================

#include "UI/VoxelPauseMenu.h"
#include "FirstVoxelHUD.h"
#include "Voxel/Core/World/VoxelWorld.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Canvas.h"
#include "Engine/GameViewportClient.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

// ============================================================
//  Layout constants
// ============================================================
namespace PauseLayout
{
    static constexpr float PanelW    = 480.f;  // width of the centre panel
    static constexpr float ItemH     = 44.f;   // height per menu item row
    static constexpr float TitleSize = 2.0f;   // scale for title text
    static constexpr float ItemSize  = 1.4f;   // scale for normal items
    static constexpr float HintSize  = 1.0f;   // scale for hint / info text

    // Colours
    static const FLinearColor ColBG      (0.f, 0.f, 0.f, 0.82f);
    static const FLinearColor ColPanel   (0.08f, 0.08f, 0.10f, 0.94f);
    static const FLinearColor ColBorder  (0.35f, 0.35f, 0.45f, 1.f);
    static const FLinearColor ColTitle   (1.f, 0.85f, 0.25f, 1.f);   // gold
    static const FLinearColor ColSel     (0.25f, 0.9f, 0.45f, 1.f);  // green selected
    static const FLinearColor ColNormal  (0.88f, 0.88f, 0.88f, 1.f); // near-white
    static const FLinearColor ColDimmed  (0.45f, 0.45f, 0.45f, 1.f); // unavailable
    static const FLinearColor ColDivider (0.30f, 0.30f, 0.40f, 0.7f);
    static const FLinearColor ColHint    (0.55f, 0.80f, 1.f, 1.f);   // light-blue hint
    static const FLinearColor ColDanger  (1.f, 0.30f, 0.30f, 1.f);   // red confirm
    static const FLinearColor ColSaved   (0.40f, 1.f, 0.60f, 1.f);   // green "saved" badge
}

// ============================================================
//  Init
// ============================================================
void UVoxelPauseMenu::Init(AHUD* InHUD)
{
    OwnerHUD = InHUD;
}

// ============================================================
//  Open / Close
// ============================================================
void UVoxelPauseMenu::Open()
{
    if (bOpen) return;
    bOpen         = true;
    ActivePanel   = EPauseMenuPanel::Main;
    MainSelection = 0;
    SlotSelection = 0;

    // We do NOT call SetGamePaused here.
    // SetGamePaused(true) freezes the UE input system, which makes
    // WasInputKeyJustPressed return false for every key while paused —
    // so menu navigation would be completely dead.
    // Instead we freeze the player pawn directly (disable movement + hide)
    // while keeping the game clock running so the HUD can draw and poll input.
    if (APlayerController* PC = OwnerHUD ? OwnerHUD->GetOwningPlayerController() : nullptr)
    {
        // Freeze pawn movement without pausing the engine
        if (APawn* P = PC->GetPawn())
        {
            if (ACharacter* Ch = Cast<ACharacter>(P))
            {
                if (UCharacterMovementComponent* CMC = Ch->GetCharacterMovement())
                    CMC->DisableMovement();
            }
        }

        // Switch to UI-only input so gameplay actions (dig, jump, fly) are
        // suppressed, but key-press polling (WasInputKeyJustPressed) still works.
        FInputModeUIOnly UIMode;
        PC->SetInputMode(UIMode);
        PC->SetShowMouseCursor(true);
        PC->FlushPressedKeys(); // clear stale held keys (e.g. W still held on P press)
    }
}

void UVoxelPauseMenu::Close()
{
    if (!bOpen) return;
    bOpen = false;

    if (APlayerController* PC = OwnerHUD ? OwnerHUD->GetOwningPlayerController() : nullptr)
    {
        // Restore movement. We re-enable via MOVE_Falling + bJustTeleported so
        // UE's ProcessLanded chain runs and resets animation state correctly.
        // SetMovementMode(MOVE_Walking) alone doesn't probe the floor.
        if (APawn* P = PC->GetPawn())
        {
            if (ACharacter* Ch = Cast<ACharacter>(P))
            {
                if (UCharacterMovementComponent* CMC = Ch->GetCharacterMovement())
                {
                    CMC->Velocity        = FVector::ZeroVector;
                    CMC->SetMovementMode(MOVE_Falling);
                    CMC->bJustTeleported = true;
                }
            }
        }

        FInputModeGameOnly GameMode;
        PC->SetInputMode(GameMode);
        PC->SetShowMouseCursor(false);
        PC->FlushPressedKeys();
    }
}

// ============================================================
//  Draw — called every frame from AFirstVoxelHUD::DrawHUD()
// ============================================================
void UVoxelPauseMenu::Draw()
{
    AFirstVoxelHUD* MyHUD = Cast<AFirstVoxelHUD>(OwnerHUD);
    if (!bOpen || !MyHUD || !MyHUD->GetCanvas()) return;

    CX = MyHUD->GetCanvas()->SizeX * 0.5f;
    CY = MyHUD->GetCanvas()->SizeY * 0.5f;

    DrawBackground();

    switch (ActivePanel)
    {
    case EPauseMenuPanel::Main:     DrawMain();            break;
    case EPauseMenuPanel::SaveSlot: DrawSlotPanel(true);   break;
    case EPauseMenuPanel::LoadSlot: DrawSlotPanel(false);  break;
    case EPauseMenuPanel::Confirm:  DrawConfirm();         break;
    }
}

// ============================================================
//  MAIN PANEL
// ============================================================
void UVoxelPauseMenu::DrawMain()
{
    // Title
    DrawCenteredText(TEXT("P A U S E D"), CY - 110.f, PauseLayout::ColTitle, PauseLayout::TitleSize);
    DrawDivider(CY - 70.f);

    // Items
    const FString Items[MainItemCount] = {
        TEXT("Resume"),
        TEXT("Save World"),
        TEXT("Load World"),
        TEXT("Return to Title")
    };

    float Y = CY - 40.f;
    for (int32 i = 0; i < MainItemCount; ++i)
    {
        const bool bSel = (i == MainSelection);
        FLinearColor Col = bSel ? PauseLayout::ColSel : PauseLayout::ColNormal;
        // "Return to Title" uses danger colour when selected
        if (i == 3 && bSel) Col = PauseLayout::ColDanger;
        DrawMenuItem(Items[i], Y, bSel);
        Y += PauseLayout::ItemH;
    }

    DrawDivider(Y + 4.f);

    // Hint line
    DrawCenteredText(TEXT("[↑↓ / DPad]  Navigate    [Enter / A]  Select    [Esc / B]  Resume"),
                     Y + 16.f, PauseLayout::ColHint, PauseLayout::HintSize);

    HandleMainInput();
}

void UVoxelPauseMenu::HandleMainInput()
{
    APlayerController* PC = OwnerHUD ? OwnerHUD->GetOwningPlayerController() : nullptr;
    if (!PC) return;

    // Navigation
    if (JustPressed(EKeys::Up)            || JustPressed(EKeys::Gamepad_DPad_Up))
        MainSelection = (MainSelection - 1 + MainItemCount) % MainItemCount;
    if (JustPressed(EKeys::Down)          || JustPressed(EKeys::Gamepad_DPad_Down))
        MainSelection = (MainSelection + 1) % MainItemCount;

    // Confirm
    if (JustPressed(EKeys::Enter)         || JustPressed(EKeys::Gamepad_FaceButton_Bottom))
    {
        switch (MainSelection)
        {
        case 0: Close(); break;                           // Resume
        case 1: ActivePanel = EPauseMenuPanel::SaveSlot; SlotSelection = 0; break;
        case 2: ActivePanel = EPauseMenuPanel::LoadSlot; SlotSelection = 0; break;
        case 3: ActivePanel = EPauseMenuPanel::Confirm;  break;
        }
    }

    // Close / back
    if (JustPressed(EKeys::Escape)        ||
        JustPressed(EKeys::Gamepad_FaceButton_Right) ||
        JustPressed(EKeys::Gamepad_Special_Right))   // Start toggles pause off
        Close();
}

// ============================================================
//  SLOT PANEL  (shared for Save and Load)
// ============================================================
void UVoxelPauseMenu::DrawSlotPanel(bool bIsSave)
{
    const FString Title = bIsSave ? TEXT("SAVE WORLD") : TEXT("LOAD WORLD");
    DrawCenteredText(Title, CY - 130.f, PauseLayout::ColTitle, PauseLayout::TitleSize);
    DrawDivider(CY - 90.f);

    float Y = CY - 70.f;
    for (int32 i = 0; i < MaxSaveSlots; ++i)
    {
        const bool bExists = SlotExists(i);
        const bool bSel    = (i == SlotSelection);

        // Build label — append "[SAVED]" badge if slot has data
        FString Label = DefaultSlotNames[i];
        if (bExists) Label += TEXT("   [SAVED]");

        // Dimmed on Load panel when slot is empty (can't load nothing)
        const bool bDimmed = !bIsSave ? !bExists : false;
        DrawMenuItem(Label, Y, bSel, bDimmed);
        Y += PauseLayout::ItemH;
    }

    DrawDivider(Y + 4.f);
    DrawCenteredText(TEXT("[↑↓]  Select    [Enter / A]  Confirm    [Esc / B]  Back"),
                     Y + 16.f, PauseLayout::ColHint, PauseLayout::HintSize);

    HandleSlotInput(bIsSave);
}

void UVoxelPauseMenu::HandleSlotInput(bool bIsSave)
{
    if (!OwnerHUD) return;

    if (JustPressed(EKeys::Up)   || JustPressed(EKeys::Gamepad_DPad_Up))
        SlotSelection = (SlotSelection - 1 + MaxSaveSlots) % MaxSaveSlots;
    if (JustPressed(EKeys::Down) || JustPressed(EKeys::Gamepad_DPad_Down))
        SlotSelection = (SlotSelection + 1) % MaxSaveSlots;

    // Back
    if (JustPressed(EKeys::Escape) || JustPressed(EKeys::Gamepad_FaceButton_Right))
    {
        ActivePanel = EPauseMenuPanel::Main;
        return;
    }

    // Confirm
    if (JustPressed(EKeys::Enter) || JustPressed(EKeys::Gamepad_FaceButton_Bottom))
    {
        AVoxelWorld* World = FindVoxelWorld();
        if (!World) return;

        const FString SlotName = DefaultSlotNames[SlotSelection];

        if (bIsSave)
        {
            // Save terrain modifications + seed to the chosen slot
            World->SaveToFile(SlotName);
            // Brief feedback — show a screen message
            if (GEngine)
                GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green,
                    FString::Printf(TEXT("World saved to '%s'"), *SlotName));
            ActivePanel = EPauseMenuPanel::Main;
        }
        else
        {
            // Only load if the slot file actually exists
            if (!SlotExists(SlotSelection)) return;

            World->ClearWorld();
            World->LoadFromFile(SlotName);
            World->GenerateWorldDeferred();

            if (GEngine)
                GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan,
                    FString::Printf(TEXT("World loaded from '%s'"), *SlotName));

            // Resume automatically after loading
            Close();
        }
    }
}

// ============================================================
//  CONFIRM PANEL  (Return to Title)
// ============================================================
void UVoxelPauseMenu::DrawConfirm()
{
    DrawCenteredText(TEXT("RETURN TO TITLE?"), CY - 80.f, PauseLayout::ColDanger, PauseLayout::TitleSize);
    DrawDivider(CY - 40.f);
    DrawCenteredText(TEXT("All unsaved progress will be lost."), CY - 20.f, PauseLayout::ColNormal, PauseLayout::HintSize);

    DrawMenuItem(TEXT("Yes — Return to Title"), CY + 20.f,  MainSelection == 0);
    DrawMenuItem(TEXT("No  — Stay in Game"),    CY + 64.f,  MainSelection == 1);

    DrawDivider(CY + 108.f);
    DrawCenteredText(TEXT("[↑↓]  Select    [Enter / A]  Confirm    [Esc / B]  Back"),
                     CY + 122.f, PauseLayout::ColHint, PauseLayout::HintSize);

    HandleConfirmInput();
}

void UVoxelPauseMenu::HandleConfirmInput()
{
    if (!OwnerHUD) return;

    if (JustPressed(EKeys::Up)   || JustPressed(EKeys::Gamepad_DPad_Up))
        MainSelection = (MainSelection == 0) ? 1 : 0;
    if (JustPressed(EKeys::Down) || JustPressed(EKeys::Gamepad_DPad_Down))
        MainSelection = (MainSelection == 0) ? 1 : 0;

    // Back
    if (JustPressed(EKeys::Escape) || JustPressed(EKeys::Gamepad_FaceButton_Right))
    {
        ActivePanel   = EPauseMenuPanel::Main;
        MainSelection = 3; // stay on "Return to Title" in main menu
        return;
    }

    if (JustPressed(EKeys::Enter) || JustPressed(EKeys::Gamepad_FaceButton_Bottom))
    {
        if (MainSelection == 0) // Yes
        {
            Close();
            // Show the title screen via the HUD
            if (AFirstVoxelHUD* FVHUD = Cast<AFirstVoxelHUD>(OwnerHUD))
                FVHUD->bShowTitleScreen = true;
        }
        else // No
        {
            ActivePanel   = EPauseMenuPanel::Main;
            MainSelection = 0;
        }
    }
}

// ============================================================
//  Helpers
// ============================================================
AVoxelWorld* UVoxelPauseMenu::FindVoxelWorld() const
{
    if (!OwnerHUD || !OwnerHUD->GetWorld()) return nullptr;
    for (TActorIterator<AVoxelWorld> It(OwnerHUD->GetWorld()); It; ++It)
        return *It;
    return nullptr;
}

UFont* UVoxelPauseMenu::GetFont() const
{
    return GEngine ? GEngine->GetSmallFont() : nullptr;
}

FString UVoxelPauseMenu::SlotFileName(int32 SlotIdx) const
{
    if (!OwnerHUD || !OwnerHUD->GetWorld()) return TEXT("");
    AVoxelWorld* W = FindVoxelWorld();
    const FString WorldName = W ? W->GetName() : TEXT("VoxelWorld");
    const FString SlotName  = DefaultSlotNames[SlotIdx];
    return FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("VoxelSaves"),
        FString::Printf(TEXT("%s_%s.sav"), *WorldName, *SlotName));
}

bool UVoxelPauseMenu::SlotExists(int32 SlotIdx) const
{
    const FString Path = SlotFileName(SlotIdx);
    return !Path.IsEmpty() && FPaths::FileExists(Path);
}

bool UVoxelPauseMenu::JustPressed(const FKey Key) const
{
    APlayerController* PC = OwnerHUD ? OwnerHUD->GetOwningPlayerController() : nullptr;
    // WasInputKeyJustPressed works even in FInputModeUIOnly because we never
    // call SetGamePaused — the engine tick and input tick both keep running.
    return PC && PC->WasInputKeyJustPressed(Key);
}

// ============================================================
//  Drawing primitives
// ============================================================
void UVoxelPauseMenu::DrawBackground() const
{
    AFirstVoxelHUD* MyHUD = Cast<AFirstVoxelHUD>(OwnerHUD);
    if (!MyHUD || !MyHUD->GetCanvas()) return;
    const float W = MyHUD->GetCanvas()->SizeX;
    const float H = MyHUD->GetCanvas()->SizeY;

    // Full-screen dark overlay
    OwnerHUD->DrawRect(PauseLayout::ColBG, 0.f, 0.f, W, H);

    // Centre panel background
    const float PH = 360.f;
    const float PX = CX - PauseLayout::PanelW * 0.5f;
    const float PY = CY - PH * 0.5f;
    OwnerHUD->DrawRect(PauseLayout::ColPanel,  PX,       PY,       PauseLayout::PanelW, PH);
    OwnerHUD->DrawRect(PauseLayout::ColBorder, PX,       PY,       PauseLayout::PanelW, 2.f);
    OwnerHUD->DrawRect(PauseLayout::ColBorder, PX,       PY + PH - 2.f, PauseLayout::PanelW, 2.f);
    OwnerHUD->DrawRect(PauseLayout::ColBorder, PX,       PY,       2.f, PH);
    OwnerHUD->DrawRect(PauseLayout::ColBorder, PX + PauseLayout::PanelW - 2.f, PY, 2.f, PH);
}

void UVoxelPauseMenu::DrawCenteredText(const FString& Text, float Y, FLinearColor Color, float Scale) const
{
    if (!OwnerHUD) return;
    UFont* Font = GetFont();
    if (!Font) return;

    float TW, TH;
    OwnerHUD->GetTextSize(Text, TW, TH, Font, Scale);
    OwnerHUD->DrawText(Text, Color, CX - TW * 0.5f, Y, Font, Scale);
}

void UVoxelPauseMenu::DrawMenuItem(const FString& Label, float Y, bool bSelected, bool bDimmed) const
{
    if (!OwnerHUD) return;
    UFont* Font = GetFont();
    if (!Font) return;

    FLinearColor Col = bDimmed  ? PauseLayout::ColDimmed  :
                       bSelected ? PauseLayout::ColSel    :
                                   PauseLayout::ColNormal;

    float TW, TH;
    OwnerHUD->GetTextSize(Label, TW, TH, Font, PauseLayout::ItemSize);

    const float IX = CX - TW * 0.5f;

    // Highlight box for selected item
    if (bSelected && !bDimmed)
    {
        const float Pad = 14.f;
        OwnerHUD->DrawRect(FLinearColor(0.15f, 0.65f, 0.25f, 0.35f),
                           IX - Pad, Y - 4.f, TW + Pad * 2.f, TH + 8.f);
    }

    OwnerHUD->DrawText(Label, Col, IX, Y, Font, PauseLayout::ItemSize);
}

void UVoxelPauseMenu::DrawDivider(float Y) const
{
    if (!OwnerHUD) return;
    const float X = CX - PauseLayout::PanelW * 0.5f + 16.f;
    OwnerHUD->DrawRect(PauseLayout::ColDivider, X, Y, PauseLayout::PanelW - 32.f, 1.f);
}
