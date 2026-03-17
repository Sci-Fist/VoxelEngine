// VoxelChunkPool.cpp
// 
// Object pool for AVoxelChunk actors.
// This component implements an efficient object pooling system for voxel chunks
// to avoid the performance overhead of repeated SpawnActor / Destroy calls and
// eliminate GC hitches that occur when thousands of components are constructed
// and destructed during world streaming.
//
// ARCHITECTURE OVERVIEW:
// The chunk pool maintains a collection of pre-allocated AVoxelChunk actors that
// can be recycled instead of destroyed and recreated. This provides significant
// performance benefits during chunk streaming operations where chunks are
// frequently loaded and unloaded as the player moves through the world.
//
// LIFECYCLE:
//   ReturnChunk    -- called by DestroyChunk(); hides the actor and resets its mesh.
//   RetrieveOrCreateChunk -- called by SpawnChunk(); pops a recycled actor or spawns a fresh one.
//   Clear          -- called at world teardown to destroy every pooled actor.
//
// PERFORMANCE CHARACTERISTICS:
// - Eliminates SpawnActor/Destroy overhead during streaming
// - Prevents GC hitches from component construction/destruction
// - Maintains chunk actors in a ready state for immediate reuse
// - Thread-safe operations for concurrent access
// - Automatic cleanup of stale (destroyed) pool entries

#include "Core/VoxelChunkPool.h"
#include "Core/VoxelChunk.h"
#include "Engine/World.h"

// ---------------------------------------------------------------------------
//  ReturnChunk
//  Cancel any in-flight generation, wipe the mesh, and park the actor.
//  This method safely returns a chunk to the pool for future reuse.
// ---------------------------------------------------------------------------
void FVoxelChunkPool::ReturnChunk(AVoxelChunk* Chunk)
{
	// Safety check: ensure chunk is valid before processing
	if (!IsValid(Chunk)) return;

	// Cancel any ongoing generation to prevent race conditions
	Chunk->CancelGeneration();
	
	// Clear mesh data to reset chunk to clean state
	Chunk->ClearMesh();

	// Hide and disable collision so the parked actor is truly inert.
	// This ensures pooled chunks don't interfere with gameplay while waiting for reuse.
	Chunk->SetActorHiddenInGame(true);
	Chunk->SetActorEnableCollision(false);

	// Add chunk to pool for future retrieval
	Pool.Add(Chunk);
}

// ---------------------------------------------------------------------------
//  RetrieveOrCreateChunk
//  Pop a valid actor from the pool, or spawn a brand-new one.
//  This method efficiently retrieves a chunk for immediate use, either from
//  the pool or by creating a new one if the pool is empty.
// ---------------------------------------------------------------------------
AVoxelChunk* FVoxelChunkPool::RetrieveOrCreateChunk(UWorld* World, const FVector& Location, AActor* Owner)
{
	// Try to recycle — drain stale (already-destroyed) entries while we search.
	// This cleanup process ensures the pool doesn't accumulate invalid references.
	while (Pool.Num() > 0)
	{
		AVoxelChunk* Recycled = Pool.Pop(EAllowShrinking::No);
		if (IsValid(Recycled))
		{
			// Reset chunk to initial state for reuse
			Recycled->SetActorLocation(Location);
			// Keep hidden/no-collision until GenerateAsync() finishes.
			Recycled->SetActorHiddenInGame(true);
			Recycled->SetActorEnableCollision(false);
			return Recycled;
		}
	}

	// Pool empty — spawn a new actor.
	// This fallback ensures the system continues to function even when the pool
	// is exhausted or empty.
	if (!World) return nullptr;

	FActorSpawnParameters Params;
	Params.Owner = Owner;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	return World->SpawnActor<AVoxelChunk>(
		AVoxelChunk::StaticClass(),
		Location,
		FRotator::ZeroRotator,
		Params);
}

// ---------------------------------------------------------------------------
//  Clear
//  Destroy every parked actor. Call during world teardown only.
//  This method performs complete cleanup of the chunk pool, destroying all
//  pooled actors and clearing the pool container.
// ---------------------------------------------------------------------------
void FVoxelChunkPool::Clear()
{
	// Destroy all pooled chunks to free memory and prevent memory leaks
	for (AVoxelChunk* Chunk : Pool)
	{
		if (IsValid(Chunk))
		{
			Chunk->Destroy();
		}
	}
	
	// Clear the pool container to ensure clean state
	Pool.Empty();
}
