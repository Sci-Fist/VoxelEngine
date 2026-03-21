// VoxelPauseMenu.cpp
//
// FIX N19 — FindVoxelWorld() now uses CachedVoxelWorld for O(1) lookup.
//            Was running TActorIterator every Draw() call (60 Hz scan of all actors).
//
// FIX N20 — Close() now restores MOVE_Walking + UpdateFloorFromAdjustment.
//            Old code used MOVE_Falling + bJustTeleported which leaves the
//            character in falling animation state until ProcessLanded fires
//            (can take several frames, especially on voxel terrain where the
//            floor probe fires late). MOVE_Walking + UpdateFloorFromAdjustment
//            immediately grounds the character.

#include "UI/VoxelPauseMenu.h"
#include "FirstVoxelHUD.h"
#include "Voxel/Core/World/VoxelWorld.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Canvas.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

namespace PauseLayout
{
    static constexpr float PanelW    = 480.f;
    static constexpr float ItemH     = 44.f;
    static constexpr float TitleSize = 2.0f;
    static constexpr float ItemSize  = 1.4f;
    static constexpr float HintSize  = 1.0f;
    static const FLinearColor ColBG     (0.f,   0.f,   0.f,   0.82f);
    static const FLinearColor ColPanel  (0.08f, 0.08f, 0.10f, 0.94f);
    static const FLinearColor ColBorder (0.35f, 0.35f, 0.45f, 1.f);
    static const FLinearColor ColTitle  (1.f,   0.85f, 0.25f, 1.f);
    static const FLinearColor ColSel    (0.25f, 0.9f,  0.45f, 1.f);
    static const FLinearColor ColNormal (0.88f, 0.88f, 0.88f, 1.f);
    static const FLinearColor ColDimmed (0.45f, 0.45f, 0.45f, 1.f);
    static const FLinearColor ColDivider(0.30f, 0.30f, 0.40f, 0.7f);
    static const FLinearColor ColHint   (0.55f, 0.80f, 1.f,   1.f);
    static const FLinearColor ColDanger (1.f,   0.30f, 0.30f, 1.f);
}

void UVoxelPauseMenu::Init(AHUD* InHUD)
{
    OwnerHUD = InHUD;
    CachedVoxelWorld = nullptr;
}

void UVoxelPauseMenu::Open()
{
    if (bOpen) return;
    bOpen = true; ActivePanel = EPauseMenuPanel::Main; MainSelection = 0; SlotSelection = 0;

    if (APlayerController* PC = OwnerHUD ? OwnerHUD->GetOwningPlayerController() : nullptr)
    {
        if (APawn* P = PC->GetPawn())
            if (ACharacter* Ch = Cast<ACharacter>(P))
                if (UCharacterMovementComponent* CMC = Ch->GetCharacterMovement())
                    CMC->DisableMovement();

        FInputModeUIOnly UIMode;
        PC->SetInputMode(UIMode);
        PC->SetShowMouseCursor(true);
        PC->FlushPressedKeys();
    }
}

void UVoxelPauseMenu::Close()
{
    if (!bOpen) return;
    bOpen = false;

    if (APlayerController* PC = OwnerHUD ? OwnerHUD->GetOwningPlayerController() : nullptr)
    {
        if (APawn* P = PC->GetPawn())
        {
            if (ACharacter* Ch = Cast<ACharacter>(P))
            {
                if (UCharacterMovementComponent* CMC = Ch->GetCharacterMovement())
                {
                    // FIX N20: MOVE_Walking + UpdateFloorFromAdjustment grounds the
                    // character immediately. The old MOVE_Falling path left the char
                    // in falling animation for several frames on voxel terrain.
                    CMC->Velocity = FVector::ZeroVector;
                    CMC->SetMovementMode(MOVE_Walking);
                    CMC->UpdateFloorFromAdjustment();
                    CMC->bJustTeleported = false;
                }
            }
        }
        PC->SetInputMode(FInputModeGameOnly());
        PC->SetShowMouseCursor(false);
        PC->FlushPressedKeys();
    }
}

void UVoxelPauseMenu::Draw()
{
    AFirstVoxelHUD* MyHUD = Cast<AFirstVoxelHUD>(OwnerHUD);
    if (!bOpen || !MyHUD || !MyHUD->GetCanvas()) return;
    CX = MyHUD->GetCanvas()->SizeX * 0.5f;
    CY = MyHUD->GetCanvas()->SizeY * 0.5f;
    DrawBackground();
    switch (ActivePanel)
    {
    case EPauseMenuPanel::Main:     DrawMain();           break;
    case EPauseMenuPanel::SaveSlot: DrawSlotPanel(true);  break;
    case EPauseMenuPanel::LoadSlot: DrawSlotPanel(false); break;
    case EPauseMenuPanel::Confirm:  DrawConfirm();        break;
    }
}

void UVoxelPauseMenu::DrawMain()
{
    DrawCenteredText(TEXT("P A U S E D"), CY-110.f, PauseLayout::ColTitle, PauseLayout::TitleSize);
    DrawDivider(CY-70.f);
    static const FString Items[MainItemCount] = {TEXT("Resume"),TEXT("Save World"),TEXT("Load World"),TEXT("Return to Title")};
    float Y = CY-40.f;
    for (int32 i=0;i<MainItemCount;++i)
    {
        const bool bSel=(i==MainSelection);
        DrawMenuItem(Items[i], Y, bSel);
        Y += PauseLayout::ItemH;
    }
    DrawDivider(Y+4.f);
    DrawCenteredText(TEXT("[↑↓]  Navigate    [Enter]  Select    [Esc]  Resume"), Y+16.f, PauseLayout::ColHint, PauseLayout::HintSize);
    HandleMainInput();
}

void UVoxelPauseMenu::HandleMainInput()
{
    if (!OwnerHUD) return;
    if (JustPressed(EKeys::Up)||JustPressed(EKeys::Gamepad_DPad_Up))     MainSelection=(MainSelection-1+MainItemCount)%MainItemCount;
    if (JustPressed(EKeys::Down)||JustPressed(EKeys::Gamepad_DPad_Down)) MainSelection=(MainSelection+1)%MainItemCount;
    if (JustPressed(EKeys::Enter)||JustPressed(EKeys::Gamepad_FaceButton_Bottom))
    {
        switch (MainSelection)
        {
        case 0: Close(); break;
        case 1: ActivePanel=EPauseMenuPanel::SaveSlot; SlotSelection=0; break;
        case 2: ActivePanel=EPauseMenuPanel::LoadSlot; SlotSelection=0; break;
        case 3: ActivePanel=EPauseMenuPanel::Confirm;  break;
        }
    }
    if (JustPressed(EKeys::Escape)||JustPressed(EKeys::Gamepad_FaceButton_Right)||JustPressed(EKeys::Gamepad_Special_Right))
        Close();
}

void UVoxelPauseMenu::DrawSlotPanel(bool bIsSave)
{
    DrawCenteredText(bIsSave?TEXT("SAVE WORLD"):TEXT("LOAD WORLD"), CY-130.f, PauseLayout::ColTitle, PauseLayout::TitleSize);
    DrawDivider(CY-90.f);
    float Y=CY-70.f;
    for (int32 i=0;i<MaxSaveSlots;++i)
    {
        const bool bEx=SlotExists(i), bSel=(i==SlotSelection);
        FString Label=DefaultSlotNames[i]; if (bEx) Label+=TEXT("   [SAVED]");
        DrawMenuItem(Label, Y, bSel, !bIsSave&&!bEx);
        Y+=PauseLayout::ItemH;
    }
    DrawDivider(Y+4.f);
    DrawCenteredText(TEXT("[↑↓]  Select    [Enter]  Confirm    [Esc]  Back"), Y+16.f, PauseLayout::ColHint, PauseLayout::HintSize);
    HandleSlotInput(bIsSave);
}

void UVoxelPauseMenu::HandleSlotInput(bool bIsSave)
{
    if (!OwnerHUD) return;
    if (JustPressed(EKeys::Up)||JustPressed(EKeys::Gamepad_DPad_Up))     SlotSelection=(SlotSelection-1+MaxSaveSlots)%MaxSaveSlots;
    if (JustPressed(EKeys::Down)||JustPressed(EKeys::Gamepad_DPad_Down)) SlotSelection=(SlotSelection+1)%MaxSaveSlots;
    if (JustPressed(EKeys::Escape)||JustPressed(EKeys::Gamepad_FaceButton_Right)) { ActivePanel=EPauseMenuPanel::Main; return; }
    if (JustPressed(EKeys::Enter)||JustPressed(EKeys::Gamepad_FaceButton_Bottom))
    {
        AVoxelWorld* World=FindVoxelWorld(); if (!World) return;
        const FString SlotName=DefaultSlotNames[SlotSelection];
        if (bIsSave)
        {
            World->SaveToFile(SlotName);
            if (GEngine) GEngine->AddOnScreenDebugMessage(-1,3.f,FColor::Green,FString::Printf(TEXT("Saved to '%s'"),*SlotName));
            ActivePanel=EPauseMenuPanel::Main;
        }
        else
        {
            if (!SlotExists(SlotSelection)) return;
            World->ClearWorld(); World->LoadFromFile(SlotName); World->GenerateWorldDeferred();
            if (GEngine) GEngine->AddOnScreenDebugMessage(-1,3.f,FColor::Cyan,FString::Printf(TEXT("Loaded '%s'"),*SlotName));
            Close();
        }
    }
}

void UVoxelPauseMenu::DrawConfirm()
{
    DrawCenteredText(TEXT("RETURN TO TITLE?"), CY-80.f, PauseLayout::ColDanger, PauseLayout::TitleSize);
    DrawDivider(CY-40.f);
    DrawCenteredText(TEXT("All unsaved progress will be lost."), CY-20.f, PauseLayout::ColNormal, PauseLayout::HintSize);
    DrawMenuItem(TEXT("Yes — Return to Title"), CY+20.f, MainSelection==0);
    DrawMenuItem(TEXT("No  — Stay in Game"),    CY+64.f, MainSelection==1);
    DrawDivider(CY+108.f);
    DrawCenteredText(TEXT("[↑↓]  Select    [Enter]  Confirm    [Esc]  Back"), CY+122.f, PauseLayout::ColHint, PauseLayout::HintSize);
    HandleConfirmInput();
}

void UVoxelPauseMenu::HandleConfirmInput()
{
    if (!OwnerHUD) return;
    if (JustPressed(EKeys::Up)||JustPressed(EKeys::Down)||JustPressed(EKeys::Gamepad_DPad_Up)||JustPressed(EKeys::Gamepad_DPad_Down))
        MainSelection=(MainSelection==0)?1:0;
    if (JustPressed(EKeys::Escape)||JustPressed(EKeys::Gamepad_FaceButton_Right))
    { ActivePanel=EPauseMenuPanel::Main; MainSelection=3; return; }
    if (JustPressed(EKeys::Enter)||JustPressed(EKeys::Gamepad_FaceButton_Bottom))
    {
        if (MainSelection==0)
        { Close(); if (AFirstVoxelHUD* H=Cast<AFirstVoxelHUD>(OwnerHUD)) H->bShowTitleScreen=true; }
        else { ActivePanel=EPauseMenuPanel::Main; MainSelection=0; }
    }
}

// FIX N19: O(1) cached lookup — no per-Draw TActorIterator scan
AVoxelWorld* UVoxelPauseMenu::FindVoxelWorld() const
{
    if (IsValid(CachedVoxelWorld)) return CachedVoxelWorld;
    if (!OwnerHUD || !OwnerHUD->GetWorld()) return nullptr;
    for (TActorIterator<AVoxelWorld> It(OwnerHUD->GetWorld()); It; ++It)
        if (*It) { CachedVoxelWorld = *It; return *It; }
    return nullptr;
}

UFont* UVoxelPauseMenu::GetFont() const { return GEngine ? GEngine->GetSmallFont() : nullptr; }

FString UVoxelPauseMenu::SlotFileName(int32 SlotIdx) const
{
    AVoxelWorld* W = FindVoxelWorld();
    const FString WN = W ? W->GetName() : TEXT("VoxelWorld");
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VoxelSaves"),
        FString::Printf(TEXT("%s_%s.sav"), *WN, *DefaultSlotNames[SlotIdx]));
}

bool UVoxelPauseMenu::SlotExists(int32 SlotIdx) const
{ const FString P=SlotFileName(SlotIdx); return !P.IsEmpty()&&FPaths::FileExists(P); }

bool UVoxelPauseMenu::JustPressed(const FKey Key) const
{ APlayerController* PC=OwnerHUD?OwnerHUD->GetOwningPlayerController():nullptr; return PC&&PC->WasInputKeyJustPressed(Key); }

void UVoxelPauseMenu::DrawBackground() const
{
    AFirstVoxelHUD* H=Cast<AFirstVoxelHUD>(OwnerHUD); if(!H||!H->GetCanvas()) return;
    const float W=H->GetCanvas()->SizeX, Ht=H->GetCanvas()->SizeY;
    OwnerHUD->DrawRect(PauseLayout::ColBG, 0,0,W,Ht);
    const float PH=360.f, PX=CX-PauseLayout::PanelW*0.5f, PY=CY-PH*0.5f;
    OwnerHUD->DrawRect(PauseLayout::ColPanel,  PX,PY,PauseLayout::PanelW,PH);
    OwnerHUD->DrawRect(PauseLayout::ColBorder, PX,PY,PauseLayout::PanelW,2.f);
    OwnerHUD->DrawRect(PauseLayout::ColBorder, PX,PY+PH-2.f,PauseLayout::PanelW,2.f);
    OwnerHUD->DrawRect(PauseLayout::ColBorder, PX,PY,2.f,PH);
    OwnerHUD->DrawRect(PauseLayout::ColBorder, PX+PauseLayout::PanelW-2.f,PY,2.f,PH);
}

void UVoxelPauseMenu::DrawCenteredText(const FString& Text, float Y, FLinearColor Col, float Scale) const
{
    if (!OwnerHUD) return; UFont* F=GetFont(); if (!F) return;
    float TW,TH; OwnerHUD->GetTextSize(Text,TW,TH,F,Scale);
    OwnerHUD->DrawText(Text, Col, CX-TW*0.5f, Y, F, Scale);
}

void UVoxelPauseMenu::DrawMenuItem(const FString& Label, float Y, bool bSel, bool bDim) const
{
    if (!OwnerHUD) return; UFont* F=GetFont(); if (!F) return;
    FLinearColor Col=bDim?PauseLayout::ColDimmed:bSel?PauseLayout::ColSel:PauseLayout::ColNormal;
    float TW,TH; OwnerHUD->GetTextSize(Label,TW,TH,F,PauseLayout::ItemSize);
    const float IX=CX-TW*0.5f;
    if (bSel&&!bDim) OwnerHUD->DrawRect(FLinearColor(0.15f,0.65f,0.25f,0.35f),IX-14.f,Y-4.f,TW+28.f,TH+8.f);
    OwnerHUD->DrawText(Label, Col, IX, Y, F, PauseLayout::ItemSize);
}

void UVoxelPauseMenu::DrawDivider(float Y) const
{
    if (!OwnerHUD) return;
    OwnerHUD->DrawRect(PauseLayout::ColDivider, CX-PauseLayout::PanelW*0.5f+16.f, Y, PauseLayout::PanelW-32.f, 1.f);
}
