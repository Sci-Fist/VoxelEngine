#include "VoxelChunkPool.h"

void FVoxelChunkPool::ReturnChunk(AVoxelChunk* Chunk)
{
    if (!IsValid(Chunk)) return;

    Chunk->ClearMesh();
    Chunk->SetActorHiddenInGame(true);
    Chunk->SetActorEnableCollision(false);
    Pool.AddUnique(Chunk); // [Expert Fix] Prevent duplicate entries
}

AVoxelChunk* FVoxelChunkPool::RetrieveOrCreateChunk(UWorld* World, const FVector& Location, AActor* Owner)
{
    AVoxelChunk* Chunk = nullptr;

    // [Expert Fix] Safety Validation Loop for popped actors
    while (Pool.Num() > 0)
    {
        Chunk = Pool.Pop();
        if (IsValid(Chunk))
        {
            Chunk->SetActorLocation(Location);
            Chunk->SetActorHiddenInGame(false);
            Chunk->SetActorEnableCollision(true);
            break; 
        }
        Chunk = nullptr; // Ignore pending kill
    }

    if (!Chunk && World)
    {
        Chunk = World->SpawnActor<AVoxelChunk>(AVoxelChunk::StaticClass(), Location, FRotator::ZeroRotator);
    }

    if (Chunk && Owner)
    {
        Chunk->SetOwner(Owner);
    }

    return Chunk;
}

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
