// VoxelDataMap.cpp
#include "Core/VoxelDataMap.h"
#include "Misc/ScopeLock.h"

void FVoxelDataMap::Init(int32 InChunkSize)
{
	FScopeLock ScopeLock(&MapLock);
	ChunkSize = InChunkSize;
	Chunks.Empty();
}

void FVoxelDataMap::SetSphere(const FVector& WorldPos, float Radius, float Density, float VoxelSize, const FVector& Anchor)
{
	const FVector RelativePos = WorldPos - Anchor;
	const FIntVector Center = FIntVector(
		FMath::RoundToInt(RelativePos.X / VoxelSize),
		FMath::RoundToInt(RelativePos.Y / VoxelSize),
		FMath::RoundToInt(RelativePos.Z / VoxelSize)
	);

	const int32 RVox = FMath::CeilToInt(Radius / VoxelSize);

	// Build a local batch without holding the lock during the (potentially long)
	// nested loop.  This keeps GetChunkData() calls on background threads
	// unblocked while a player is digging.
	TMap<FIntVector, FChunkData> Batch;

	// Cache last-used chunk in the batch to avoid repeated TMap lookups for
	// the common case where many consecutive voxels belong to the same chunk.
	FIntVector CurrentChunkCoord(INT32_MAX, INT32_MAX, INT32_MAX);
	FChunkData* CurrentChunk = nullptr;

	for (int32 z = -RVox; z <= RVox; ++z)
	for (int32 y = -RVox; y <= RVox; ++y)
	for (int32 x = -RVox; x <= RVox; ++x)
	{
		const FIntVector Coord = Center + FIntVector(x, y, z);
		const FVector Pos = Anchor + FVector(Coord.X, Coord.Y, Coord.Z) * VoxelSize;
		const float Dist = FVector::Dist(WorldPos, Pos);

		if (Dist > Radius) continue;

		// Smooth SDF gradient: full density at centre, tapering to near-zero at edge.
		const float NormDist     = FMath::Clamp(Dist / Radius, 0.f, 1.f);
		float TargetDensity = Density * (1.f - NormDist);
		if (FMath::IsNearlyZero(TargetDensity))
			TargetDensity = (Density > 0.f) ? 0.001f : -0.001f;

		const FIntVector CC = GetChunkCoord(Coord);
		if (CC != CurrentChunkCoord || !CurrentChunk)
		{
			CurrentChunk      = &Batch.FindOrAdd(CC);
			CurrentChunkCoord = CC;
		}
		CurrentChunk->ModifiedVoxels.Add(GetLocalIndex(GetLocalCoord(Coord)), TargetDensity);
	}

	// Merge batch into the shared map under a single brief lock.
	FScopeLock ScopeLock(&MapLock);
	for (auto& Pair : Batch)
	{
		FChunkData& Dest = Chunks.FindOrAdd(Pair.Key);
		for (auto& VoxPair : Pair.Value.ModifiedVoxels)
			Dest.ModifiedVoxels.Add(VoxPair.Key, VoxPair.Value);
	}
}

void FVoxelDataMap::SetDensity(const FIntVector& GlobalCoord, float Density)
{
	const FIntVector ChunkCoord = GetChunkCoord(GlobalCoord);
	const int32 LocalIdx        = GetLocalIndex(GetLocalCoord(GlobalCoord));

	FScopeLock ScopeLock(&MapLock);
	
	FChunkData& ChunkData = Chunks.FindOrAdd(ChunkCoord);
	ChunkData.ModifiedVoxels.Add(LocalIdx, Density);
}

bool FVoxelDataMap::GetDensity(const FIntVector& GlobalCoord, float& OutDensity) const
{
	const FIntVector ChunkCoord = GetChunkCoord(GlobalCoord);
	const int32 LocalIdx        = GetLocalIndex(GetLocalCoord(GlobalCoord));

	FScopeLock ScopeLock(&MapLock);
	
	if (const FChunkData* ChunkData = Chunks.Find(ChunkCoord))
	{
		if (const float* FoundDensity = ChunkData->ModifiedVoxels.Find(LocalIdx))
		{
			OutDensity = *FoundDensity;
			return true;
		}
	}
	return false;
}

void FVoxelDataMap::Clear()
{
	FScopeLock ScopeLock(&MapLock);
	Chunks.Empty();
}

bool FVoxelDataMap::GetChunkData(const FIntVector& ChunkCoord, TMap<int32, float>& OutModified) const
{
	FScopeLock ScopeLock(&MapLock);
	if (const FChunkData* ChunkData = Chunks.Find(ChunkCoord))
	{
		OutModified = ChunkData->ModifiedVoxels;
		return true;
	}
	return false;
}

void FVoxelDataMap::Serialize(FArchive& Ar)
{
	FScopeLock ScopeLock(&MapLock);
	Ar << ChunkSize;
	int32 NumChunks = Chunks.Num();
	Ar << NumChunks;

	if (Ar.IsLoading())
	{
		Chunks.Empty(NumChunks);
		for (int32 i = 0; i < NumChunks; ++i)
		{
			FIntVector ChunkCoord;
			Ar << ChunkCoord;
			FChunkData& ChunkData = Chunks.Add(ChunkCoord);
			int32 NumModified;
			Ar << NumModified;
			for (int32 j = 0; j < NumModified; ++j)
			{
				int32 Index;
				float Density;
				Ar << Index << Density;
				ChunkData.ModifiedVoxels.Add(Index, Density);
			}
		}
	}
	else
	{
		for (auto& It : Chunks)
		{
			FIntVector ChunkCoord = It.Key;
			Ar << ChunkCoord;
			FChunkData& ChunkData = It.Value;
			int32 NumModified = ChunkData.ModifiedVoxels.Num();
			Ar << NumModified;
			for (auto& VoxelIt : ChunkData.ModifiedVoxels)
			{
				int32 Index = VoxelIt.Key;
				float Density = VoxelIt.Value;
				Ar << Index << Density;
			}
		}
	}
}

FIntVector FVoxelDataMap::GetChunkCoord(const FIntVector& GlobalCoord) const
{
	return FIntVector(
		FMath::FloorToInt((float)GlobalCoord.X / ChunkSize),
		FMath::FloorToInt((float)GlobalCoord.Y / ChunkSize),
		FMath::FloorToInt((float)GlobalCoord.Z / ChunkSize));
}

FIntVector FVoxelDataMap::GetLocalCoord(const FIntVector& GlobalCoord) const
{
	int32 X = GlobalCoord.X % ChunkSize; if (X < 0) X += ChunkSize;
	int32 Y = GlobalCoord.Y % ChunkSize; if (Y < 0) Y += ChunkSize;
	int32 Z = GlobalCoord.Z % ChunkSize; if (Z < 0) Z += ChunkSize;
	return FIntVector(X, Y, Z);
}

int32 FVoxelDataMap::GetLocalIndex(const FIntVector& LocalCoord) const
{
	return LocalCoord.X + LocalCoord.Y * ChunkSize + LocalCoord.Z * ChunkSize * ChunkSize;
}

void FVoxelDataMap::CopyFrom(const FVoxelDataMap& Other)
{
	if (this == &Other) return;

	// Order locks by memory address to prevent A-B/B-A deadlocks
	if (this < &Other)
	{
		MapLock.Lock();
		Other.MapLock.Lock();
	}
	else
	{
		Other.MapLock.Lock();
		MapLock.Lock();
	}

	ChunkSize = Other.ChunkSize;
	Chunks = Other.Chunks;

	MapLock.Unlock();
	Other.MapLock.Unlock();
}
