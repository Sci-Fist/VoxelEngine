// VoxelStreamingComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Biomes/VoxelBiomeManager.h"
#include "VoxelStreamingComponent.generated.h"

class AVoxelWorld;
class UVoxelCullingManager;

UCLASS( ClassGroup=(Voxel), meta=(BlueprintSpawnableComponent) )
class FIRSTVOXEL_API UVoxelStreamingComponent : public UActorComponent
{
    GENERATED_BODY()

public:	
    UVoxelStreamingComponent();

protected:
    virtual void BeginPlay() override;

public:	
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UFUNCTION(BlueprintCallable, Category="Voxel|Streaming")
    void UpdateStreaming();

    UFUNCTION(BlueprintCallable, Category="Voxel|Streaming")
    void CheckCloseRangeVisibility();
    void ApplyDiscoveryResult(const FVector& PlayerPos, const FIntVector& PlayerCoord, int32 SkyZMin, int32 SkyZMax, const TSet<FIntVector>& Desired);

    void AddChunkNeedingVisibilityCheck(const FIntVector& Coord);
    void ClearVisibilityChecks();

private:
    float StreamingTimer = 0.f;
    static constexpr float StreamingInterval = 0.25f;

    FVector LastStreamedPos = FVector::ZeroVector;
    FVector LastSkyAltPos = FVector(1e9f);

    float CachedSkyAltWorld = 0.f;
    float CachedCurvedH = 0.f;
    float CachedCurvedR = 0.f;

    TSet<FIntVector> ChunksNeedingVisibilityCheck;
    TSet<FIntVector> PendingDiscoveryTasks;

    // ── Hierarchical Discovery Settings ──────────────────────────────────
    float HighAltitudeThreshold = 10000.f; // 100m
    int32 MegaChunkMultiplier = 4;        // 4x4x4 chunks

    // ── GPU Culling State ──────────────────────────────────────────────
    int32 CurrentCullingRequestID = -1;
    TArray<FIntVector> PendingCullingChunks;

    TWeakObjectPtr<AVoxelWorld> WorldOwner;
    UPROPERTY(Transient)
    UVoxelCullingManager* CullingManager;

    // ── Refactored Helpers for UpdateStreaming ────────────────────────────
    void CalculateSkyAltitude(const FVector& PlayerPos, const struct FVoxelGenerationConfig& Config);
    void GatherColumnHeights(const FIntVector& PlayerCoord, float ChunkWorldSize, const struct FVoxelGenerationConfig& Config, int32 Radius, TArray<FVoxelBiomeManager::FWeightsAndHeight>& OutColumns);
    void BuildDesiredChunkSet(const FIntVector& PlayerCoord, const TArray<FVoxelBiomeManager::FWeightsAndHeight>& CachedColumns, float ChunkWorldSize, float SkyAltWorld, float HalfThickCm, int32 Radius, int32& OutSkyZMin, int32& OutSkyZMax, TSet<FIntVector>& OutDesired);
    void DiscoverHierarchical(const FIntVector& PlayerCoord, const TArray<FVoxelBiomeManager::FWeightsAndHeight>& CachedColumns, int32 Radius, TSet<FIntVector>& OutDesired);
    void UpdateLODs(const FVector& PlayerPos, const FIntVector& PlayerCoord, int32 SkyZMin, int32 SkyZMax, const TSet<FIntVector>& Desired);
    void RebuildGenerationQueue(const FVector& PlayerPos, const FVector& PlayerForward, const TSet<FIntVector>& Desired);
};
