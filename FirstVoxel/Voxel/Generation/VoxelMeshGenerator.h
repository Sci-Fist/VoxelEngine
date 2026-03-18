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
//    edges. bD0Solid + Axis determine winding so the front face always points
//    into air (see WINDING ORDER below).
//
// -- WINDING ORDER ------------------------------------------------------------
//
//  The canonical quad vertex order (i0, i1, i2, i3) is constructed so that
//  CrossProduct(v1-v0, v2-v0) points in the -Axis direction for ideal grid
//  vertices.  Therefore:
//
//    bD0Solid = true  (D0 solid, air on D1 side) → face must point +Axis
//               → emit (v2, v1, v0)   REVERSED winding
//    bD0Solid = false (D1 solid, air on D0 side) → face must point -Axis
//               → emit (v0, v1, v2)   CANONICAL winding
//
//  This rule is DETERMINISTIC — it does not use the actual vertex positions.
//  Previous code used CrossProduct on actual positions, which failed on
//  curved terrain because Surface Nets vertices deviate from grid centres
//  enough to flip the cross-product sign, randomly back-facing some quads
//  ("checkerboard on slopes" artifact).
//
// -- FLAT vs SLOPE CLASSIFICATION ---------------------------------------------
//
//  Each quad's outward normal is known from Axis + bD0Solid (see above).
//  This is tested against Config.SlopeThreshold (default 0.7 ≈ 45°):
//    abs(OutwardNormal.Z) >= SlopeThreshold → FlatMesh  (grass / dirt material)
//    abs(OutwardNormal.Z) <  SlopeThreshold → SlopeMesh (cliff / rock material)
//
// -- NORMALS ------------------------------------------------------------------
//
//  ComputeNormal() returns the negative density gradient (central differences),
//  which points OUT of solid (into air) — correct for smooth vertex normals.
//  These are per-vertex lighting normals, not the geometric face normal used
//  for winding/classification.
//  Backface triangles flip winding only; normals are preserved pointing inward
//  (into solid) so the underside of terrain shades correctly from inside.
//
// -- VERTEX COLOR ENCODING ----------------------------------------------------
//
//  ColumnColors[] (one entry per XY column) is precomputed once in O(n²).
//    R = Forest   G = Desert   B = Peaks+Cliffs   A = Craters+Mesa
//
// -- UV PROJECTION ------------------------------------------------------------
//
//  MakeUV() selects the projection axis from the outward normal:
//    Top/bottom faces   → XY projection (no stretch on flat ground)
//    East/West walls    → YZ projection
//    North/South walls  → XZ projection
//
// -- MESH POST-PROCESSING -----------------------------------------------------
//
//  FlattenMeshTops() snaps top-facing vertices (normal.Z > 0.9) to the
//  local neighbourhood maximum Z.  Improves walkability by eliminating slight
//  height variation that causes non-walkable floor normals.
//
//  Implementation: O(V) 2D spatial grid (bucket map keyed on grid cell).
//  Pass 1 builds max-Z per cell; Pass 2 looks up each vertex's 3×3 cells
//  (9 lookups) and snaps.  The old O(V²) brute-force search caused
//  ~48 M comparisons when 12 background tasks ran concurrently.
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

	/**
	 * Post-process FlatMesh to ensure top-facing surfaces are truly horizontal.
	 * Vertices with normal.Z > 0.9 are considered "top" faces. Their Z position
	 * is adjusted to match the highest Z in their local 3x3 neighborhood, and
	 * normals are recomputed to point upward. This improves walkability on
	 * voxel terrain by eliminating edge artifacts that cause non-walkable floor normals.
	 *
	 * @param InVoxelSize   World-space size of one voxel (for tolerance calculations)
	 * @param OutMesh       Mesh data to post-process (modifies FlatMesh in-place)
	 */
	static void FlattenMeshTops(float InVoxelSize, FVoxelMeshOutput& OutMesh);

private:
	static FVector InterpolateEdge(
		const FVector& P1, float D1,
		const FVector& P2, float D2);

public:
	static FVector ComputeNormal(
		const TArray<float>& Densities,
		int32 X, int32 Y, int32 Z,
		int32 InChunkSize);

	static FORCEINLINE int32 Idx(int32 X, int32 Y, int32 Z, int32 S)
	{
		return X + Y * S + Z * S * S;
	}
};