// VoxelWaterSimulator.h  [canonical location: Voxel/Water/]
// Cellular-automata voxel water simulation.
//
// SIMULATION RULES (run by Step() on a fixed timer from AVoxelWorld)
//   1. Source cells are forced to WATER_FULL at the start of every step.
//   2. FALL:   if the cell below is air and not full, water falls into it.
//   3. SPREAD: if below is blocked, water flows laterally to lower neighbours.
//   4. EVAPORATE: water leaving a loaded chunk boundary is discarded.
//
// THREAD SAFETY
//   All public methods must be called from the GameThread.
//   The simulator holds raw pointers into FVoxelWaterData structs owned by
//   AVoxelChunk; those structs must outlive their registration.
#pragma once

#include "CoreMinimal.h"
#include "Math/IntVector.h"
#include "Voxel/Water/VoxelWaterTypes.h"

class FIRSTVOXEL_API FVoxelWaterSimulator
{
public:
    /**
     * @param InChunkSize  Voxels per chunk side (e.g. 16).
     * @param InVoxelSize  World-space cm per voxel (e.g. 100).
     */
    explicit FVoxelWaterSimulator(int32 InChunkSize, float InVoxelSize);

    // ---- Chunk registration ------------------------------------------------

    /** Call after a chunk finishes generation (GameThread only). */
    void RegisterChunk(const FIntVector& ChunkCoord, FVoxelWaterData* WaterData);

    /** Call before a chunk is destroyed or returned to the pool. */
    void UnregisterChunk(const FIntVector& ChunkCoord);

    bool IsRegistered(const FIntVector& ChunkCoord) const { return ChunkMap.Contains(ChunkCoord); }

    // ---- Water placement ---------------------------------------------------

    /** Place a permanent water source at a world-voxel coordinate. */
    void SetSource(const FIntVector& WorldVoxel);

    /** Place flowing water at a world-voxel coordinate. */
    void SetFlowing(const FIntVector& WorldVoxel, uint8 Level = WATER_FULL);

    /** Remove all water from a world-voxel coordinate. */
    void ClearCell(const FIntVector& WorldVoxel);

    /** Read the fill level (0 if chunk not loaded). */
    uint8 GetLevel(const FIntVector& WorldVoxel) const;

    bool IsWater (const FIntVector& WorldVoxel) const;
    bool IsSolid (const FIntVector& WorldVoxel) const;

    // ---- Simulation --------------------------------------------------------

    /**
     * Advance one cellular-automata step.
     * Returns coordinates of chunks whose water data changed so the caller
     * (AVoxelWorld) can call RebuildWaterMesh() on those chunks.
     */
    TArray<FIntVector> Step();

    /** Wipe all water data from all registered chunks. */
    void ClearAll();

private:
    int32 ChunkSize;
    float VoxelSize;

    struct FChunkEntry { FVoxelWaterData* Data = nullptr; };
    TMap<FIntVector, FChunkEntry> ChunkMap;

    // Coordinate helpers
    FORCEINLINE FIntVector ToChunkCoord(const FIntVector& WV) const;
    FORCEINLINE FIntVector ToLocal     (const FIntVector& WV) const;
    FORCEINLINE int32      LocalIdx(int32 lx, int32 ly, int32 lz) const
    {
        return lx + ly * ChunkSize + lz * ChunkSize * ChunkSize;
    }

    uint8*       CellPtr     (const FIntVector& WV);
    const uint8* CellPtrConst(const FIntVector& WV) const;
    bool         IsSolidAt   (const FIntVector& WV) const;

    bool SimCell(const FIntVector& WV, TSet<FIntVector>& DirtyChunks);
};
