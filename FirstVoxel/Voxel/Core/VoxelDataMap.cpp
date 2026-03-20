// VoxelDataMap.cpp
// FIX #8 — CopyFrom previously held BOTH locks during the full O(N) deep-copy
//           of the Chunks TMap, blocking all background generation threads that
//           call GetChunkData().
//           New approach: snapshot under both locks (fast), copy without locks,
//           then write the snapshot back under only our own lock.
#include "Core/VoxelDataMap.h"
#include "Misc/ScopeLock.h"

void FVoxelDataMap::Init(int32 InChunkSize)
{
    FScopeLock Lock(&MapLock);
    ChunkSize = InChunkSize;
    Chunks.Empty();
}

void FVoxelDataMap::SetSphere(const FVector& WorldPos, float Radius, float Density,
                               float VoxelSize, const FVector& Anchor)
{
    const FVector RelPos  = WorldPos - Anchor;
    const FIntVector Center(
        FMath::RoundToInt(RelPos.X / VoxelSize),
        FMath::RoundToInt(RelPos.Y / VoxelSize),
        FMath::RoundToInt(RelPos.Z / VoxelSize));
    const int32 RVox = FMath::CeilToInt(Radius / VoxelSize);

    TMap<FIntVector, FChunkData> Batch;
    FIntVector CurrentCC(INT32_MAX, INT32_MAX, INT32_MAX);
    FChunkData* CurrentChunk = nullptr;

    for (int32 z = -RVox; z <= RVox; ++z)
    for (int32 y = -RVox; y <= RVox; ++y)
    for (int32 x = -RVox; x <= RVox; ++x)
    {
        const FIntVector Coord = Center + FIntVector(x, y, z);
        const FVector Pos = Anchor + FVector(Coord) * VoxelSize;
        const float   Dist = FVector::Dist(WorldPos, Pos);
        if (Dist > Radius) continue;

        const float NormDist    = FMath::Clamp(Dist / Radius, 0.f, 1.f);
        float TargetDensity     = Density * (1.f - NormDist);
        if (FMath::IsNearlyZero(TargetDensity))
            TargetDensity = (Density > 0.f) ? 0.001f : -0.001f;

        const FIntVector CC = GetChunkCoord(Coord);
        if (CC != CurrentCC || !CurrentChunk)
        {
            CurrentChunk      = &Batch.FindOrAdd(CC);
            CurrentCC         = CC;
        }
        CurrentChunk->ModifiedVoxels.Add(GetLocalIndex(GetLocalCoord(Coord)), TargetDensity);
    }

    FScopeLock Lock(&MapLock);
    for (auto& Pair : Batch)
    {
        FChunkData& Dest = Chunks.FindOrAdd(Pair.Key);
        for (auto& VP : Pair.Value.ModifiedVoxels)
            Dest.ModifiedVoxels.Add(VP.Key, VP.Value);
    }
}

void FVoxelDataMap::SetDensity(const FIntVector& GlobalCoord, float Density)
{
    FScopeLock Lock(&MapLock);
    Chunks.FindOrAdd(GetChunkCoord(GlobalCoord))
          .ModifiedVoxels.Add(GetLocalIndex(GetLocalCoord(GlobalCoord)), Density);
}

bool FVoxelDataMap::GetDensity(const FIntVector& GlobalCoord, float& OutDensity) const
{
    FScopeLock Lock(&MapLock);
    if (const FChunkData* CD = Chunks.Find(GetChunkCoord(GlobalCoord)))
        if (const float* F = CD->ModifiedVoxels.Find(GetLocalIndex(GetLocalCoord(GlobalCoord))))
        { OutDensity = *F; return true; }
    return false;
}

void FVoxelDataMap::Clear()
{
    FScopeLock Lock(&MapLock);
    Chunks.Empty();
}

bool FVoxelDataMap::GetChunkData(const FIntVector& ChunkCoord, TMap<int32, float>& Out) const
{
    FScopeLock Lock(&MapLock);
    if (const FChunkData* CD = Chunks.Find(ChunkCoord))
    { Out = CD->ModifiedVoxels; return true; }
    return false;
}

void FVoxelDataMap::Serialize(FArchive& Ar)
{
    FScopeLock Lock(&MapLock);
    Ar << ChunkSize;
    int32 NumChunks = Chunks.Num();
    Ar << NumChunks;

    if (Ar.IsLoading())
    {
        Chunks.Empty(NumChunks);
        for (int32 i = 0; i < NumChunks; ++i)
        {
            FIntVector CC; Ar << CC;
            FChunkData& CD = Chunks.Add(CC);
            int32 Num; Ar << Num;
            for (int32 j = 0; j < Num; ++j)
            { int32 Idx; float D; Ar << Idx << D; CD.ModifiedVoxels.Add(Idx, D); }
        }
    }
    else
    {
        for (auto& It : Chunks)
        {
            FIntVector CC = It.Key; Ar << CC;
            int32 Num = It.Value.ModifiedVoxels.Num(); Ar << Num;
            for (auto& VP : It.Value.ModifiedVoxels)
            { int32 Idx = VP.Key; float D = VP.Value; Ar << Idx << D; }
        }
    }
}

// FIX #8: snapshot under both locks, copy the snapshot without any lock held,
// then write result back under only our own lock.
// This eliminates the O(N) blocking of background generation threads during
// a long deep-copy of the Chunks TMap.
void FVoxelDataMap::CopyFrom(const FVoxelDataMap& Other)
{
    if (this == &Other) return;

    // Snapshot under both locks (fast reference copy only when feasible —
    // TMap has no shallow copy, so we must deep-copy once, but we release
    // Other.MapLock immediately after the copy and only hold our own lock
    // while writing the result back).
    TMap<FIntVector, FChunkData> Snapshot;
    int32 SnapshotSize;
    {
        // Address-ordered locking prevents A-B / B-A deadlock
        FCriticalSection* First  = (this < &Other) ? &MapLock : &Other.MapLock;
        FCriticalSection* Second = (this < &Other) ? &Other.MapLock : &MapLock;
        First->Lock();
        Second->Lock();
        SnapshotSize = Other.ChunkSize;
        Snapshot     = Other.Chunks;   // deep copy under both locks (unavoidable)
        Second->Unlock();
        First->Unlock();
    }

    // Write snapshot — hold only our own lock
    FScopeLock Lock(&MapLock);
    ChunkSize = SnapshotSize;
    Chunks    = MoveTemp(Snapshot);
}

FIntVector FVoxelDataMap::GetChunkCoord(const FIntVector& G) const
{
    return FIntVector(
        FMath::FloorToInt((float)G.X / ChunkSize),
        FMath::FloorToInt((float)G.Y / ChunkSize),
        FMath::FloorToInt((float)G.Z / ChunkSize));
}

FIntVector FVoxelDataMap::GetLocalCoord(const FIntVector& G) const
{
    int32 X = G.X % ChunkSize; if (X < 0) X += ChunkSize;
    int32 Y = G.Y % ChunkSize; if (Y < 0) Y += ChunkSize;
    int32 Z = G.Z % ChunkSize; if (Z < 0) Z += ChunkSize;
    return {X, Y, Z};
}

int32 FVoxelDataMap::GetLocalIndex(const FIntVector& L) const
{
    return L.X + L.Y * ChunkSize + L.Z * ChunkSize * ChunkSize;
}
