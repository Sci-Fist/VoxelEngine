#pragma once

#include "CoreMinimal.h"
#include "Core/VoxelChunk.h"

/**
 * FVoxelChunkPool manages a collection of inactive AVoxelChunk actors.
 * It facilitates recycling chunks instead of spawning and destroying them dynamically
 * to improve performance and avoid Garbage Collection hiccups.
 */
struct FVoxelChunkPool
{
public:
    /** Adds an inactive chunk back to the pool, hiding it. */
    void ReturnChunk(AVoxelChunk* Chunk);

    /** Retrieves a chunk from the pool or spawns a new one if empty. */
    AVoxelChunk* RetrieveOrCreateChunk(UWorld* World, const FVector& Location, AActor* Owner);

    /** Safe iteration to empty the pool and destroy holding actors. */
    void Clear();

private:
    TArray<AVoxelChunk*> Pool;
};
