#pragma once

#include "CoreMinimal.h"
#include "VoxelDensityChunk.h"

/**
 * ChunkManager (World Access Layer)
 * Manages all sparse dense density buffers across loaded chunk grids seamlessly.
 */
struct FVoxelChunkManager
{
	/** Active dense scalar buffers indexed by chunk coordinates (e.g., FIntVector(0,0,0)). */
	TMap<FIntVector, TSharedPtr<FVoxelDensityChunk>> DenseChunks;

	/** Returns the Density Chunk at Coord, allocating a new one if not present. */
	TSharedPtr<FVoxelDensityChunk> GetOrCreateChunk(const FIntVector& Coord, int32 GridSize)
	{
		if (TSharedPtr<FVoxelDensityChunk>* Ptr = DenseChunks.Find(Coord))
		{
			return *Ptr;
		}

		TSharedPtr<FVoxelDensityChunk> NewChunk = MakeShared<FVoxelDensityChunk>();
		NewChunk->Init(GridSize);
		DenseChunks.Add(Coord, NewChunk);
		return NewChunk;
	}

	/** Clears all dense caches. */
	void Clear()
	{
		DenseChunks.Empty();
	}
};
