// =============================================================================
// VoxelDataMap.h
// =============================================================================
//
// Sparse store for player-driven voxel density overrides.
// Maps global voxel coordinates to float density values that override the
// procedural density generator during chunk regeneration.
//
// -- DATA LAYOUT --------------------------------------------------------------
//
//  Chunks   TMap<FIntVector, FChunkData>   one entry per modified chunk
//  FChunkData.ModifiedVoxels  TMap<int32, float>  flat local index -> density
//
//  Flat local index = LX + LY*ChunkSize + LZ*ChunkSize^2
//  where (LX,LY,LZ) = GlobalCoord mod ChunkSize (always >= 0).
//
// -- HOW OVERRIDES ARE APPLIED ------------------------------------------------
//
//  Before FVoxelGeneratorTask::BuildDensityField() starts, it calls
//  GetChunkData() to snapshot the relevant TMap into a dense float array
//  (DenseEdits[]). During the Z loop, each voxel's index is looked up in
//  DenseEdits in O(1):
//    Override < 0  -> D = min(ProceduralD, Override)   (carve)
//    Override > 0  -> D = max(ProceduralD, Override)   (fill)
//
// -- THREAD SAFETY ------------------------------------------------------------
//
//  MapLock (FCriticalSection) protects all map mutations.
//  SetSphere() builds a local batch WITHOUT holding MapLock so that
//  background GetChunkData() calls on generation threads are not blocked
//  during the O(R^3) sphere iteration. The lock is acquired once at the
//  end to merge the batch into Chunks.
//
//  CopyFrom() acquires both locks in address order to prevent A-B/B-A
//  deadlocks when two maps copy each other concurrently.
//
// -- SERIALIZATION ------------------------------------------------------------
//
//  Serialize(FArchive&) supports both save (IsLoading=false) and load.
//  Format: [ChunkSize int32] [NumChunks int32]
//          for each chunk: [ChunkCoord FIntVector] [NumModified int32]
//                          for each voxel: [LocalIdx int32] [Density float]
// =============================================================================
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

	/** Set overrides in a spherical radius. Pass World Anchor for aligning sparse lookup keys. */
	void SetSphere(const FVector& WorldPos, float Radius, float Density, float VoxelSize, const FVector& Anchor = FVector::ZeroVector);


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
