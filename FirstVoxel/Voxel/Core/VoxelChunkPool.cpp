// VoxelChunkPool.cpp
// Object pool for AVoxelChunk actors.
// Recycling avoids the cost of repeated SpawnActor / Destroy calls and
// eliminates the GC hitches that occur when thousands of components are
// constructed / destructed every time the player moves between biomes.
//
// LIFECYCLE:
//   ReturnChunk    -- called by DestroyChunk(); hides the actor and resets its mesh.
//   RetrieveOrCreateChunk -- called by SpawnChunk(); pops a recycled actor or spawns a fresh one.
//   Clear          -- called at world teardown to destroy every pooled actor.

#include "Core/VoxelChunkPool.h"
#include "Core/VoxelChunk.h"
#include "Engine/World.h"

// ---------------------------------------------------------------------------
//  ReturnChunk
//  Cancel any in-flight generation, wipe the mesh, and park the actor.
// ---------------------------------------------------------------------------
void FVoxelChunkPool::ReturnChunk(AVoxelChunk* Chunk)
{
	if (!IsValid(Chunk)) return;

	Chunk->CancelGeneration();
	Chunk->ClearMesh();

	// Hide and disable collision so the parked actor is truly inert.
	Chunk->SetActorHiddenInGame(true);
	Chunk->SetActorEnableCollision(false);

	Pool.Add(Chunk);
}

// ---------------------------------------------------------------------------
//  RetrieveOrCreateChunk
//  Pop a valid actor from the pool, or spawn a brand-new one.
// ---------------------------------------------------------------------------
AVoxelChunk* FVoxelChunkPool::RetrieveOrCreateChunk(UWorld* World, const FVector& Location, AActor* Owner)
{
	// Try to recycle — drain stale (already-destroyed) entries while we search.
	while (Pool.Num() > 0)
	{
		AVoxelChunk* Recycled = Pool.Pop(EAllowShrinking::No);
		if (IsValid(Recycled))
		{
			Recycled->SetActorLocation(Location);
			// Keep hidden/no-collision until GenerateAsync() finishes.
			Recycled->SetActorHiddenInGame(true);
			Recycled->SetActorEnableCollision(false);
			return Recycled;
		}
	}

	// Pool empty — spawn a new actor.
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
// ---------------------------------------------------------------------------
void FVoxelChunkPool::Clear()
{
	for (AVoxelChunk* Chunk : Pool)
	{
		if (IsValid(Chunk))
		{
			Chunk->Destroy();
		}
	}
	Pool.Empty();
}
