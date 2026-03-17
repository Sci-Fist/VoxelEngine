#pragma once

#include "CoreMinimal.h"
#include "Core/VoxelChunk.h"

// =============================================================================
// FVoxelChunkPool
// =============================================================================
//
// Object pool for AVoxelChunk actors. Prevents the expensive SpawnActor /
// Destroy round-trip that would otherwise occur every time the player moves
// and chunks scroll in/out of the render distance.
//
// -- LIFECYCLE ----------------------------------------------------------------
//
//  ReturnChunk(Chunk)
//    Called by AVoxelWorld::DestroyChunk().
//    Cancels any in-flight generation, clears the mesh, hides the actor,
//    disables collision, and pushes the chunk onto the Pool stack.
//
//  RetrieveOrCreateChunk(World, Location, Owner)
//    Called by AVoxelWorld::SpawnChunk().
//    Pops a valid actor from the pool (skipping stale entries) or falls
//    back to World->SpawnActor when the pool is empty.
//    The returned chunk is still hidden with collision off -- the caller
//    is responsible for configuring and calling GenerateAsync().
//
//  Clear()
//    Called during world teardown. Destroys every pooled actor.
//
// -- STALE ENTRY HANDLING -----------------------------------------------------
//
//  Pool entries can become stale (IsValid returns false) if the actor was
//  destroyed externally (e.g. level cleanup). RetrieveOrCreateChunk drains
//  invalid entries while searching rather than doing a separate validation
//  pass, so the pool stays compact without a dedicated cleanup tick.
//
// -- THREAD SAFETY ------------------------------------------------------------
//
//  Pool is accessed only from the game thread. No locking is required.
// =============================================================================
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
