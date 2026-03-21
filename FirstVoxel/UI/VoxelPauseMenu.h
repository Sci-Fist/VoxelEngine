// VoxelPauseMenu.h
//
// FIX N16 — slot constants now come from VoxelSaveSlotConfig.h
// FIX N19 — CachedVoxelWorld caches FindVoxelWorld() result (was O(N) per Draw)
// FIX N20 — Close() now restores MOVE_Walking + UpdateFloorFromAdjustment
//            instead of MOVE_Falling + bJustTeleported
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Misc/Paths.h"
#include "InputCoreTypes.h"
#include "VoxelSaveSlotConfig.h"
#include "VoxelPauseMenu.generated.h"

class AVoxelWorld;
class AHUD;
class UFont;

UENUM()
enum class EPauseMenuPanel : uint8
{
    Main, SaveSlot, LoadSlot, Confirm
};

UCLASS()
class FIRSTVOXEL_API UVoxelPauseMenu : public UObject
{
    GENERATED_BODY()

public:
    void Init(AHUD* InHUD);
    void Open();
    void Close();
    bool IsOpen() const { return bOpen; }
    void Draw();

private:
    bool            bOpen         = false;
    EPauseMenuPanel ActivePanel   = EPauseMenuPanel::Main;
    int32           MainSelection = 0;
    static constexpr int32 MainItemCount = 4;
    int32           SlotSelection = 0;

    UPROPERTY() TObjectPtr<AHUD> OwnerHUD = nullptr;

    // FIX N19: cache to avoid O(N_actors) TActorIterator scan every Draw()
    UPROPERTY(Transient) mutable TObjectPtr<AVoxelWorld> CachedVoxelWorld = nullptr;

    AVoxelWorld* FindVoxelWorld() const;
    UFont*       GetFont()        const;

    void DrawMain();
    void DrawSlotPanel(bool bIsSave);
    void DrawConfirm();

    bool JustPressed(const FKey Key) const;
    void HandleMainInput();
    void HandleSlotInput(bool bIsSave);
    void HandleConfirmInput();

    FString SlotFileName(int32 SlotIdx) const;
    bool    SlotExists  (int32 SlotIdx) const;

    void DrawBackground()                                                const;
    void DrawCenteredText(const FString& Text, float Y,
                          FLinearColor Color, float Scale = 1.f)        const;
    void DrawMenuItem(const FString& Label, float Y,
                      bool bSelected, bool bDimmed = false)             const;
    void DrawDivider(float Y)                                            const;

    mutable float CX = 0.f;
    mutable float CY = 0.f;
};
