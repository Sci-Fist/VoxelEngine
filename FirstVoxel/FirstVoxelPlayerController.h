// FirstVoxelPlayerController.h
//
// FIX N8  — RaycastAccum throttles PlayerTick physics trace to ~12 Hz.
// FIX N11 — CachedVoxelWorld caches FindVoxelWorld() result; no per-call scan.
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

    /** Modify voxel terrain at the player's crosshair hit point. */
    void ModifyVoxelTerrain(float InRadius, float InDensity);

    /**
     * Returns the first AVoxelWorld in the level.
     * FIX N11: result cached in CachedVoxelWorld; O(1) after first call.
     */
    AVoxelWorld* FindVoxelWorld() const;

    /** Toggle the world map on/off. */
    UFUNCTION(BlueprintCallable, Category="Voxel|Map")
    void ToggleMap();

    UFUNCTION(BlueprintCallable, Category="Voxel|Map")
    void OpenMap();

    UFUNCTION(BlueprintCallable, Category="Voxel|Map")
    void CloseMap();

    UFUNCTION(BlueprintPure, Category="Voxel|Map")
    bool IsMapOpen() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void SetupInputComponent() override;
    virtual void PlayerTick(float DeltaTime) override;

    UPROPERTY(EditDefaultsOnly, Category="UI")
    TSubclassOf<UUserWidget> CrosshairWidgetClass;

    UPROPERTY(EditDefaultsOnly, Category="UI|Map",
        meta=(ToolTip="Widget class for the world map. Defaults to UVoxelMapWidget."))
    TSubclassOf<UVoxelMapWidget> MapWidgetClass;

    UPROPERTY(EditDefaultsOnly, Category="Input")
    UInputMappingContext* MappingContext = nullptr;

private:
    UPROPERTY()
    TObjectPtr<UVoxelMapWidget> MapWidgetInstance = nullptr;

    void EnsureMapWidget();

    /** FIX N11: cached world reference — avoids TActorIterator scan per call. */
    UPROPERTY(Transient)
    TObjectPtr<AVoxelWorld> CachedVoxelWorld = nullptr;

    /** Last successful aim trace result. */
    FHitResult LastAimHit;
    bool bHasAimHit = false;

    /** FIX N8: accumulator for 12 Hz trace throttle. */
    float RaycastAccum = 0.f;
};
