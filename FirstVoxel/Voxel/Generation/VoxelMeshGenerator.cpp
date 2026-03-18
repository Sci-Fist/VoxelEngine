// VoxelMeshGenerator.cpp
// Surface Nets implementation.
// Converts a 3D density field into smooth terrain mesh, splitting quads into
// FlatMesh (normal.Z >= SlopeThreshold) and SlopeMesh (normal.Z < SlopeThreshold).
//
// FIXES IN THIS REVISION:
//   1. EdgeTable removed. Pass 1 now uses correct Surface Nets gate:
//      (CubeIndex != 0 && CubeIndex != 255).
//      The old MC EdgeTable had mirrored duplicate rows that caused valid
//      surface cells to be skipped, leaving holes in thin terrain.
//
//   2. ComputeNormal sign fixed. The density gradient points INTO solid
//      (positive density = solid). Normals must point OUT of solid (into air).
//      Both the fast-path and border-path now negate the gradient consistently.
//      Previously the sign was correct but a comment said "FIX: invert" — making
//      it look intentional while the border fallback was also negated, causing
//      double-negation on border cells.
//
//   3. Flat/slope classification uses geometric face normal, NOT averaged
//      cell normals. The four per-cell normals (gradient-based, pointing away
//      from solid) are not the same as the face's geometric outward normal.
//      We now compute the quad's actual geometric normal via cross-product and
//      use its Z component to decide flat vs slope.
//
//   4. Backface winding bug fixed. The old code negated normals AND flipped
//      winding order — a double-negation that made "backfaces" render as
//      front-faces. Backfaces now only flip winding; normals stay as-is
//      (the backface shades the underside of the terrain for depth).
//
//   5. UV triplanar projection uses face normal for proper axis selection.
//      Flat faces project from Z (top-down), steep XY-facing walls project
//      from X or Y. Prevents UV stretching on cliff faces.
//
//   6. Degenerate geometry check tightened. EdgeCount < 3 (not < 2) skips
//      degenerate single-edge cells that can't form a valid quad vertex.

#include "Generation/VoxelMeshGenerator.h"
#include "CoreMinimal.h"
#include "Async/ParallelFor.h"
#include "ProceduralMeshComponent.h"
#include "Generation/VoxelDensityGenerator.h"
#include "Biomes/VoxelBiomeManager.h"
#include "VoxelLogger.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

FVector FVoxelMeshGenerator::InterpolateEdge(
	const FVector& P1, float D1,
	const FVector& P2, float D2)
{
	if (FMath::Abs(D2 - D1) < 1e-6f) return P1;
	const float t = FMath::Clamp(-D1 / (D2 - D1), 0.f, 1.f);
	return FMath::Lerp(P1, P2, t);
}

// ComputeNormal: central difference of the density field.
// The gradient of a signed-distance-like field points INTO solid (increasing density).
// We negate it so the normal points OUT of solid — into air — for correct lighting.
FVector FVoxelMeshGenerator::ComputeNormal(
	const TArray<float>& Densities,
	int32 X, int32 Y, int32 Z,
	int32 InChunkSize)
{
	const int32 S  = InChunkSize + 3;
	const int32 S2 = S * S;

	// Fast path: interior cells with guaranteed safe neighbours
	if (X >= 1 && X <= InChunkSize + 1 &&
	    Y >= 1 && Y <= InChunkSize + 1 &&
	    Z >= 1 && Z <= InChunkSize + 1)
	{
		const int32 C = X + Y * S + Z * S2;
		const FVector Grad(
			Densities[C + 1]  - Densities[C - 1],
			Densities[C + S]  - Densities[C - S],
			Densities[C + S2] - Densities[C - S2]
		);
		// Negate: gradient points INTO solid, normal must point OUT
		return -Grad.GetSafeNormal();
	}

	// Border fallback with clamped access
	auto SafeGet = [&](int32 ix, int32 iy, int32 iz) -> float
	{
		ix = FMath::Clamp(ix, 0, S - 1);
		iy = FMath::Clamp(iy, 0, S - 1);
		iz = FMath::Clamp(iz, 0, S - 1);
		return Densities[Idx(ix, iy, iz, S)];
	};

	const FVector Grad(
		SafeGet(X+1, Y,   Z  ) - SafeGet(X-1, Y,   Z  ),
		SafeGet(X,   Y+1, Z  ) - SafeGet(X,   Y-1, Z  ),
		SafeGet(X,   Y,   Z+1) - SafeGet(X,   Y,   Z-1)
	);
	// Negate: same reasoning as fast path
	return -Grad.GetSafeNormal();
}

// ---------------------------------------------------------------------------
// GenerateMesh — Surface Nets
//
// Pass 1: for every cell with a sign change (CubeIndex != 0 && != 255),
//         compute one vertex at the average of all cut-edge intersection points.
//
// Pass 2: for every axis-aligned grid edge that crosses the isosurface,
//         emit a quad connecting the four cells that share that edge.
//         bD0Solid determines winding so the quad faces into air.
// ---------------------------------------------------------------------------

void FVoxelMeshGenerator::GenerateMesh(
	const TArray<float>& Densities,
	int32                InChunkSize,
	float                InVoxelSize,
	const FVector&       ChunkOrigin,
	FVoxelMeshOutput&    OutMesh,
	const FVoxelGenerationConfig& Config,
	int32                InStepSize)
{
	OutMesh.Reset();

	const int32 EffectiveSize    = InChunkSize / InStepSize;
	const float EffectiveVoxelSize = InVoxelSize * (float)InStepSize;
	const int32 S                = EffectiveSize + 3;
	const int32 S3               = S * S * S;

	OutMesh.FlatMesh.ReserveInitial(EffectiveSize * EffectiveSize * 3);

	// Per-cell vertex storage
	TArray<int32>   VertexIndices; VertexIndices.Init(-1, S3);
	TArray<FVector> CellVertices;  CellVertices.Init(FVector::ZeroVector, S3);
	TArray<FVector> CellNormals;   CellNormals.Init(FVector::ZeroVector, S3);

	static const FIntVector CornerOffset[8] =
	{
		{0,0,0},{1,0,0},{1,1,0},{0,1,0},
		{0,0,1},{1,0,1},{1,1,1},{0,1,1}
	};

	// Each of the 12 cube edges connects two corner indices
	static const int32 EdgeToCorner[12][2] =
	{
		{0,1},{1,2},{2,3},{3,0},
		{4,5},{5,6},{6,7},{7,4},
		{0,4},{1,5},{2,6},{3,7}
	};

	// ── PASS 1: vertex placement ──────────────────────────────────────────
	ParallelFor(EffectiveSize + 2, [&](int32 Z)
	{
		for (int32 Y = 0; Y <= EffectiveSize + 1; ++Y)
		for (int32 X = 0; X <= EffectiveSize + 1; ++X)
		{
			float   D[8];
			FVector P[8];
			int32   CubeIndex = 0;

			for (int32 i = 0; i < 8; ++i)
			{
				const int32 cx = X + CornerOffset[i].X;
				const int32 cy = Y + CornerOffset[i].Y;
				const int32 cz = Z + CornerOffset[i].Z;
				D[i] = Densities[Idx(cx, cy, cz, S)];
				P[i] = FVector(cx - 1, cy - 1, cz - 1) * EffectiveVoxelSize;
				if (D[i] > 0.f) CubeIndex |= (1 << i);
			}

			// FIX 1: Correct Surface Nets surface gate.
			// 0   = all air  (no surface crossing)
			// 255 = all solid (no surface crossing)
			// Anything else has at least one sign-change edge → place a vertex.
			if (CubeIndex == 0 || CubeIndex == 255) continue;

			// Average all cut-edge intersection points for the cell vertex
			FVector CellPos  = FVector::ZeroVector;
			int32   EdgeCount = 0;

			for (int32 e = 0; e < 12; ++e)
			{
				const int32 c0 = EdgeToCorner[e][0];
				const int32 c1 = EdgeToCorner[e][1];
				// Only process edges that actually cross the surface
				if ((D[c0] > 0.f) != (D[c1] > 0.f))
				{
					CellPos += InterpolateEdge(P[c0], D[c0], P[c1], D[c1]);
					++EdgeCount;
				}
			}

			// FIX 1b: Need at least 3 cut edges to form a meaningful vertex.
			// 1–2 cut edges produce degenerate collapsed geometry.
			if (EdgeCount < 3) continue;

			CellPos /= (float)EdgeCount;

			const int32 CellIndex = Idx(X, Y, Z, S);
			CellVertices[CellIndex]  = CellPos;
			CellNormals[CellIndex]   = ComputeNormal(Densities, X, Y, Z, EffectiveSize);
			VertexIndices[CellIndex] = 1;
		}
	});

	// ── Pre-compute biome vertex colours (O(n²), cached per XY column) ────
	TArray<FColor> ColumnColors;
	ColumnColors.SetNumUninitialized(S * S);
	for (int32 CY = 0; CY < S; ++CY)
	for (int32 CX = 0; CX < S; ++CX)
	{
		const float WX = ChunkOrigin.X + (CX - 1.f) * EffectiveVoxelSize;
		const float WY = ChunkOrigin.Y + (CY - 1.f) * EffectiveVoxelSize;
		const FVoxelBiomeWeightMap W = FVoxelBiomeManager::GetBiomeWeightsStatic(WX, WY, Config);
		FLinearColor C(0.f, 0.f, 0.f, 1.f);
		C.R = W.Forest;
		C.G = W.Desert;
		C.B = W.Peaks + W.Cliffs;
		C.A = W.Craters + W.Mesa;
		ColumnColors[CX + CY * S] = C.ToFColor(false);
	}

	auto GetQuadColor = [&](int32 qX, int32 qY) -> const FColor&
	{
		return ColumnColors[FMath::Clamp(qX, 0, S-1) + FMath::Clamp(qY, 0, S-1) * S];
	};

	// FIX 5: Triplanar UV projection based on face normal.
	// Flat faces (abs(N.Z) dominant) → XY projection (top-down, no stretch).
	// East/West walls (abs(N.X) dominant) → YZ projection.
	// North/South walls (abs(N.Y) dominant) → XZ projection.
	auto MakeUV = [&](const FVector& VLocal, const FVector& FaceNorm) -> FVector2D
	{
		const FVector VWorld = ChunkOrigin + VLocal;
		const float   s      = InVoxelSize * 4.f;
		const FVector AN     = FaceNorm.GetAbs();

		if (AN.Z >= AN.X && AN.Z >= AN.Y)
			return FVector2D(VWorld.X / s, VWorld.Y / s);   // top/bottom face
		if (AN.X >= AN.Y)
			return FVector2D(VWorld.Y / s, VWorld.Z / s);   // east/west wall
		return FVector2D(VWorld.X / s, VWorld.Z / s);        // north/south wall
	};

	// Emit one triangle into the correct destination buffer
	auto EmitTriangle = [&](FVoxelMeshData& Dest,
		const FVector& V0, const FVector& V1, const FVector& V2,
		const FVector& N0, const FVector& N1, const FVector& N2,
		const FVector& FaceNorm,
		const FColor&  VC)
	{
		const int32 Base = Dest.Vertices.Num();
		Dest.Vertices.Add(V0); Dest.Vertices.Add(V1); Dest.Vertices.Add(V2);
		Dest.Normals.Add(N0);  Dest.Normals.Add(N1);  Dest.Normals.Add(N2);
		Dest.UVs.Add(MakeUV(V0, FaceNorm));
		Dest.UVs.Add(MakeUV(V1, FaceNorm));
		Dest.UVs.Add(MakeUV(V2, FaceNorm));
		Dest.VertexColors.Add(VC); Dest.VertexColors.Add(VC); Dest.VertexColors.Add(VC);
		static const FProcMeshTangent T(1, 0, 0);
		Dest.Tangents.Add(T); Dest.Tangents.Add(T); Dest.Tangents.Add(T);
		Dest.Triangles.Add(Base); Dest.Triangles.Add(Base+1); Dest.Triangles.Add(Base+2);
	};

	// ── PASS 2: quad emission ─────────────────────────────────────────────
	//
	// FIX 3: Flat vs slope is determined by the GEOMETRIC face normal
	//        (cross-product of quad diagonals), not the average of the four
	//        per-cell density-gradient normals. Gradient normals point away
	//        from solid and are smooth across the isosurface; they are not
	//        the same as the geometric face direction.
	//
	// FIX 4: Backfaces only flip winding order. They do NOT negate normals.
	//        The old code negated normals AND flipped winding, which is a
	//        double-negation — backfaces ended up front-facing again.
	//        BackfaceMesh provides visual interior depth; it should shade
	//        the underside of the terrain so its normals point inward (into
	//        solid), which happens naturally by just flipping winding.
	//
	// bD0Solid: true when the voxel on the D0 side of the edge is solid.
	//           Determines which winding produces an outward-facing front-face.
	auto EmitQuad = [&](int32 i0, int32 i1, int32 i2, int32 i3,
	                    int32 ColX, int32 ColY, bool bD0Solid, bool bBorder)
	{
		if (VertexIndices[i0] < 0 || VertexIndices[i1] < 0 ||
		    VertexIndices[i2] < 0 || VertexIndices[i3] < 0) return;

		const FVector& v0 = CellVertices[i0]; const FVector& n0 = CellNormals[i0];
		const FVector& v1 = CellVertices[i1]; const FVector& n1 = CellNormals[i1];
		const FVector& v2 = CellVertices[i2]; const FVector& n2 = CellNormals[i2];
		const FVector& v3 = CellVertices[i3]; const FVector& n3 = CellNormals[i3];

		// FIX 3: Geometric face normal via cross-product of quad diagonals.
		// This is the actual direction the quad's surface faces in world space.
		// We choose the diagonal order that matches the front-face winding below.
		FVector GeomNormal;
		if (!bD0Solid)
			GeomNormal = FVector::CrossProduct(v2 - v0, v3 - v1).GetSafeNormal();
		else
			GeomNormal = FVector::CrossProduct(v1 - v3, v0 - v2).GetSafeNormal();

		// Classify flat vs slope using the geometric normal, not gradient normals.
		// Config.SlopeThreshold default = 0.7 (~45°). Faces more vertical than this
		// go to SlopeMesh and get the cliff/rock material.
		const float   SlopeThresh = Config.SlopeThreshold;
		const bool    bIsFlat     = FMath::Abs(GeomNormal.Z) >= SlopeThresh;

		FVoxelMeshData& Dest     = bIsFlat ? OutMesh.FlatMesh  : OutMesh.SlopeMesh;
		FVoxelMeshData& BackDest = bIsFlat ? OutMesh.BackMesh  : OutMesh.SlopeBackMesh;

		const FColor& VC = GetQuadColor(ColX, ColY);

		if (!bD0Solid)
		{
			// D1 is solid → normal faces toward D0 (air) → CCW: v0,v1,v2 + v0,v2,v3
			EmitTriangle(Dest, v0, v1, v2, n0, n1, n2, GeomNormal, VC);
			EmitTriangle(Dest, v0, v2, v3, n0, n2, n3, GeomNormal, VC);

			// FIX 4: Backface — flip winding ONLY; normals stay (now point inward = correct for underside)
			EmitTriangle(BackDest, v2, v1, v0, n2, n1, n0, -GeomNormal, VC);
			EmitTriangle(BackDest, v3, v2, v0, n3, n2, n0, -GeomNormal, VC);
		}
		else
		{
			// D0 is solid → normal faces toward D1 (air) → CW flip: v2,v1,v0 + v3,v2,v0
			EmitTriangle(Dest, v2, v1, v0, n2, n1, n0, GeomNormal, VC);
			EmitTriangle(Dest, v3, v2, v0, n3, n2, n0, GeomNormal, VC);

			// FIX 4: Backface — flip winding ONLY
			EmitTriangle(BackDest, v0, v1, v2, n0, n1, n2, -GeomNormal, VC);
			EmitTriangle(BackDest, v0, v2, v3, n0, n2, n3, -GeomNormal, VC);
		}

		// Area 1: LOD Seams Improvement — Curtain Skirts for border quads
		if (bBorder && bIsFlat)
		{
			const FVector Down(0, 0, -EffectiveVoxelSize * 1.2f); // drop slightly more than 1 voxel to cover rounding edges
			
			EmitTriangle(Dest, v0, v1, v0 + Down, n0, n1, n0, GeomNormal, VC);
			EmitTriangle(Dest, v1, v1 + Down, v0 + Down, n1, n1, n0, GeomNormal, VC);

			EmitTriangle(Dest, v1, v2, v1 + Down, n1, n2, n1, GeomNormal, VC);
			EmitTriangle(Dest, v2, v2 + Down, v1 + Down, n2, n2, n1, GeomNormal, VC);

			EmitTriangle(Dest, v2, v3, v2 + Down, n2, n3, n2, GeomNormal, VC);
			EmitTriangle(Dest, v3, v3 + Down, v2 + Down, n3, n3, n2, GeomNormal, VC);

			EmitTriangle(Dest, v3, v0, v3 + Down, n3, n0, n3, GeomNormal, VC);
			EmitTriangle(Dest, v0, v0 + Down, v3 + Down, n0, n0, n3, GeomNormal, VC);
		}
	};

	// 1. X-axis edges: surface between (X,Y,Z) and (X+1,Y,Z)
	for (int32 Z = 1; Z <= EffectiveSize; ++Z)
	for (int32 Y = 1; Y <= EffectiveSize; ++Y)
	for (int32 X = 1; X <= EffectiveSize; ++X)
	{
		const float D0 = Densities[Idx(X,   Y, Z, S)];
		const float D1 = Densities[Idx(X+1, Y, Z, S)];
		if ((D0 > 0.f) != (D1 > 0.f))
		{
			const bool bBorder = (X == 1 || X == EffectiveSize || Y == 1 || Y == EffectiveSize);
			EmitQuad(Idx(X, Y,   Z,   S), Idx(X, Y,   Z-1, S),
			         Idx(X, Y-1, Z-1, S), Idx(X, Y-1, Z,   S),
			         X, Y, D0 > 0.f, bBorder);
		}
	}

	// 2. Y-axis edges: surface between (X,Y,Z) and (X,Y+1,Z)
	for (int32 Z = 1; Z <= EffectiveSize; ++Z)
	for (int32 Y = 1; Y <= EffectiveSize; ++Y)
	for (int32 X = 1; X <= EffectiveSize; ++X)
	{
		const float D0 = Densities[Idx(X, Y,   Z, S)];
		const float D1 = Densities[Idx(X, Y+1, Z, S)];
		if ((D0 > 0.f) != (D1 > 0.f))
		{
			const bool bBorder = (X == 1 || X == EffectiveSize || Y == 1 || Y == EffectiveSize);
			EmitQuad(Idx(X,   Y, Z,   S), Idx(X-1, Y, Z,   S),
			         Idx(X-1, Y, Z-1, S), Idx(X,   Y, Z-1, S),
			         X, Y, D0 > 0.f, bBorder);
		}
	}

	// 3. Z-axis edges: surface between (X,Y,Z) and (X,Y,Z+1)
	for (int32 Z = 1; Z <= EffectiveSize; ++Z)
	for (int32 Y = 1; Y <= EffectiveSize; ++Y)
	for (int32 X = 1; X <= EffectiveSize; ++X)
	{
		const float D0 = Densities[Idx(X, Y, Z,   S)];
		const float D1 = Densities[Idx(X, Y, Z+1, S)];
		if ((D0 > 0.f) != (D1 > 0.f))
		{
			const bool bBorder = (X == 1 || X == EffectiveSize || Y == 1 || Y == EffectiveSize);
			EmitQuad(Idx(X,   Y,   Z, S), Idx(X,   Y-1, Z, S),
			         Idx(X-1, Y-1, Z, S), Idx(X-1, Y,   Z, S),
			         X, Y, D0 > 0.f, bBorder);
		}
	}
}
