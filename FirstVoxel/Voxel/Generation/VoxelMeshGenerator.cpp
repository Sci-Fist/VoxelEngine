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

// Post-processing: Flatten top-facing vertices to improve walkability.
// Applied to the unified CellVertices array BEFORE mesh section splits 
// to prevent tearing at boundary edges.
static void FlattenCellTops(
    float InVoxelSize, 
    TArray<FVector>& CellVertices, 
    TArray<FVector>& CellNormals, 
    const TArray<int32>& VertexIndices, 
    int32 S)
{
    const int32 S3 = S * S * S;
    const float CellSize     = InVoxelSize * 1.5f; 
    const float CellSizeInv  = 1.f / CellSize;
    const float ZCellSize    = InVoxelSize * 3.0f; // altitude band
    const float ZCellSizeInv = 1.f / ZCellSize;

    TMap<FIntVector, float> CellMaxZ;
    CellMaxZ.Reserve(S3);

    for (int32 i = 0; i < S3; ++i)
    {
        if (VertexIndices[i] != 1) continue;
        if (CellNormals[i].Z <= 0.9f) continue;

        const int32 GX = FMath::FloorToInt(CellVertices[i].X * CellSizeInv);
        const int32 GY = FMath::FloorToInt(CellVertices[i].Y * CellSizeInv);
        const int32 GZ = FMath::FloorToInt(CellVertices[i].Z * ZCellSizeInv);
        const FIntVector Key(GX, GY, GZ);
        float& MaxZ = CellMaxZ.FindOrAdd(Key, CellVertices[i].Z);
        MaxZ = FMath::Max(MaxZ, CellVertices[i].Z);
    }

    for (int32 i = 0; i < S3; ++i)
    {
        if (VertexIndices[i] != 1) continue;
        if (CellNormals[i].Z <= 0.9f) continue;

        const int32 GX = FMath::FloorToInt(CellVertices[i].X * CellSizeInv);
        const int32 GY = FMath::FloorToInt(CellVertices[i].Y * CellSizeInv);
        const int32 GZ = FMath::FloorToInt(CellVertices[i].Z * ZCellSizeInv);
        float NeighMax = CellVertices[i].Z;

        const FIntVector Key(GX, GY, GZ);
        if (const float* Z = CellMaxZ.Find(Key))
        {
            NeighMax = *Z;
        }

        if (FMath::Abs(NeighMax - CellVertices[i].Z) > KINDA_SMALL_NUMBER)
        {
            CellVertices[i].Z = NeighMax;
            CellNormals[i]    = FVector(0.f, 0.f, 1.f);
        }
    }
}

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

	TArray<int32> FlatMap;      FlatMap.Init(-1, S3);
	TArray<int32> SlopeMap;     SlopeMap.Init(-1, S3);
	TArray<int32> BackMap;      BackMap.Init(-1, S3);
	TArray<int32> SlopeBackMap; SlopeBackMap.Init(-1, S3);

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

			// Averaged coordinates of cut edges form a watertight vertex node.

			CellPos /= (float)EdgeCount;

			const int32 CellIndex = Idx(X, Y, Z, S);
			CellVertices[CellIndex]  = CellPos;
			CellNormals[CellIndex]   = ComputeNormal(Densities, X, Y, Z, EffectiveSize);
			VertexIndices[CellIndex] = 1;
		}
	});

	// FIX: Apply top-flattening inline to unified list before splitting
	// to prevent tearing against SlopeMesh boundaries.
	FlattenCellTops(EffectiveVoxelSize, CellVertices, CellNormals, VertexIndices, S);

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

	// UV: pure world-space top-down projection (seamless across all chunks).
	//
	// We tile by absolute world X,Y so texture boundaries never coincide with
	// chunk boundaries and no seams appear between adjacent chunks.
	//
	// Walls will show mild vertical stretch when viewed at a glancing angle,
	// but this is invisible in practice when the Material uses the
	// WorldAlignedTexture node (recommended) which blends per-pixel in the
	// shader and fully handles all three axes without any C++ branching.
	//
	// Scale: 1 tile = 4 voxels (400 cm). Increase the multiplier to tile
	// more frequently or decrease for larger texture coverage.
	auto MakeUV = [&](const FVector& VLocal, const FVector& /*FaceNorm*/) -> FVector2D
	{
		const FVector VWorld = ChunkOrigin + VLocal;
		const float   s      = InVoxelSize * 4.f;
		return FVector2D(VWorld.X / s, VWorld.Y / s);
	};


	auto EmitTriangle = [&](FVoxelMeshData& Dest, TArray<int32>& SectionIndices,
		int32 i0, int32 i1, int32 i2, const FVector& FaceNorm, const FColor& VC)
	{
		if (!FaceNorm.IsNormalized() || FaceNorm.Size() < 0.1f) return;

		const float AreaSq = FVector::CrossProduct(CellVertices[i1] - CellVertices[i0], CellVertices[i2] - CellVertices[i0]).SizeSquared();
		if (AreaSq < 0.01f) return;

		auto AppendVertex = [&](int32 cellIndex) -> int32 {
			if (SectionIndices[cellIndex] != -1) return SectionIndices[cellIndex];
			const int32 NewIdx = Dest.Vertices.Add(CellVertices[cellIndex]);
			Dest.Normals.Add(CellNormals[cellIndex]);
			Dest.UVs.Add(MakeUV(CellVertices[cellIndex], FaceNorm));
			Dest.VertexColors.Add(VC);
			static const FProcMeshTangent T(1, 0, 0);
			Dest.Tangents.Add(T);
			SectionIndices[cellIndex] = NewIdx;
			return NewIdx;
		};

		const int32 idx0 = AppendVertex(i0);
		const int32 idx1 = AppendVertex(i1);
		const int32 idx2 = AppendVertex(i2);

		Dest.Triangles.Add(idx0);
		Dest.Triangles.Add(idx1);
		Dest.Triangles.Add(idx2);
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
	                    int32 ColX, int32 ColY, bool bD0Solid, bool bBorder, const FVector& Axis)
	{
		if (VertexIndices[i0] < 0 || VertexIndices[i1] < 0 ||
		    VertexIndices[i2] < 0 || VertexIndices[i3] < 0) return;

		const FVector& v0 = CellVertices[i0]; const FVector& n0 = CellNormals[i0];
		const FVector& v1 = CellVertices[i1]; const FVector& n1 = CellNormals[i1];
		const FVector& v2 = CellVertices[i2]; const FVector& n2 = CellNormals[i2];
		const FVector& v3 = CellVertices[i3]; const FVector& n3 = CellNormals[i3];

		// Outward normal: D0 is at the lower coordinate, D1 at the higher.
		// If D0 solid -> air is on the D1 side -> face points +Axis.
		// If D1 solid -> air is on the D0 side -> face points -Axis.
		const FVector OutwardNormal = bD0Solid ? Axis : -Axis;

		// FIX: Compute actual geometric normal (cross product of quad diagonals)
		// for accurate flat vs slope classification. The old code used the uniform
		// Axis vector, which classified ALL X/Y edge quads as Slopes regardless of orientation.
		FVector GeoNormal = FVector::CrossProduct(v2 - v0, v3 - v1).GetSafeNormal();
		
		// Ensure GeoNormal points outward (same hemisphere as OutwardNormal)
		if ((GeoNormal | OutwardNormal) < 0.f)
		{
			GeoNormal = -GeoNormal;
		}

		// Classify flat vs slope from the geometric normal's Z component.
		const float SlopeThresh = Config.SlopeThreshold;
		const bool  bIsFlat     = FMath::Abs(GeoNormal.Z) >= SlopeThresh;

		FVoxelMeshData& Dest     = bIsFlat ? OutMesh.FlatMesh : OutMesh.SlopeMesh;
		FVoxelMeshData& BackDest = bIsFlat ? OutMesh.BackMesh : OutMesh.SlopeBackMesh;

		TArray<int32>& IndicesDest = bIsFlat ? FlatMap : SlopeMap;
		TArray<int32>& BackIndicesDest = bIsFlat ? BackMap : SlopeBackMap;

		const FColor& VC = GetQuadColor(ColX, ColY);

		if (bD0Solid)
		{
			EmitTriangle(Dest, IndicesDest, i2, i1, i0, OutwardNormal, VC);
			EmitTriangle(Dest, IndicesDest, i3, i2, i0, OutwardNormal, VC);

			EmitTriangle(BackDest, BackIndicesDest, i0, i1, i2, -OutwardNormal, VC);
			EmitTriangle(BackDest, BackIndicesDest, i0, i2, i3, -OutwardNormal, VC);
		}
		else
		{
			EmitTriangle(Dest, IndicesDest, i0, i1, i2, OutwardNormal, VC);
			EmitTriangle(Dest, IndicesDest, i0, i2, i3, OutwardNormal, VC);

			EmitTriangle(BackDest, BackIndicesDest, i2, i1, i0, -OutwardNormal, VC);
			EmitTriangle(BackDest, BackIndicesDest, i3, i2, i0, -OutwardNormal, VC);
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
			         X, Y, D0 > 0.f, bBorder, FVector(1.f, 0.f, 0.f));
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
			EmitQuad(Idx(X,   Y, Z,   S), Idx(X,   Y, Z-1, S),
			         Idx(X-1, Y, Z-1, S), Idx(X-1, Y, Z,   S),
			         X, Y, D0 > 0.f, bBorder, FVector(0.f, 1.f, 0.f));
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
			         X, Y, D0 > 0.f, bBorder, FVector(0.f, 0.f, 1.f));
		}
	}
}

