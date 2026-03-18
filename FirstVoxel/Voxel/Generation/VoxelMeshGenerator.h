// =============================================================================
// VoxelMeshGenerator.h
// =============================================================================
//
// Stateless, thread-safe Surface Nets mesh builder.
// Converts a 3D density field into a ProceduralMesh-ready dataset split into
// two material sections: FlatMesh (top-facing) and SlopeMesh (cliff/wall).
//
// -- ALGORITHM ----------------------------------------------------------------
//
//  PASS 1 — Vertex placement
//    For every cell whose 8 corners contain a density sign change
//    (CubeIndex != 0 && CubeIndex != 255), compute one vertex at the average
//    of all cut-edge intersection points. Run via ParallelFor over Z slices.
//
//  PASS 2 — Quad emission
//    For every axis-aligned edge that crosses the isosurface, emit a quad
//    connecting the four cells sharing that edge. Three loops cover X, Y, Z
//    edges. bD0Solid determines winding so the front face always points into air.
//
// -- FLAT vs SLOPE CLASSIFICATION ---------------------------------------------
//
//  Each quad's geometric face normal (cross-product of diagonals) is tested
//  against Config.SlopeThreshold (default 0.7 ≈ 45°):
//    abs(GeomNormal.Z) >= SlopeThreshold → FlatMesh  (grass / dirt material)
//    abs(GeomNormal.Z) <  SlopeThreshold → SlopeMesh (cliff / rock material)
//
//  The classification uses the GEOMETRIC normal, not the averaged per-cell
//  density-gradient normals. Gradient normals are smooth interpolants for
//  lighting; they do not reliably indicate the face's actual orientation.
//
// -- NORMALS ------------------------------------------------------------------
//
//  ComputeNormal() returns the negative density gradient (central differences),
//  which points OUT of solid (into air) — correct for outward-facing normals.
//  Backface triangles keep the same per-vertex normals but have flipped winding
//  so they shade the underside of the terrain from inside.
//
// -- VERTEX COLOR ENCODING ----------------------------------------------------
//
//  ColumnColors[] (one entry per XY column) is precomputed once in O(n²).
//    R = Forest   G = Desert   B = Peaks+Cliffs   A = Craters+Mesa
//
// -- UV PROJECTION ------------------------------------------------------------
//
//  MakeUV() selects the projection axis from the geometric face normal:
//    Top/bottom faces   → XY projection (no stretch on flat ground)
//    East/West walls    → YZ projection
//    North/South walls  → XZ projection
//
// -- THREAD SAFETY ------------------------------------------------------------
//
//  GenerateMesh() is fully stateless. Safe to call from multiple background
//  threads simultaneously.
// =============================================================================
#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

// ---------------------------------------------------------------------------
// FVoxelMeshData — geometry for one ProceduralMesh section
// ---------------------------------------------------------------------------
struct FVoxelMeshData
{
	TArray<FVector>          Vertices;
	TArray<int32>            Triangles;
	TArray<FVector>          Normals;
	TArray<FVector2D>        UVs;
	TArray<FColor>           VertexColors;
	TArray<FProcMeshTangent> Tangents;

	void Reset()
	{
		Vertices.Reset(); Triangles.Reset(); Normals.Reset();
		UVs.Reset(); VertexColors.Reset(); Tangents.Reset();
	}

	bool IsEmpty() const { return Vertices.Num() == 0; }

	void ReserveInitial(int32 N)
	{
		Vertices.Reserve(N); Triangles.Reserve(N); Normals.Reserve(N);
		UVs.Reserve(N); VertexColors.Reserve(N); Tangents.Reserve(N);
	}
};

// ---------------------------------------------------------------------------
// FVoxelMeshOutput — four sections from one GenerateMesh call
// ---------------------------------------------------------------------------
struct FVoxelMeshOutput
{
	/** Section 0 — flat terrain (normal.Z >= SlopeThreshold). Collision on. */
	FVoxelMeshData FlatMesh;

	/** Section 1 — steep slopes/cliffs (normal.Z < SlopeThreshold). Collision on. */
	FVoxelMeshData SlopeMesh;

	/** Backface mirror of FlatMesh — visual only, no collision. */
	FVoxelMeshData BackMesh;

	/** Backface mirror of SlopeMesh — visual only, no collision. */
	FVoxelMeshData SlopeBackMesh;

	void Reset()
	{
		FlatMesh.Reset(); SlopeMesh.Reset();
		BackMesh.Reset(); SlopeBackMesh.Reset();
	}
};

// ---------------------------------------------------------------------------
// FVoxelMeshGenerator — stateless, thread-safe mesh builder
// ---------------------------------------------------------------------------
struct FVoxelMeshGenerator
{
	/**
	 * Run Surface Nets on a density field and fill OutMesh.
	 *
	 * @param Densities     Flat array, size = (ChunkSize/StepSize + 3)^3.
	 *                      Index: X + Y*S + Z*S*S  where S = ChunkSize/StepSize + 3.
	 * @param InChunkSize   Voxels per axis (e.g. 16 or 32).
	 * @param InVoxelSize   World-space size of one voxel in cm (e.g. 100).
	 * @param ChunkOrigin   World position of local voxel [0,0,0].
	 * @param OutMesh       Receives the generated geometry.
	 * @param Config        Generation config — SlopeThreshold used for flat/slope split.
	 * @param InStepSize    LOD step — 1 = full res, 2 = half res, etc.
	 */
	static void GenerateMesh(
		const TArray<float>&          Densities,
		int32                         InChunkSize,
		float                         InVoxelSize,
		const FVector&                ChunkOrigin,
		FVoxelMeshOutput&             OutMesh,
		const struct FVoxelGenerationConfig& Config,
		int32                         InStepSize = 1);

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
};
