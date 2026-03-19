#pragma once

#include "CoreMinimal.h"
#include "VoxelDensityChunk.h"

/**
 * FVoxelChunkManager
 *
 * World access layer for managing sparse dense density buffers across loaded
 * chunk grids.
 *
 * Provides a centralized TMap from chunk coordinates to FVoxelDensityChunk
 * instances. Chunks are allocated lazily on first access and freed when
 * Clear() is called or the manager is destroyed.
 *
 * THREAD SAFETY:
 * - NOT thread-safe. All methods must be called from the game thread.
 * - TMap is not safe for concurrent reads + writes.
 * - Background generation tasks must not call GetOrCreateChunk() directly;
 *   they receive a pre-allocated TSharedPtr from ConfigureChunk() on the GT.
 *
 * USAGE PATTERN:
 *   AVoxelWorld::ConfigureChunk() calls GetOrCreateChunk() on the game thread,
 *   stores the result in AVoxelChunk::DenseChunk (TSharedPtr), and passes it
 *   to the background FVoxelGeneratorTask. The task reads from the shared
 *   pointer without calling this manager directly.
 *
 * MEMORY MANAGEMENT:
 * - Uses TSharedPtr for automatic lifetime management of density chunks.
 * - Chunks are reference-counted: alive as long as AVoxelChunk holds a ref.
 * - Clear() releases all map references; chunks kept alive by chunks survive.
 */
struct FVoxelChunkManager
{
    /** Active dense scalar buffers indexed by chunk coordinate. */
    TMap<FIntVector, TSharedPtr<FVoxelDensityChunk>> DenseChunks;

    /**
     * Returns the density chunk at Coord, allocating one if none exists.
     *
     * GAME-THREAD ONLY. Do not call from background tasks.
     *
     * @param Coord     Chunk coordinate (e.g. FIntVector(0,0,0)).
     * @param GridSize  Voxels per side for a newly allocated chunk.
     * @return          Shared pointer to the existing or newly created chunk.
     */
    TSharedPtr<FVoxelDensityChunk> GetOrCreateChunk(const FIntVector& Coord, int32 GridSize)
    {
        if (TSharedPtr<FVoxelDensityChunk>* Ptr = DenseChunks.Find(Coord))
            return *Ptr;

        TSharedPtr<FVoxelDensityChunk> NewChunk = MakeShared<FVoxelDensityChunk>();
        NewChunk->Init(GridSize);
        DenseChunks.Add(Coord, NewChunk);
        return NewChunk;
    }

    /**
     * Release all map references to managed chunks.
     * Chunks still referenced by AVoxelChunk::DenseChunk remain alive.
     *
     * GAME-THREAD ONLY.
     */
    void Clear() { DenseChunks.Empty(); }
};
