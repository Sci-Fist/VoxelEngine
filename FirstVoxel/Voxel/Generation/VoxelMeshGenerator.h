// VoxelMeshGenerator.h
// Surface Nets implementation — produces two separate mesh sections per chunk:
//   Section 0 (FlatMesh)  — near-horizontal faces → grass / dirt material
//   Section 1 (SlopeMesh) — steep faces           → rock / cliff material
// The split is driven by the face's averaged Normal.Z vs SlopeThreshold.
#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

// ---------------------------------------------------------------------------
// FVoxelMeshData — data for ONE ProceduralMesh section
// ---------------------------------------------------------------------------
struct FVoxelMeshData
{
	TArray<FVector>          Vertices;
	TArray<int32>            Triangles;
	TArray<FVector>          Normals;
	TArray<FVector2D>        UVs;
	TArray<FColor>           VertexColors; // R = AO hint, G = height blend
	TArray<FProcMeshTangent> Tangents;

	void Reset()
	{
		Vertices.Reset();
		Triangles.Reset();
		Normals.Reset();
		UVs.Reset();
		VertexColors.Reset();
		Tangents.Reset();
	}

	bool IsEmpty() const { return Vertices.Num() == 0; }

	void ReserveInitial(int32 N)
	{
		Vertices.Reserve(N);
		Triangles.Reserve(N);
		Normals.Reserve(N);
		UVs.Reserve(N);
		VertexColors.Reserve(N);
		Tangents.Reserve(N);
	}
};

// ---------------------------------------------------------------------------
// FVoxelMeshOutput — two sections produced by one GenerateMesh call
// ---------------------------------------------------------------------------
struct FVoxelMeshOutput
{
	/** Section 0 — flat / near-horizontal faces (grass, dirt, snow) */
	FVoxelMeshData FlatMesh;

	/** Section 1 — steep / cliff faces (rock, shale) */
	FVoxelMeshData SlopeMesh;

	void Reset()
	{
		FlatMesh.Reset();
		SlopeMesh.Reset();
	}
};

// ---------------------------------------------------------------------------
// FVoxelMeshGenerator — stateless, thread-safe
// ---------------------------------------------------------------------------
struct FVoxelMeshGenerator
{
	/**
	 * Run Surface Nets on a density field and fill OutMesh.
	 *
	 * @param Densities       Flat array of size (ChunkSize+3)^3.
	 *                        Index: X + Y*(ChunkSize+3) + Z*(ChunkSize+3)^2
	 * @param ChunkSize       Voxels per axis (e.g. 32)
	 * @param VoxelSize       World-space size of one voxel in cm (e.g. 100)
	 * @param ChunkOrigin     World position of voxel [0,0,0] in this chunk
	 * @param OutMesh         Output — FlatMesh (sec0) + SlopeMesh (sec1)
	 * @param SlopeThreshold  Normal.Z threshold. Faces with |Normal.Z| >= this
	 *                        value go into FlatMesh; steeper faces go into SlopeMesh.
	 *                        0.7 ≈ 45°  0.5 ≈ 60°  (default 0.7)
	 */
	static void GenerateMesh(
		const TArray<float>& Densities,
		int32                InChunkSize,
		float                InVoxelSize,
		const FVector&       ChunkOrigin,
		FVoxelMeshOutput&    OutMesh,
		const struct FVoxelGenerationConfig& Config,
		float                SlopeThreshold = 0.7f,
		int32                InStepSize = 1);

private:
	static FVector InterpolateEdge(
		const FVector& P1, float D1,
		const FVector& P2, float D2);

	static FVector ComputeNormal(
		const TArray<float>& Densities,
		int32 X, int32 Y, int32 Z,
		int32 InChunkSize);

	static FORCEINLINE int32 Idx(int32 X, int32 Y, int32 Z, int32 S)
	{
		return X + Y * S + Z * S * S;
	}

	// Surface nets uses edges from the grid, defined by intersections.
	static const int32 EdgeTable[256];

};
