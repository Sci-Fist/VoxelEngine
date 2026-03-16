// FirstVoxelPlayerController.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FirstVoxelPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;
class AVoxelWorld;
class UVoxelMapWidget;

UCLASS()
class FIRSTVOXEL_API AFirstVoxelPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    AFirstVoxelPlayerController();

    /** Tries to modify voxel terrain at the player's crosshair. */
    void ModifyVoxelTerrain(float InRadius, float InDensity);

    /** Lazily finds the first AVoxelWorld in the level. */
    AVoxelWorld* FindVoxelWorld() const;

    /**
     * Toggle the world map on/off.
     * Called by AFirstVoxelCharacter when M is pressed.
     * Automatically finds the nearest AVoxelWorld in the level.
     */
    UFUNCTION(BlueprintCallable, Category="Voxel|Map")
    void ToggleMap();

    /** Programmatically open / close without toggling. */
    UFUNCTION(BlueprintCallable, Category="Voxel|Map")
    void OpenMap();

    UFUNCTION(BlueprintCallable, Category="Voxel|Map")
    void CloseMap();

    UFUNCTION(BlueprintPure, Category="Voxel|Map")
    bool IsMapOpen() const;

protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void PlayerTick(float DeltaTime) override;

    // ---- UI assets ----

    /** Crosshair widget class (assign in BP subclass). */
    UPROPERTY(EditDefaultsOnly, Category="UI")
    TSubclassOf<UUserWidget> CrosshairWidgetClass;

    /**
     * Map widget class.
     * Defaults to UVoxelMapWidget (pure C++, no Blueprint needed).
     * Override with a Blueprint subclass (WBP_VoxelMap) to customise the layout.
     * 
     * Note: Combat and Side-Scrolling variants have their own specialized player
     * controllers (CombatPlayerController and SideScrollingPlayerController) that
     * are currently unused but preserved for future development.
     */
    UPROPERTY(EditDefaultsOnly, Category="UI|Map",
        meta=(ToolTip="Widget class for the world map. Defaults to UVoxelMapWidget. Assign a Blueprint subclass to override layout."))
    TSubclassOf<UVoxelMapWidget> MapWidgetClass;

    /** Input mapping context (assign in BP subclass or leave nullptr for no-context mode). */
    UPROPERTY(EditDefaultsOnly, Category="Input")
    UInputMappingContext* DefaultMappingContext;

private:
    UPROPERTY()
    TObjectPtr<UVoxelMapWidget> MapWidgetInstance = nullptr;

    void EnsureMapWidget();

    // [Expert Optimization] Cache line traces to prevent duplicate Raycasts per frame
    FHitResult LastAimHit;
    bool bHasAimHit = false;
};
