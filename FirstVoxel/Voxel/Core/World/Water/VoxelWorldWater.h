// VoxelWorldWater.h
// Water subsystem manager for AVoxelWorld.
// Owns the water simulation tick, chunk water init, and source tracking.
// Intended to be held as a member (or TUniquePtr) on AVoxelWorld so water
// logic lives in its own translation unit (VoxelWorldWater.cpp).
#pragma once

#include "CoreMinimal.h"
#include "Math/IntVector.h"
#include "Voxel/VoxelLogger.h"

class AActor;
class AVoxelChunk;
class FVoxelWaterSimulator;
class UVoxelWaterComponent;



class FVoxelWorldWater
{
public:
    explicit FVoxelWorldWater(AActor* InWorldOwner);

    /** Wire up the external simulator and ocean component. Call once after creation. */
    void Initialize(FVoxelWaterSimulator* InWaterSimulator, UVoxelWaterComponent* InWaterComponent);

    // ---- Tick ---------------------------------------------------------------
    /** Called every frame from AVoxelWorld::Tick. Advances the sim on a fixed interval. */
    void TickWater(float DeltaTime);

    // ---- Chunk lifecycle ----------------------------------------------------
    /** Register a freshly generated chunk with the simulator and seed its sources. */
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

private:
    AActor*               WorldOwner    = nullptr;
    FVoxelWaterSimulator* WaterSimulator = nullptr;
    TWeakObjectPtr<UVoxelWaterComponent> WaterComponent;

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

    void LogWaterSimulationStep() const;
    void LogChunkWaterInit(const FIntVector& ChunkCoord) const;
    void LogWaterSourceRegistration(const FIntVector& WorldVoxelCoord) const;
};
