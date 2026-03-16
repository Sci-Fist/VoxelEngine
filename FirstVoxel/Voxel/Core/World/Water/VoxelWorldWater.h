// VoxelWorldWater.h
// Water simulation and registration manager for AVoxelWorld.
// Extracted into a dedicated ActorComponent to reduce monolithic AVoxelWorld creep.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Math/IntVector.h"
#include "VoxelWorldWater.generated.h"

class AVoxelWorld;
class AVoxelChunk;
class FVoxelWaterSimulator;
class UVoxelWaterComponent;

UCLASS(ClassGroup = (Voxel), meta = (BlueprintSpawnableComponent))
class FIRSTVOXEL_API UVoxelWorldWaterComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UVoxelWorldWaterComponent();

    /** Wire up the external simulator and ocean component. Call during initialization. */
    void Initialize(TUniquePtr<FVoxelWaterSimulator> InWaterSimulator, UVoxelWaterComponent* InOceanComponent);

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // ---- Chunk lifecycle ----------------------------------------------------
    /** Register chunk with simulator and bind chunk source callback. */
    void InitChunkWater(AVoxelChunk* Chunk);

    /** Unregister a chunk that is about to be destroyed or pooled. */
    void RemoveChunkFromWaterSimulation(const FIntVector& ChunkCoord);

    // ---- Source management --------------------------------------------------
    void RegisterWaterSource  (const FIntVector& WorldVoxelCoord);
    void UnregisterWaterSource(const FIntVector& WorldVoxelCoord);
    void ClearAllWaterSources ();

    // ---- State --------------------------------------------------------------
    void ResetWaterState();
    void ClearChunkWaterData(const FIntVector& ChunkCoord);
    bool IsChunkWaterDirty  (const FIntVector& ChunkCoord) const;

    FVoxelWaterSimulator* GetSimulator() const { return WaterSimulator.Get(); }

private:
    TUniquePtr<FVoxelWaterSimulator> WaterSimulator;
    TWeakObjectPtr<UVoxelWaterComponent> OceanComponent;

    /** All world-voxel coordinates that have been registered as water sources. */
    TArray<FIntVector> WaterSources;

    /** Per-chunk water source lists for efficient init/teardown. */
    TMap<FIntVector, TArray<FIntVector>> ChunkWaterSources;

    bool  bWaterSimulationEnabled = true;
    float WaterSimTimer           = 0.f;
    float WaterSimInterval        = 0.2f;

    // ---- Helpers ------------------------------------------------------------
    void UpdateWaterSimulation(float DeltaTime);
    void ProcessChunkWaterSources(AVoxelChunk* Chunk, const FIntVector& ChunkCoord);
    void RebuildWaterMeshForChunk(AVoxelChunk* Chunk);
};
