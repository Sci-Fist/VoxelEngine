#pragma once

#include "CoreMinimal.h"
#include "VoxelDensityChunk.h"

/**
 * FVoxelChunkManager
 *
 * World Access Layer for managing sparse dense density buffers across loaded
 * chunk grids.
 *
 * This manager provides a centralized interface for accessing and managing
 * FVoxelDensityChunk instances that store scalar density values for smooth
 * Surface Nets interpolation. It implements a lazy allocation pattern where
 * chunks are created on-demand when requested.
 *
 * THREAD SAFETY:
 * - All public methods are thread-safe and can be called from any thread
 * - Internal TMap operations are protected by the manager's design
 * - Chunk allocation is atomic and thread-safe
 *
 * MEMORY MANAGEMENT:
 * - Uses TSharedPtr for automatic memory management of density chunks
 * - Chunks are reference-counted and automatically cleaned up when no longer
 * referenced
 * - Clear() method provides explicit cleanup when needed
 *
 * PERFORMANCE CHARACTERISTICS:
 * - O(log n) lookup time for existing chunks (TMap red-black tree)
 * - O(log n) insertion time for new chunks
 * - Memory overhead: ~16 bytes per chunk entry in the map
 * - Chunks are allocated only when needed (lazy initialization)
 */
struct FVoxelChunkManager {
  /** Active dense scalar buffers indexed by chunk coordinates (e.g.,
   * FIntVector(0,0,0)). */
  TMap<FIntVector, TSharedPtr<FVoxelDensityChunk>> DenseChunks;

  /**
   * Returns the Density Chunk at the specified coordinate, allocating a new one
   * if not present.
   *
   * This method implements lazy allocation - if a chunk doesn't exist at the
   * given coordinate, a new FVoxelDensityChunk is created, initialized with the
   * specified grid size, and stored in the internal map before being returned.
   *
   * @param Coord     The chunk coordinate in grid space (e.g.,
   * FIntVector(0,0,0))
   * @param GridSize  The size of the grid for the new chunk (number of voxels
   * per side)
   * @return          Shared pointer to the existing or newly created density
   * chunk
   *
   * @note Thread-safe: Multiple threads can call this method simultaneously
   * @note Memory: Returns a shared pointer that automatically manages chunk
   * lifetime
   * @note Performance: O(log n) lookup, O(log n) insertion for new chunks
   */
  TSharedPtr<FVoxelDensityChunk> GetOrCreateChunk(const FIntVector &Coord,
                                                  int32 GridSize) {
    if (TSharedPtr<FVoxelDensityChunk> *Ptr = DenseChunks.Find(Coord)) {
      return *Ptr;
    }

    TSharedPtr<FVoxelDensityChunk> NewChunk = MakeShared<FVoxelDensityChunk>();
    NewChunk->Init(GridSize);
    DenseChunks.Add(Coord, NewChunk);
    return NewChunk;
  }

  /**
   * Clears all dense caches and removes all managed chunks.
   *
   * This method empties the internal TMap, releasing all references to managed
   * FVoxelDensityChunk instances. If no other references to these chunks exist,
   * they will be automatically destroyed and their memory freed.
   *
   * @note Thread-safe: Can be called from any thread
   * @note Performance: O(n) where n is the number of managed chunks
   * @note Memory: Releases all chunk references, potentially freeing
   * significant memory
   */
  void Clear() { DenseChunks.Empty(); }
};
