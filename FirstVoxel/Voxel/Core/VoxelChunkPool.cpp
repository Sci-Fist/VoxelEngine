// VoxelChunkPool.cpp
// FIX N7 — MaxPoolSize cap enforced in ReturnChunk().
// Chunks beyond the cap are destroyed immediately instead of accumulating.

#include "Core/VoxelChunkPool.h"
#include "Core/VoxelChunk.h"
#include "Engine/World.h"

void FVoxelChunkPool::ReturnChunk(AVoxelChunk* Chunk)
{
    if (!IsValid(Chunk)) return;
    Chunk->Reset();
    Chunk->SetActorHiddenInGame(true);
    Chunk->SetActorEnableCollision(false);

    // FIX N7: Destroy overflow chunks instead of parking them indefinitely.
    // MaxPoolSize prevents the pool from accumulating hundreds of actors in
    // long streaming sessions where many chunks are cycled in/out.
    if (Pool.Num() >= MaxPoolSize)
    {
        Chunk->Destroy();
        return;
    }

    Pool.Add(Chunk);
}

AVoxelChunk* FVoxelChunkPool::RetrieveOrCreateChunk(UWorld* World,
                                                      const FVector& Location,
                                                      AActor* Owner)
{
    while (Pool.Num() > 0)
    {
        AVoxelChunk* Recycled = Pool.Pop(EAllowShrinking::No);
        if (IsValid(Recycled))
        {
            Recycled->SetActorLocation(Location);
            Recycled->SetActorHiddenInGame(true);
            Recycled->SetActorEnableCollision(false);
            return Recycled;
        }
    }

    if (!World) return nullptr;
    FActorSpawnParameters Params;
    Params.Owner = Owner;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return World->SpawnActor<AVoxelChunk>(
        AVoxelChunk::StaticClass(), Location, FRotator::ZeroRotator, Params);
}

void FVoxelChunkPool::Clear()
{
    for (AVoxelChunk* Chunk : Pool)
        if (IsValid(Chunk)) Chunk->Destroy();
    Pool.Empty();
}
