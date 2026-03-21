// =============================================================================
// VoxelChunkManager.h
// FIX N6 — DenseChunks is now pruned when chunks are unloaded.
//           Added RemoveChunk(Coord) so AVoxelWorld::DestroyChunk() can call it.
//           Previously entries accumulated indefinitely — after a long session
//           with streaming, every chunk ever loaded had a DenseChunks entry.
// =============================================================================
#pragma once

#include "CoreMinimal.h"
#include "VoxelDensityChunk.h"

// NOTE: false thread-safety claim removed (Issue #8 from original analysis).
// This manager is game-thread only — no locking is needed.
struct FVoxelChunkManager
{
    /** Active dense scalar buffers indexed by chunk coordinate. */
    TMap<FIntVector, TSharedPtr<FVoxelDensityChunk>> DenseChunks;

    /**
     * Returns the density chunk at Coord, allocating one if none exists.
     * Game-thread only.
     */
    TSharedPtr<FVoxelDensityChunk> GetOrCreateChunk(const FIntVector& Coord, int32 GridSize)
    {
        if (TSharedPtr<FVoxelDensityChunk>* Ptr = DenseChunks.Find(Coord))
            return *Ptr;
        TSharedPtr<FVoxelDensityChunk> New = MakeShared<FVoxelDensityChunk>();
        New->Init(GridSize);
        DenseChunks.Add(Coord, New);
        return New;
    }

    /**
     * FIX N6: Remove one chunk's entry when it is unloaded.
     * The TSharedPtr will be destroyed here if the chunk no longer holds a ref
     * (which it does until ClearMesh/pool recycle). Call from DestroyChunk().
     * Game-thread only.
     */
    void RemoveChunk(const FIntVector& Coord)
    {
        DenseChunks.Remove(Coord);
    }

    /**
     * Release all entries. Chunks still referenced by AVoxelChunk::DenseChunk
     * remain alive until the chunk is returned to the pool and ClearMesh() runs.
     * Game-thread only.
     */
    void Clear() { DenseChunks.Empty(); }

    /** Current number of tracked chunks. */
    int32 Num() const { return DenseChunks.Num(); }
};
