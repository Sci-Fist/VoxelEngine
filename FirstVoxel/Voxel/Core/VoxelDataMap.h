#pragma once

#include "CoreMinimal.h"
#include "FirstVoxel.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

struct FIRSTVOXEL_API FVoxelDataMap
{
public:
	void Init(int32 InChunkSize);

	/** Set a density override at a global voxel coordinate. Positive = solid, Negative = air. */
	void SetDensity(const FIntVector& GlobalCoord, float Density);

	/** Set overrides in a spherical radius */
	void SetSphere(const FVector& WorldPos, float Radius, float Density, float VoxelSize);

	/** 
	 * Retrieve a density override.
	 * Returns true if an override exists for this coordinate, setting OutDensity.
	 */
	bool GetDensity(const FIntVector& GlobalCoord, float& OutDensity) const;

	/** Clear all overrides */
	void Clear();

	/** 
	 * Snaps a copy of all modified voxels for a specific chunk.
	 * Used to avoid per-voxel locking during density generation.
	 */
	bool GetChunkData(const FIntVector& ChunkCoord, TMap<int32, float>& OutModified) const;

	/** Binary serialization for Saving/Loading */
	void Serialize(FArchive& Ar);

	/** Bulk copy overrides from another map */
	void CopyFrom(const FVoxelDataMap& Other);

	struct FChunkData
	{
		TMap<int32, float> ModifiedVoxels;
	};

	const TMap<FIntVector, FChunkData>& GetChunks() const { return Chunks; }

private:
	int32 ChunkSize = 32;

	TMap<FIntVector, FChunkData> Chunks;


	mutable FCriticalSection MapLock;

	FIntVector GetChunkCoord(const FIntVector& GlobalCoord) const;
	FIntVector GetLocalCoord(const FIntVector& GlobalCoord) const;
	int32      GetLocalIndex(const FIntVector& LocalCoord) const;
};
