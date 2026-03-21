// =============================================================================
// VoxelChunkPool.h
// FIX N7 — Added MaxPoolSize cap (default 48).
//           In long streaming sessions the pool accumulated hundreds of
//           hidden chunk actors. Anything beyond MaxPoolSize is now destroyed
//           immediately in ReturnChunk() instead of being parked.
// =============================================================================
#pragma once

#include "CoreMinimal.h"
#include "Core/VoxelChunk.h"

struct FVoxelChunkPool
{
public:
    /**
     * Maximum number of idle chunks to keep parked.
     * Additional chunks beyond this cap are destroyed immediately in ReturnChunk().
     * 48 = 3 × (RenderDistanceXY=8) columns × ~2 Z-levels average — covers
     * typical streaming burst without unbounded growth.
     */
    static constexpr int32 MaxPoolSize = 48;

    /** Adds an inactive chunk back to the pool (or destroys it if pool is full). */
    void ReturnChunk(AVoxelChunk* Chunk);

    /** Retrieves a chunk from the pool or spawns a new one if empty. */
    AVoxelChunk* RetrieveOrCreateChunk(UWorld* World, const FVector& Location, AActor* Owner);

    /** Destroy every parked actor. Call during world teardown only. */
    void Clear();

    int32 Num() const { return Pool.Num(); }

private:
    TArray<AVoxelChunk*> Pool;
};
