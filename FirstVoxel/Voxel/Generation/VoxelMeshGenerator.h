// =============================================================================
// VoxelMeshGenerator.h
// =============================================================================
//
// Stateless, thread-safe Surface Nets mesh builder.
// Converts a 3D density field into a ProceduralMesh-ready dataset.
//
// -- ALGORITHM OVERVIEW -------------------------------------------------------
//
//  Surface Nets (Gibson 1998) is a dual-contouring variant that produces
//  smooth meshes with correct topology:
//
//   PASS 1 -- Vertex placement
//     For every voxel cell that straddles the density isosurface (sign change
//     among 8 corners), compute one vertex at the average of all edge
//     intersection points. Runs via ParallelFor over the Z dimension.
//
//   PASS 2 -- Quad emission
//     For every axis-aligned grid edge that crosses the surface, emit a quad
//     connecting the vertices of the four sharing cells. Three loops handle
//     X-axis, Y-axis, and Z-axis edges respectively.
//
// -- WINDING ORDER & NORMALS --------------------------------------------------
//
//  Quads are single-sided. Winding is determined by the density sign of the
//  D0 voxel on each edge (bD0Solid parameter). This ensures normals always
//  point outward (into air), halving triangle count vs the old double-emit
//  approach which emitted both windings regardless of orientation.
//
//  Normals are computed by central differences on the density field, giving
//  smooth gradient-based normals that blend naturally across biome boundaries.
//
// -- VERTEX COLOR ENCODING ----------------------------------------------------
//
//  ColumnColors[] (one entry per XY column) is precomputed once in O(n^2)
//  and sampled per quad in O(1). Colors encode biome weights for material
//  blending in the terrain material graph:
//    R = Forest weight     G = Desert weight
//    B = Peaks + Cliffs    A = Craters + Mesa
//
// -- OUTPUT -------------------------------------------------------------------
//
//  FVoxelMeshOutput.FlatMesh  -- all quads in a single continuous section.
//  Uploaded to ProceduralMeshComponent section 0 in AVoxelChunk::UploadSection.
//
// -- THREAD SAFETY ------------------------------------------------------------
//
//  GenerateMesh() has no mutable state. Safe to call from multiple background
//  threads simultaneously (as during ParallelFor in BuildDensityField).
// =============================================================================
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
	TArray<FColor>           VertexColors; // Per-vertex biome blend for material tinting
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
	/** Section 0 — continuous mesh section containing all quads (with collision) */
	FVoxelMeshData FlatMesh;

	/** Section 1 — backfaces for visual double-sidedness (no collision) */
	FVoxelMeshData BackMesh;

	void Reset()
	{
		FlatMesh.Reset();
		BackMesh.Reset();
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
	 * @param OutMesh         Output — FlatMesh containing all vertices
	 */
	static void GenerateMesh(
		const TArray<float>& Densities,
		int32                InChunkSize,
		float                InVoxelSize,
		const FVector&       ChunkOrigin,
		FVoxelMeshOutput&    OutMesh,
		const struct FVoxelGenerationConfig& Config,
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
