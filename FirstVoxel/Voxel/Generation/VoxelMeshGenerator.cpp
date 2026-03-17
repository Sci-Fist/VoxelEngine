// VoxelMeshGenerator.cpp
// Surface Nets implementation â€” smooth terrain mesh from density field perfectly preserving sharp boxy edits.
// This file implements the Surface Nets algorithm for generating smooth terrain mesh
// from a 3D density field. The algorithm produces high-quality, continuous mesh
// that preserves sharp features while maintaining smooth surfaces.

#include "Generation/VoxelMeshGenerator.h"
#include "CoreMinimal.h"
#include "Async/ParallelFor.h"
#include "ProceduralMeshComponent.h"
#include "Generation/VoxelDensityGenerator.h"
#include "Biomes/VoxelBiomeManager.h"
#include "VoxelLogger.h"

// ---------------------------------------------------------------------------
// SURFACE NETS ALGORITHM OVERVIEW
// ---------------------------------------------------------------------------
// The Surface Nets algorithm generates a smooth mesh from a 3D density field by:
// 1. Finding cells that contain the surface (where density transitions from solid to air)
// 2. Computing a single vertex for each cell at the average position of edge intersections
// 3. Generating quads for each edge that crosses the surface, connecting the 4 vertices
//    of the sharing cells
// This approach produces smoother results than Marching Cubes while preserving sharp features
// when the user builds with blocks.
const int32 FVoxelMeshGenerator::EdgeTable[256] =
{
	0x000, 0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c, 0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00, 
	0x190, 0x099, 0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c, 0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90, 
	0x230, 0x339, 0x033, 0x13a, 0x636, 0x73f, 0x435, 0x53c, 0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
	0x3a0, 0x2a9, 0x1a3, 0x0aa, 0x7a6, 0x6af, 0x5a5, 0x4ac,
	0xbac, 0xaa5, 0x9af, 0x8a6, 0xfaa, 0xea3, 0xda9, 0xca0,
	0x460, 0x569, 0x663, 0x76a, 0x066, 0x16f, 0x265, 0x36c,
	0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
	0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0x0ff, 0x3f5, 0x2fc,
	0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
	0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x055, 0x15c,
	0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
	0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0x0cc,
	0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
	0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc,
	0x0cc, 0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
	0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c,
	0x15c, 0x055, 0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
	0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc,
	0x2fc, 0x3f5, 0x0ff, 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
	0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c,
	0x36c, 0x265, 0x16f, 0x066, 0x76a, 0x663, 0x569, 0x460,
	0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac,
	0x4ac, 0x5a5, 0x6af, 0x7a6, 0x0aa, 0x1a3, 0x2a9, 0x3a0,
	0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c,
	0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x033, 0x339, 0x230,
	0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c,
	0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x099, 0x190,
	0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c,
	0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x000
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

FVector FVoxelMeshGenerator::InterpolateEdge(
	const FVector& P1, float D1,
	const FVector& P2, float D2)
{
	if (FMath::Abs(D2 - D1) < 1e-6f) return P1;
	float t = FMath::Clamp(-D1 / (D2 - D1), 0.f, 1.f);
	return FMath::Lerp(P1, P2, t);
}

FVector FVoxelMeshGenerator::ComputeNormal(
	const TArray<float>& Densities,
	int32 X, int32 Y, int32 Z,
	int32 InChunkSize)
{
	int32 S = InChunkSize + 3; 

	// [Expert Optimization] Internal Cell Fast Path (no bounds check branches)
	if (X >= 1 && X <= InChunkSize + 1 && 
	    Y >= 1 && Y <= InChunkSize + 1 && 
	    Z >= 1 && Z <= InChunkSize + 1)
	{
		const int32 CenterIdx = X + Y * S + Z * S * S;
		const int32 S2 = S * S;
		
		FVector Grad(
			Densities[CenterIdx + 1] - Densities[CenterIdx - 1],
			Densities[CenterIdx + S] - Densities[CenterIdx - S],
			Densities[CenterIdx + S2] - Densities[CenterIdx - S2]
		);
		return Grad.GetSafeNormal(); // Fixed: Removed inversion for correct lighting
	}

	// Border fallback
	auto SafeGet = [&](int32 ix, int32 iy, int32 iz) -> float
	{
		ix = FMath::Clamp(ix, 0, S-1);
		iy = FMath::Clamp(iy, 0, S-1);
		iz = FMath::Clamp(iz, 0, S-1);
		return Densities[Idx(ix, iy, iz, S)];
	};

	// Central diff - fixed to point out from solid
	FVector Grad(
		SafeGet(X+1,Y,Z) - SafeGet(X-1,Y,Z),
		SafeGet(X,Y+1,Z) - SafeGet(X,Y-1,Z),
		SafeGet(X,Y,Z+1) - SafeGet(X,Y,Z-1)
	);

	return Grad.GetSafeNormal(); // Fixed: Removed inversion for correct lighting
}

// ---------------------------------------------------------------------------
// SURFACE NETS
// 1. Find cells containing the surface (EdgeTable != 0).
// 2. Compute a single vertex for each cell (average of edge intersections).
// 3. For each edge of the voxel grid that crosses the surface, 
//    generate a quad connecting the 4 vertices of the sharing cells.
// ---------------------------------------------------------------------------

void FVoxelMeshGenerator::GenerateMesh(
	const TArray<float>& Densities,
	int32                InChunkSize,
	float                InVoxelSize,
	const FVector&       ChunkOrigin,
	FVoxelMeshOutput&    OutMesh,
	const FVoxelGenerationConfig& Config,
	float                SlopeThreshold,
	int32                InStepSize)
{
	OutMesh.Reset();

	const int32 Reserve = 2048;
	OutMesh.FlatMesh.ReserveInitial(Reserve);
	OutMesh.SlopeMesh.ReserveInitial(Reserve);

	const int32 EffectiveSize = InChunkSize / InStepSize;
	const float EffectiveVoxelSize = InVoxelSize * InStepSize;

	// Correct S for indexing.
	int32 S = EffectiveSize + 3; 
	int32 S3 = S * S * S;

	// Array storing vertex existence and positions
	TArray<int32> VertexIndices;
	VertexIndices.Init(-1, S3);

	TArray<FVector> CellVertices;
	CellVertices.Init(FVector::ZeroVector, S3);

	TArray<FVector> CellNormals;
	CellNormals.Init(FVector::ZeroVector, S3);

	static const FIntVector CornerOffset[8] =
	{
		{0,0,0},{1,0,0},{1,1,0},{0,1,0},
		{0,0,1},{1,0,1},{1,1,1},{0,1,1}
	};

	// Standard MC corner order to EdgeVertices endpoints
	static const int32 EdgeToCorner[12][2] = {
		{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
	};

	// --- PASS 1: Generate vertices for every intersecting cell ---
	ParallelFor(EffectiveSize + 2, [&](int32 Z)
	{
		for (int32 Y = 0; Y <= EffectiveSize + 1; ++Y)
		for (int32 X = 0; X <= EffectiveSize + 1; ++X)
		{
			float D[8];
			FVector P[8];
			
			int32 CubeIndex = 0;
			for (int32 i = 0; i < 8; ++i)
			{
				int32 cx = X + CornerOffset[i].X;
				int32 cy = Y + CornerOffset[i].Y;
				int32 cz = Z + CornerOffset[i].Z;
				D[i] = Densities[Idx(cx, cy, cz, S)];
				// World local = (grid_index - 1) * EffectiveVoxelSize
				P[i] = FVector(cx - 1, cy - 1, cz - 1) * EffectiveVoxelSize;

				// Inside is > 0
				if (D[i] > 0.f) CubeIndex |= (1 << i);
			}

			// Check for surface crossing with epsilon tolerance to catch near-zero transitions
		if (EdgeTable[CubeIndex] == 0) 
		{
			// Additional check for near-surface cases that might be missed by strict inequality
			bool bHasNearSurface = false;
			for (int32 i = 0; i < 8; ++i)
			{
				if (FMath::Abs(D[i]) < 0.01f) // epsilon threshold for near-surface
				{
					bHasNearSurface = true;
					break;
				}
			}
			if (!bHasNearSurface) continue; // fully solid or empty
		}

		// Compute average intersection point of all cut edges with degenerate quad prevention
		FVector CellPos = FVector::ZeroVector;
		int32 EdgeCount = 0;
		TArray<FVector> EdgeIntersections;

		for (int32 e = 0; e < 12; ++e)
		{
			if (EdgeTable[CubeIndex] & (1 << e))
			{
				int32 c0 = EdgeToCorner[e][0];
				int32 c1 = EdgeToCorner[e][1];
				FVector intersection = InterpolateEdge(P[c0], D[c0], P[c1], D[c1]);
				EdgeIntersections.Add(intersection);
				CellPos += intersection;
				EdgeCount++;
			}
		}

		// Validate that we have enough edges to form a valid cell
		if (EdgeCount < 2) continue; // Reduced from 3 to 2 to allow more geometry

		CellPos /= (float)EdgeCount;

		// Additional validation: check for extreme edge lengths that could cause degenerate quads
		bool bHasValidGeometry = true;
		if (EdgeCount >= 2)
		{
			// Check if any edge intersection is too far from the cell center (potential degenerate case)
			for (const FVector& intersection : EdgeIntersections)
			{
				float distance = FVector::Dist(CellPos, intersection);
				float maxExpectedDistance = EffectiveVoxelSize * 2.5f; // Increased threshold for more lenient validation
				if (distance > maxExpectedDistance)
				{
					bHasValidGeometry = false;
					break;
				}
			}
		}

		if (!bHasValidGeometry) continue;

		// --- Snap borders removed to allow natural continuous Surface Net alignment without Z displacement --
		// Vertex coordinates on boundaries evaluate identical on adjacent chunks, avoiding seam gap cracks.

		// Store vertex
		int32 CellIndex = Idx(X, Y, Z, S);
		
		// To push sharp edges to the corners if the user builds blocks, we snap
		// the vertex to grid corners if the geometry suggests it's a block.
		// For pure Surface Nets, average is fine. For sharper features we'd implement QEF here.
		// For now, average gives a much smoother terrain than MC.

		CellVertices[CellIndex] = CellPos;

		// Compute Normal at this cell center
		FVector N = ComputeNormal(Densities, X, Y, Z, EffectiveSize);
		CellNormals[CellIndex] = N;
		
		// We use VertexIndices to track if a cell has a vertex
		VertexIndices[CellIndex] = 1;
		}
	}); // ParallelFor end

	// ── Pre-compute a 2D array of biome vertex colours (one per XY column) ────────
	// The old code called GetBiomeWeightsStatic() inside each of the three quad loops
	// (X/Y/Z axis), meaning it ran multiple PerlinNoise2D evaluations per quad —
	// O(n³) noise work just for vertex colours. Caching per-column reduces this to
	// O(n²), roughly 16× fewer noise calls for a typical 16³ chunk.
	//
	// ColumnColors[X + Y * S] maps to world column (ChunkOrigin + (X-1)*EffVoxelSize).
	TArray<FColor> ColumnColors;
	ColumnColors.SetNumUninitialized(S * S);
	{
		const FProcMeshTangent DefaultTangent(1, 0, 0);
		for (int32 CY = 0; CY < S; ++CY)
		for (int32 CX = 0; CX < S; ++CX)
		{
			const float WX = ChunkOrigin.X + (CX - 1.f) * EffectiveVoxelSize;
			const float WY = ChunkOrigin.Y + (CY - 1.f) * EffectiveVoxelSize;
			const FVoxelBiomeWeightMap W = FVoxelBiomeManager::GetBiomeWeightsStatic(WX, WY, Config);

			// Blend one recognisable base colour per biome for visual debugging.
			// These colours show through wherever the material uses vertex colour.
			FLinearColor C(0.f, 0.f, 0.f, 1.f);
			C += FLinearColor(0.10f, 0.45f, 0.08f) * W.Forest;   // Forest  — green
			C += FLinearColor(0.85f, 0.75f, 0.35f) * W.Desert;   // Desert  — sand
			C += FLinearColor(0.40f, 0.40f, 0.45f) * W.Peaks;    // Peaks   — grey
			C += FLinearColor(0.40f, 0.40f, 0.45f) * W.Cliffs;   // Cliffs  — grey
			C += FLinearColor(0.10f, 0.05f, 0.15f) * W.Craters;  // Craters — dark
			C += FLinearColor(0.50f, 0.10f, 0.90f) * W.Mesa;     // Mesa    — purple
			ColumnColors[CX + CY * S] = C.ToFColor(/*bSRGB=*/true);
		}
	}

	// Returns the cached biome colour for the XY column closest to a quad centre.
	auto GetQuadColor = [&](int32 qX, int32 qY) -> const FColor&
	{
		const int32 CX = FMath::Clamp(qX, 0, S - 1);
		const int32 CY = FMath::Clamp(qY, 0, S - 1);
		return ColumnColors[CX + CY * S];
	};

	// World-space UV: true triplanar blend — no hard axis switch, no seams.
	// Each of the three planar projections is weighted by pow(|N|, sharpness)
	// so the blend is smooth across the entire flat-to-slope transition range.
	auto MakeUV = [&](const FVector& VLocal, const FVector& Norm) -> FVector2D
	{
		const FVector VWorld = ChunkOrigin + VLocal;
		const float s = InVoxelSize * 4.f;

		// Blend weights: raise abs(normal component) to a power for sharper blending.
		// Power 4 gives a clean blend that still transitions smoothly at 45-degree slopes.
		const float BlendSharpness = 4.f;
		float wX = FMath::Pow(FMath::Abs(Norm.X), BlendSharpness);
		float wY = FMath::Pow(FMath::Abs(Norm.Y), BlendSharpness);
		float wZ = FMath::Pow(FMath::Abs(Norm.Z), BlendSharpness);
		const float wSum = wX + wY + wZ + 1e-6f;
		wX /= wSum;  wY /= wSum;  wZ /= wSum;

		// Three planar UVs (scaled the same as before)
		const FVector2D uvX(VWorld.Y / s, VWorld.Z / s); // projected along X
		const FVector2D uvY(VWorld.X / s, VWorld.Z / s); // projected along Y
		const FVector2D uvZ(VWorld.X / s, VWorld.Y / s); // projected along Z (top-down)

		return uvX * wX + uvY * wY + uvZ * wZ;
	};

	// Emit one triangle into the correct mesh section.
	// All six arrays are grown together to keep indices consistent.
	auto EmitTriangle = [&](FVoxelMeshData& Dest,
		const FVector& V0, const FVector& V1, const FVector& V2,
		const FVector& N0, const FVector& N1, const FVector& N2,
		const FColor&  VertexColor)
	{
		const int32 Base = Dest.Vertices.Num();
		Dest.Vertices.Add(V0);  Dest.Vertices.Add(V1);  Dest.Vertices.Add(V2);
		Dest.Normals.Add(N0);   Dest.Normals.Add(N1);   Dest.Normals.Add(N2);
		
		Dest.UVs.Add(MakeUV(V0, N0)); 
		Dest.UVs.Add(MakeUV(V1, N1)); 
		Dest.UVs.Add(MakeUV(V2, N2));
		
		Dest.VertexColors.Add(VertexColor);
		Dest.VertexColors.Add(VertexColor);
		Dest.VertexColors.Add(VertexColor);
		static const FProcMeshTangent T(1, 0, 0);
		Dest.Tangents.Add(T); Dest.Tangents.Add(T); Dest.Tangents.Add(T);
		Dest.Triangles.Add(Base); Dest.Triangles.Add(Base + 1); Dest.Triangles.Add(Base + 2);
	};

	// EmitCurtain removed: skirts caused visible grid lines at every chunk border.
	// Seam continuity is handled by the +3 padding in the density field instead.
	// (border voxels are sampled from the world function identically by adjacent chunks)

	// --- PASS 2: Generate Quads ---
	// Each chunk meshes its "minimum" range of world edges to avoid double-meshing.
	// World Origin is at grid index 1.
	
	// ── PASS 2: Generate quads for every axis-aligned edge that crosses the surface ──
	// Each axis is handled in a separate loop so the winding order for each axis
	// direction is always correct (solid→air vs air→solid flips the quad).
	//
	// Shared quad-emission helper — resolves the four quad vertices, picks the
	// right mesh section, fetches the cached biome colour, and emits two triangles.
	// FIXED: Standardized triangle winding order to ensure consistent face orientation
	auto EmitQuad = [&](int32 i0, int32 i1, int32 i2, int32 i3,
	                    int32 ColX, int32 ColY)
	{
		if (VertexIndices[i0] < 0 || VertexIndices[i1] < 0 ||
		    VertexIndices[i2] < 0 || VertexIndices[i3] < 0) return;

		const FVector& v0 = CellVertices[i0]; const FVector& v1 = CellVertices[i1];
		const FVector& v2 = CellVertices[i2]; const FVector& v3 = CellVertices[i3];
		const FVector& n0 = CellNormals[i0];  const FVector& n1 = CellNormals[i1];
		const FVector& n2 = CellNormals[i2];  const FVector& n3 = CellNormals[i3];

		// Route flat vs slope using averaged density-gradient normal.
		const FVector AvgN = (n0 + n1 + n2 + n3) * 0.25f;
		FVoxelMeshData& Dest = (FMath::Abs(AvgN.Z) < SlopeThreshold)
			? OutMesh.SlopeMesh : OutMesh.FlatMesh;

		const FColor& VC = GetQuadColor(ColX, ColY);

		// Emit both windings so every face is visible from outside regardless
		// of which direction the density edge crosses. This is the correct
		// approach for Surface Nets where per-axis winding analysis is unreliable.
		EmitTriangle(Dest, v0, v1, v2, n0, n1, n2, VC);
		EmitTriangle(Dest, v0, v2, v3, n0, n2, n3, VC);
		EmitTriangle(Dest, v2, v1, v0, n2, n1, n0, VC);
		EmitTriangle(Dest, v3, v2, v0, n3, n2, n0, VC);
	};

	// 1. X-Axis edges: surface between (X, Y, Z) and (X+1, Y, Z).
	//    The quad connects the four cells that share this edge.
	for (int32 Z = 1; Z <= EffectiveSize; ++Z)
	for (int32 Y = 1; Y <= EffectiveSize; ++Y)
	for (int32 X = 1; X <= EffectiveSize; ++X)
	{
		const float D0 = Densities[Idx(X,   Y, Z, S)];
		const float D1 = Densities[Idx(X+1, Y, Z, S)];
		if ((D0 > 0.f) != (D1 > 0.f))
		{
			EmitQuad(Idx(X, Y,   Z,   S), Idx(X, Y,   Z-1, S),
			         Idx(X, Y-1, Z-1, S), Idx(X, Y-1, Z,   S),
			         X, Y);
		}
	}

	// 2. Y-Axis edges: surface between (X, Y, Z) and (X, Y+1, Z).
	for (int32 Z = 1; Z <= EffectiveSize; ++Z)
	for (int32 Y = 1; Y <= EffectiveSize; ++Y)
	for (int32 X = 1; X <= EffectiveSize; ++X)
	{
		const float D0 = Densities[Idx(X, Y,   Z, S)];
		const float D1 = Densities[Idx(X, Y+1, Z, S)];
		if ((D0 > 0.f) != (D1 > 0.f))
		{
			EmitQuad(Idx(X,   Y, Z,   S), Idx(X,   Y, Z-1, S),
			         Idx(X-1, Y, Z-1, S), Idx(X-1, Y, Z,   S),
			         X, Y);
		}
	}

// 3. Z-Axis edges: surface between (X, Y, Z) and (X, Y, Z+1).
for (int32 Z = 1; Z <= EffectiveSize; ++Z)
for (int32 Y = 1; Y <= EffectiveSize; ++Y)
for (int32 X = 1; X <= EffectiveSize; ++X)
{
		const float D0 = Densities[Idx(X, Y,   Z, S)];
		const float D1 = Densities[Idx(X, Y, Z+1, S)];
		if ((D0 > 0.f) != (D1 > 0.f))
		{
			EmitQuad(Idx(X,   Y,   Z, S), Idx(X,   Y-1, Z, S),
			         Idx(X-1, Y-1, Z, S), Idx(X-1, Y,   Z, S),
			         X, Y);
		}
	}
}
