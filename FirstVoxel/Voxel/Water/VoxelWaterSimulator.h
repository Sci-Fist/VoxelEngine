// =============================================================================
// VoxelWaterSimulator.h  [canonical: Voxel/Water/]
// =============================================================================
//
// Cellular-automata water simulation system for voxel terrain.
//
// Implements realistic water physics including gravity flow, lateral spreading,
// and water source management within the chunk-based world system. Operates
// on FVoxelWaterData stored inside AVoxelChunk actors and is driven by
// UVoxelWorldWaterComponent at a configurable tick interval.
//
// @thread-safety Game-thread only. All public methods must be called from
//                the game thread. No internal locking is used.
// @performance   O(N) per chunk where N is number of water cells.
//                Optimized for sparse water distribution with early termination.
//
// -- SIMULATION RULES (one Step() call) ---------------------------------------
//
//   For every registered chunk, for every voxel with water:
//
//   1. SOURCE REFRESH
//      Cells with WATER_SOURCE are set to WATER_FULL before any flow runs.
//      Sources are permanent and never drained.
//
//   2. GRAVITY (FALL)
//      If the voxel directly below is air (not solid, not already full),
//      transfer all water downward. Fast path for vertical columns.
//
//   3. SPREAD
//      When blocked below, test four cardinal horizontal neighbours.
//      Flow to any neighbour that has room (fill < current - 1).
//      Equalises levels over multiple steps, creating natural pooling.
//
//   4. BOUNDARY
//      Water flowing out of a loaded chunk's boundary is discarded.
//      Cross-chunk propagation requires both chunks to be registered.
//
// -- CHUNK REGISTRATION -------------------------------------------------------
//
//   RegisterChunk(Coord, WaterData*, Generation)
//     Call after AVoxelChunk::ApplyMesh(). Pass the chunk's WaterGeneration
//     counter so that Step() can detect stale (recycled) entries.
//
//   UnregisterChunk(Coord)
//     Call before ReturnChunk() / pool reuse.
//
//   The simulator holds RAW POINTERS into FVoxelWaterData owned by AVoxelChunk.
//   The chunk MUST outlive its registration. AVoxelWorld ensures this by calling
//   UnregisterChunk before ReturnChunk.
//
// -- GENERATIONAL GUARD -------------------------------------------------------
//
//   FChunkEntry stores the WaterGeneration value captured at RegisterChunk().
//   If a chunk is recycled (ClearMesh increments WaterGeneration) without
//   being unregistered first, Step() detects the mismatch and skips that
//   entry, preventing writes through a stale FVoxelWaterData pointer.
//   This is a safety net — the primary guard is the UnregisterChunk call in
//   AVoxelWorld::DestroyChunk().
//
// -- RETURN VALUE OF Step() ---------------------------------------------------
//
//   Returns TArray<FIntVector> of chunk coords whose Cells[] changed.
//   UVoxelWorldWaterComponent calls AVoxelChunk::RebuildWaterMesh() on each.
//
// -- PERFORMANCE CHARACTERISTICS ----------------------------------------------
//
//   - Processing order: bottom-to-top for natural gravity simulation
//   - Early termination when cells are empty or sources
//   - Chunk-based processing for memory efficiency
//   - Dirty chunk tracking to minimize mesh updates
//   - Sparse water distribution optimization via HasAnyWater() early-out
// =============================================================================
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

    /**
     * Register a chunk's water data for simulation.
     * @param ChunkCoord   Grid coordinate of the chunk.
     * @param WaterData    Pointer to the chunk's water data. Must outlive registration.
     * @param Generation   The chunk's WaterGeneration counter at registration time.
     *                     Step() skips entries where this value no longer matches.
     * Call after a chunk finishes generation (GameThread only).
     */
    void RegisterChunk(const FIntVector& ChunkCoord, FVoxelWaterData* WaterData, int32 Generation);

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
     *
     * Entries whose stored Generation does not match the chunk's current
     * WaterGeneration are silently skipped (stale pointer guard).
     */
    TArray<FIntVector> Step();

    /** Wipe all water data from all registered chunks. */
    void ClearAll();

private:
    int32 ChunkSize;
    float VoxelSize;

    struct FChunkEntry
    {
        FVoxelWaterData* Data       = nullptr;
        // Generation value captured at RegisterChunk(). If the owning chunk is
        // recycled (ClearMesh increments its WaterGeneration counter) without
        // an explicit UnregisterChunk call, this mismatch is caught in Step()
        // and the entry is skipped, preventing writes through a stale pointer.
        int32            Generation = -1;
    };
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

    bool SimCell(const FIntVector& WV, uint8* SrcCell, TSet<FIntVector>& DirtyChunks);
};
