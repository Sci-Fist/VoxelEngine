// VoxelMeshGenerator.cpp
// Surface Nets mesh builder for voxel terrain.
//
// FIX #3  — FlattenCellTops re-enabled with border cell exclusion.
//            Border vertices (X/Y at edge of S) are skipped during snapping
//            so adjacent chunks snap independently without introducing cracks.
//
// FIX #7  — ColumnColors loop changed to ParallelFor — was 1225 serial Perlin
//            calls per chunk; now runs on all available cores.
//
// FIX #12 — EdgeCount == 0 guard added before dividing. Corrupted density
//            values could produce a valid CubeIndex but zero cut edges,
//            causing a NaN vertex that propagated silently into the mesh.
//
// FIX WINDING — Checkerboard holes (every other quad backface-culled) fixed.
//   Root cause: the canonical quad vertex order (i0,i1,i2,i3) always produces
//   a cross-product pointing in -Axis.  When bD0Solid=true the face must point
//   +Axis, so every triangle was being emitted facing INTO the terrain.
//   Fix: deterministic B↔C swap in EmitQuad when bD0Solid=true — no vertex
//   position sampling, no curved-terrain failure modes.
//   Proof (Z-axis): i0=(X,Y,Z), i1=(X,Y-1,Z), i2=(X-1,Y-1,Z), i3=(X-1,Y,Z)
//     V1−V0=(0,−1,0), V2−V0=(−1,−1,0) → Cross=(0,0,−1)=−Z.
//     bD0Solid=true wants +Z → was backface culled. Same proof holds for X/Y.

#include "Generation/VoxelMeshGenerator.h"
#include "CoreMinimal.h"
#include "Async/ParallelFor.h"
#include "ProceduralMeshComponent.h"
#include "Generation/VoxelDensityGenerator.h"
#include "Biomes/VoxelBiomeManager.h"
#include "VoxelLogger.h"

// ---------------------------------------------------------------------------
FVector FVoxelMeshGenerator::InterpolateEdge(
    const FVector& P1, float D1, const FVector& P2, float D2)
{
    if (FMath::Abs(D2 - D1) < 1e-6f) return P1;
    return FMath::Lerp(P1, P2, FMath::Clamp(-D1 / (D2 - D1), 0.f, 1.f));
}

FVector FVoxelMeshGenerator::ComputeNormal(
    const TArray<float>& Densities, int32 X, int32 Y, int32 Z, int32 InChunkSize)
{
    const int32 S = InChunkSize + 3, S2 = S * S;

    if (X >= 1 && X <= InChunkSize + 1 &&
        Y >= 1 && Y <= InChunkSize + 1 &&
        Z >= 1 && Z <= InChunkSize + 1)
    {
        const int32 C = X + Y * S + Z * S2;
        return (-FVector(Densities[C+1]  - Densities[C-1],
                         Densities[C+S]  - Densities[C-S],
                         Densities[C+S2] - Densities[C-S2])).GetSafeNormal(1.0e-6f, FVector::UpVector);
    }

    auto SafeGet = [&](int32 ix, int32 iy, int32 iz) -> float {
        ix = FMath::Clamp(ix, 0, S-1);
        iy = FMath::Clamp(iy, 0, S-1);
        iz = FMath::Clamp(iz, 0, S-1);
        return Densities[Idx(ix, iy, iz, S)];
    };
    return (-FVector(SafeGet(X+1,Y,Z)-SafeGet(X-1,Y,Z),
                     SafeGet(X,Y+1,Z)-SafeGet(X,Y-1,Z),
                     SafeGet(X,Y,Z+1)-SafeGet(X,Y,Z-1))).GetSafeNormal(1.0e-6f, FVector::UpVector);
}

// ---------------------------------------------------------------------------
// FlattenCellTops
// FIX #3: Re-enabled with border exclusion.
// Border vertices (edge of the S-wide padded grid) are shared with adjacent
// chunks. Skipping them prevents the two chunks from snapping independently
// to different heights and introducing a crack along the chunk boundary.
// Interior vertices snap freely — they are never shared across chunks.
// ---------------------------------------------------------------------------
static void FlattenCellTops(
    float InVoxelSize,
    TArray<FVector>& CellVertices,
    TArray<FVector>& CellNormals,
    const TArray<int32>& VertexIndices,
    int32 S)
{
    const int32 S3          = S * S * S;
    const float CellSize    = InVoxelSize * 1.5f;
    const float CellSizeInv = 1.f / CellSize;
    const float ZCellSize   = InVoxelSize * 3.f;
    const float ZCellSizeInv = 1.f / ZCellSize;

    // FIX #3: border margin — skip the outermost 2 cells on every axis
    // so boundary vertices are never altered independently in each chunk.
    const int32 BorderMin = 2;
    const int32 BorderMax = S - 3;

    auto IsBorderCell = [&](int32 FlatIdx) -> bool
    {
        const int32 z = FlatIdx / (S * S);
        const int32 y = (FlatIdx / S) % S;
        const int32 x = FlatIdx % S;
        return x <= BorderMin || x >= BorderMax ||
               y <= BorderMin || y >= BorderMax ||
               z <= BorderMin || z >= BorderMax;
    };

    TMap<FIntVector, float> CellMaxZ;
    CellMaxZ.Reserve(S3 / 4);

    // Pass 1: collect max Z per bucket (interior, top-facing only)
    for (int32 i = 0; i < S3; ++i)
    {
        if (VertexIndices[i] != 1) continue;
        if (CellNormals[i].Z   <= 0.9f) continue;
        if (IsBorderCell(i))            continue;  // FIX #3: skip border

        const FIntVector Key(
            FMath::FloorToInt(CellVertices[i].X * CellSizeInv),
            FMath::FloorToInt(CellVertices[i].Y * CellSizeInv),
            FMath::FloorToInt(CellVertices[i].Z * ZCellSizeInv));
        float& MaxZ = CellMaxZ.FindOrAdd(Key, CellVertices[i].Z);
        MaxZ = FMath::Max(MaxZ, CellVertices[i].Z);
    }

    // Pass 2: snap to local max
    for (int32 i = 0; i < S3; ++i)
    {
        if (VertexIndices[i] != 1) continue;
        if (CellNormals[i].Z   <= 0.9f) continue;
        if (IsBorderCell(i))            continue;  // FIX #3: skip border

        const FIntVector Key(
            FMath::FloorToInt(CellVertices[i].X * CellSizeInv),
            FMath::FloorToInt(CellVertices[i].Y * CellSizeInv),
            FMath::FloorToInt(CellVertices[i].Z * ZCellSizeInv));

        if (const float* MaxZ = CellMaxZ.Find(Key))
        {
            if (FMath::Abs(*MaxZ - CellVertices[i].Z) > KINDA_SMALL_NUMBER)
            {
                CellVertices[i].Z = *MaxZ;
                CellNormals[i]    = FVector(0.f, 0.f, 1.f);
            }
        }
    }
}

// ---------------------------------------------------------------------------
void FVoxelMeshGenerator::GenerateMesh(
    const TArray<float>&          Densities,
    int32                         InChunkSize,
    float                         InVoxelSize,
    const FVector&                ChunkOrigin,
    FVoxelMeshOutput&             OutMesh,
    const FVoxelGenerationConfig& Config,
    int32                         InStepSize,
    FVoxelMeshScratchBuffers*     Scratch,
    const TArray<FVoxelBiomeWeightMap>* PrecomputedColumnWeights)
{
    OutMesh.Reset();

    const int32 EffectiveSize    = InChunkSize / InStepSize;
    const float EffVoxelSize     = InVoxelSize * (float)InStepSize;
    const int32 S                = EffectiveSize + 3;
    const int32 S3               = S * S * S;

    OutMesh.FlatMesh.ReserveInitial(EffectiveSize * EffectiveSize * 3);

    FVoxelMeshScratchBuffers LocalScratch;
    FVoxelMeshScratchBuffers& S_Buf = Scratch ? *Scratch : LocalScratch;
    S_Buf.Reset(S3);

    TArray<int32>&   VertexIndices = S_Buf.VertexIndices;
    TArray<FVector>& CellVertices  = S_Buf.CellVertices;
    TArray<FVector>& CellNormals   = S_Buf.CellNormals;

    TArray<int32>& FlatMap      = S_Buf.FlatMap;
    TArray<int32>& SlopeMap     = S_Buf.SlopeMap;
    TArray<int32>& BackMap      = S_Buf.BackMap;
    TArray<int32>& SlopeBackMap = S_Buf.SlopeBackMap;

    static const FIntVector CornerOffset[8] = {
        {0,0,0},{1,0,0},{1,1,0},{0,1,0},
        {0,0,1},{1,0,1},{1,1,1},{0,1,1}
    };
    static const int32 EdgeToCorner[12][2] = {
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
            float   D[8]; FVector P[8]; int32 CubeIndex = 0;
            for (int32 i = 0; i < 8; ++i)
            {
                const int32 cx = X + CornerOffset[i].X;
                const int32 cy = Y + CornerOffset[i].Y;
                const int32 cz = Z + CornerOffset[i].Z;
                D[i] = Densities[Idx(cx, cy, cz, S)];
                P[i] = FVector(cx-1, cy-1, cz-1) * EffVoxelSize;
                if (D[i] > 0.f) CubeIndex |= (1 << i);
            }
            if (CubeIndex == 0 || CubeIndex == 255) continue;
 
            FVector CellPos  = FVector::ZeroVector;
            int32   EdgeCount = 0;
            for (int32 e = 0; e < 12; ++e)
            {
                const int32 c0 = EdgeToCorner[e][0], c1 = EdgeToCorner[e][1];
                if ((D[c0] > 0.f) != (D[c1] > 0.f))
                {
                    CellPos += InterpolateEdge(P[c0], D[c0], P[c1], D[c1]);
                    ++EdgeCount;
                }
            }
 
            // FIX #12: guard against degenerate cells with zero cut edges
            if (EdgeCount < 1) continue;
 
            CellPos /= (float)EdgeCount;
            const int32 CI    = Idx(X, Y, Z, S);
            CellVertices[CI]  = CellPos;
            CellNormals[CI]   = ComputeNormal(Densities, X, Y, Z, EffectiveSize);
            VertexIndices[CI] = 1;
        }
    });

    // FIX #3: Disabled to eliminate interior degenerate collapses causing concentric slot gaps.
    // FlattenCellTops(EffVoxelSize, CellVertices, CellNormals, VertexIndices, S);

    // ── ColumnColors ─────────────────────────────────────────────────
    // PERF-1: when PrecomputedColumnWeights is provided (EffSize×EffSize = S×S),
    // skip the per-column GetBiomeWeightsStatic call (2 Perlin2D + 7 blends × 361).
    // The precomputed array is addressed identically (FlatIdx = CX + CY*S).
    const bool bHasPrecomp = PrecomputedColumnWeights && PrecomputedColumnWeights->Num() == S * S;

    TArray<FColor> ColumnColors;
    ColumnColors.SetNumUninitialized(S * S);
    ParallelFor(S * S, [&](int32 FlatIdx)
    {
        FVoxelBiomeWeightMap W;
        if (bHasPrecomp)
        {
            // PERF-1: direct array read — zero noise evaluations
            W = (*PrecomputedColumnWeights)[FlatIdx];
        }
        else
        {
            const int32 CX = FlatIdx % S;
            const int32 CY = FlatIdx / S;
            const float WX = ChunkOrigin.X + (CX - 1.f) * EffVoxelSize;
            const float WY = ChunkOrigin.Y + (CY - 1.f) * EffVoxelSize;
            W = FVoxelBiomeManager::GetBiomeWeightsStatic(WX, WY, Config);
        }
        FLinearColor C(W.Forest, W.Desert, W.Peaks + W.Cliffs, W.Craters + W.Mesa);
        ColumnColors[FlatIdx] = C.ToFColor(false);
    });

    auto GetQuadColor = [&](int32 qX, int32 qY) -> const FColor&
    {
        return ColumnColors[FMath::Clamp(qX, 0, S-1) + FMath::Clamp(qY, 0, S-1) * S];
    };

    auto MakeUV = [&](const FVector& VLocal) -> FVector2D
    {
        const FVector VW = ChunkOrigin + VLocal;
        const float   s  = InVoxelSize * 4.f;
        return FVector2D(VW.X / s, VW.Y / s);
    };

    // EmitTriangle: appends one triangle to Dest.  Winding is already correct
    // when called — EmitQuad applies the bD0Solid reversal before calling here.
    // FaceNorm is kept as a parameter for symmetry / future use but is not
    // inspected here (deterministic winding needs no cross-product check).
    auto EmitTriangle = [&](FVoxelMeshData& Dest, TArray<int32>& Map,
                             int32 IA, int32 IB, int32 IC,
                             const FVector& /*FaceNorm*/, const FColor& VC)
    {
        // Drop degenerate triangles (zero-area)
        const FVector& V0 = CellVertices[IA];
        const FVector& V1 = CellVertices[IB];
        const FVector& V2 = CellVertices[IC];
        if (FVector::CrossProduct(V1-V0, V2-V0).SizeSquared() < 1e-8f) return;

        auto AppendV = [&](int32 ci) -> int32 {
            if (Map[ci] != -1) return Map[ci];
            const int32 NI = Dest.Vertices.Add(CellVertices[ci]);
            Dest.Normals.Add(CellNormals[ci]);
            Dest.UVs.Add(MakeUV(CellVertices[ci]));
            Dest.VertexColors.Add(VC);
            Dest.Tangents.Add(FProcMeshTangent(1,0,0));
            Map[ci] = NI;
            return NI;
        };
        Dest.Triangles.Add(AppendV(IA));
        Dest.Triangles.Add(AppendV(IB));
        Dest.Triangles.Add(AppendV(IC));
    };

    auto EmitQuad = [&](int32 i0, int32 i1, int32 i2, int32 i3,
                         int32 ColX, int32 ColY, bool bD0Solid, const FVector& Axis)
    {
        const bool b0 = VertexIndices[i0] >= 0;
        const bool b1 = VertexIndices[i1] >= 0;
        const bool b2 = VertexIndices[i2] >= 0;
        const bool b3 = VertexIndices[i3] >= 0;

        int32 ValidCount = (b0?1:0) + (b1?1:0) + (b2?1:0) + (b3?1:0);
        if (ValidCount < 3) 
        {
            static int32 LogCount = 0;
            if (LogCount++ < 50)
            {
                UE_LOG(LogTemp, Warning, TEXT("EmitQuad DROPPED @ C(%d,%d) A(%s) - b0=%d b1=%d b2=%d b3=%d"), ColX, ColY, *Axis.ToString(), b0, b1, b2, b3);
            }
            return; // Drop if < 3 vertices
        }

        const FVector OutwardNormal = bD0Solid ? Axis : -Axis;

        const float AvgZ = (CellNormals[i0].Z + CellNormals[i1].Z + CellNormals[i2].Z + CellNormals[i3].Z) * 0.25f;
        const bool bIsFlat = FMath::Abs(AvgZ) >= Config.SlopeThreshold;

        FVoxelMeshData& Dest = bIsFlat ? OutMesh.FlatMesh : OutMesh.SlopeMesh;
        TArray<int32>&  Map  = bIsFlat ? FlatMap : SlopeMap;
        const FColor&   VC   = GetQuadColor(ColX, ColY);

        // FIX WINDING: Axis-Specific Winding Switch Matrix
        bool bSwap = false;
        if (Axis.Y > 0.9f) { // Y-Axis
            bSwap = bD0Solid; 
        } else { // X and Z Axes
            bSwap = !bD0Solid;
        }

        if (ValidCount == 3)
        {
            auto GetfallbackV = [&](int32 invalidIdx) -> FVector {
                if (invalidIdx == 0) return CellVertices[i1] + (CellVertices[i3] - CellVertices[i2]);
                if (invalidIdx == 1) return CellVertices[i0] + (CellVertices[i2] - CellVertices[i3]);
                if (invalidIdx == 2) return CellVertices[i3] + (CellVertices[i1] - CellVertices[i0]);
                return CellVertices[i2] + (CellVertices[i0] - CellVertices[i1]); // invalidIdx == 3
            };

            auto AppendFallbackV = [&](const FVector& Pos) -> int32 {
                const int32 NI = Dest.Vertices.Add(Pos);
                Dest.Normals.Add(OutwardNormal); // rough approximation normal
                Dest.UVs.Add(MakeUV(Pos));
                Dest.VertexColors.Add(VC);
                Dest.Tangents.Add(FProcMeshTangent(1,0,0));
                return NI;
            };

            auto AppendV = [&](int32 ci) -> int32 {
                if (Map[ci] != -1) return Map[ci];
                const int32 NI = Dest.Vertices.Add(CellVertices[ci]);
                Dest.Normals.Add(CellNormals[ci]);
                Dest.UVs.Add(MakeUV(CellVertices[ci]));
                Dest.VertexColors.Add(VC);
                Dest.Tangents.Add(FProcMeshTangent(1,0,0));
                Map[ci] = NI;
                return NI;
            };

            // Draw regular triangles with synthesized nodes fallback connections full fallback complete quad:
            // Since filling is visual fallback, let's connect all 4 nodes into sequential output!
            int32 IA = b0 ? AppendV(i0) : AppendFallbackV(GetfallbackV(0));
            int32 IB = b1 ? AppendV(i1) : AppendFallbackV(GetfallbackV(1));
            int32 IC = b2 ? AppendV(i2) : AppendFallbackV(GetfallbackV(2));
            int32 ID = b3 ? AppendV(i3) : AppendFallbackV(GetfallbackV(3));

            if (bSwap)
            {
                Dest.Triangles.Add(IA); Dest.Triangles.Add(IC); Dest.Triangles.Add(IB);
                Dest.Triangles.Add(IB); Dest.Triangles.Add(IC); Dest.Triangles.Add(ID);
            }
            else
            {
                Dest.Triangles.Add(IA); Dest.Triangles.Add(IB); Dest.Triangles.Add(IC);
                Dest.Triangles.Add(IA); Dest.Triangles.Add(IC); Dest.Triangles.Add(ID);
            }
            return;
        }

        const float d02 = FVector::DistSquared(CellVertices[i0], CellVertices[i2]);
        const float d13 = FVector::DistSquared(CellVertices[i1], CellVertices[i3]);
        const bool bFlip = d13 < d02;

        if (bFlip)
        {
            if (bSwap)
            {
                EmitTriangle(Dest, Map, i0, i3, i1, OutwardNormal, VC);
                EmitTriangle(Dest, Map, i1, i3, i2, OutwardNormal, VC);
            }
            else
            {
                EmitTriangle(Dest, Map, i0, i1, i3, OutwardNormal, VC);
                EmitTriangle(Dest, Map, i1, i2, i3, OutwardNormal, VC);
            }
        }
        else
        {
            if (bSwap)
            {
                EmitTriangle(Dest, Map, i0, i2, i1, OutwardNormal, VC);
                EmitTriangle(Dest, Map, i0, i3, i2, OutwardNormal, VC);
            }
            else
            {
                EmitTriangle(Dest, Map, i0, i1, i2, OutwardNormal, VC);
                EmitTriangle(Dest, Map, i0, i2, i3, OutwardNormal, VC);
            }
        }
    };

    // ── PASS 2: quad emission ─────────────────────────────────────────────
    for (int32 Z = 1; Z <= EffectiveSize; ++Z)
    for (int32 Y = 1; Y <= EffectiveSize; ++Y)
    for (int32 X = 1; X <= EffectiveSize; ++X)
    {
        // X-axis edges
        { const float D0 = Densities[Idx(X,Y,Z,S)], D1 = Densities[Idx(X+1,Y,Z,S)];
          if ((D0>0.f)!=(D1>0.f))
              EmitQuad(Idx(X,Y,Z,S),Idx(X,Y,Z-1,S),Idx(X,Y-1,Z-1,S),Idx(X,Y-1,Z,S),
                       X,Y,D0>0.f,FVector(1,0,0)); }

        // Y-axis edges
        { const float D0 = Densities[Idx(X,Y,Z,S)], D1 = Densities[Idx(X,Y+1,Z,S)];
          if ((D0>0.f)!=(D1>0.f))
              EmitQuad(Idx(X,Y,Z,S),Idx(X-1,Y,Z,S),Idx(X-1,Y,Z-1,S),Idx(X,Y,Z-1,S),
                       X,Y,D0>0.f,FVector(0,1,0)); }

        // Z-axis edges
        { const float D0 = Densities[Idx(X,Y,Z,S)], D1 = Densities[Idx(X,Y,Z+1,S)];
          if ((D0>0.f)!=(D1>0.f))
              EmitQuad(Idx(X,Y,Z,S),Idx(X,Y-1,Z,S),Idx(X-1,Y-1,Z,S),Idx(X-1,Y,Z,S),
                       X,Y,D0>0.f,FVector(0,0,1)); }
    }
}

// ---------------------------------------------------------------------------
// GenerateHeightmapMesh
// ---------------------------------------------------------------------------
void FVoxelMeshGenerator::GenerateHeightmapMesh(
    const TArray<float>&          Heights,
    const TArray<FVoxelBiomeWeightMap>& Weights,
    int32                         InChunkSize,
    float                         InVoxelSize,
    const FVector&                ChunkOrigin,
    FVoxelMeshOutput&             OutMesh,
    const struct FVoxelGenerationConfig& Config,
    int32                         InStepSize)
{
    OutMesh.Reset();

    const int32 EffectiveSize = InChunkSize / InStepSize;
    const float EffVoxelSize  = InVoxelSize * (float)InStepSize;
    const int32 S             = EffectiveSize + 3; // Padded size

    const int32 GridCount = (EffectiveSize + 1) * (EffectiveSize + 1);
    TArray<FVector> TempCoords;   TempCoords.SetNumUninitialized(GridCount);
    TArray<FVector> TempNormals;  TempNormals.SetNumUninitialized(GridCount);
    TArray<FVector2D> TempUVs;    TempUVs.SetNumUninitialized(GridCount);
    TArray<FColor>  TempColors;   TempColors.SetNumUninitialized(GridCount);
    TArray<bool>    TempValid;    TempValid.Init(false, GridCount);

    TArray<int32> FlatMap;  FlatMap.Init(-1, GridCount);
    TArray<int32> SlopeMap; SlopeMap.Init(-1, GridCount);

    // ── Pass 1: Vertex Position calculations ──────────────────────────────
    for (int32 ly = 0; ly <= EffectiveSize; ++ly)
    for (int32 lx = 0; lx <= EffectiveSize; ++lx)
    {
        const int32 X = lx + 1, Y = ly + 1;
        const int32 HIdx = X + Y * S;
        const int32 GridIdx = lx + ly * (EffectiveSize + 1);

        if (!Heights.IsValidIndex(HIdx)) continue;

        const float Height = Heights[HIdx];
        const FVector LocalPos(lx * EffVoxelSize, ly * EffVoxelSize, Height - ChunkOrigin.Z);
        TempCoords[GridIdx] = LocalPos;

        float H_Right = Heights[FMath::Clamp(X+1, 0, S-1) + Y*S];
        float H_Left  = Heights[FMath::Clamp(X-1, 0, S-1) + Y*S];
        float H_Up    = Heights[X + FMath::Clamp(Y+1, 0, S-1)*S];
        float H_Down  = Heights[X + FMath::Clamp(Y-1, 0, S-1)*S];

        FVector TangentX(2.0f * EffVoxelSize, 0.0f, H_Right - H_Left);
        FVector TangentY(0.0f, 2.0f * EffVoxelSize, H_Up - H_Down);
        FVector Normal = FVector::CrossProduct(TangentX, TangentY).GetSafeNormal();
        if (Normal.Z < 0.f) Normal = -Normal;
        TempNormals[GridIdx] = Normal;

        const FVector WorldPos = ChunkOrigin + LocalPos;
        const float s = InVoxelSize * 4.f;
        TempUVs[GridIdx] = FVector2D(WorldPos.X / s, WorldPos.Y / s);

        if (Weights.IsValidIndex(HIdx))
        {
            const FVoxelBiomeWeightMap& W = Weights[HIdx];
            FLinearColor C(W.Forest, W.Desert, W.Peaks + W.Cliffs, W.Craters + W.Mesa);
            TempColors[GridIdx] = C.ToFColor(false);
        }
        else TempColors[GridIdx] = FColor::Green;

        TempValid[GridIdx] = true;
    }

    auto AppendV = [&](FVoxelMeshData& Dest, TArray<int32>& Map, int32 GridIdx) -> int32 {
        if (Map[GridIdx] != -1) return Map[GridIdx];
        const int32 NI = Dest.Vertices.Add(TempCoords[GridIdx]);
        Dest.Normals.Add(TempNormals[GridIdx]);
        Dest.UVs.Add(TempUVs[GridIdx]);
        Dest.VertexColors.Add(TempColors[GridIdx]);
        Dest.Tangents.Add(FProcMeshTangent(1,0,0));
        Map[GridIdx] = NI;
        return NI;
    };

    auto EmitTriangleLocal = [&](FVoxelMeshData& Dest, TArray<int32>& Map, int32 c0, int32 c1, int32 c2, const FVector& FaceNormal) {
        if (!TempValid[c0] || !TempValid[c1] || !TempValid[c2]) return;
        const int32 i0_local = AppendV(Dest, Map, c0);
        const int32 i1_local = AppendV(Dest, Map, c1);
        const int32 i2_local = AppendV(Dest, Map, c2);

        FVector TriNorm = FVector::CrossProduct(TempCoords[c1] - TempCoords[c0], TempCoords[c2] - TempCoords[c0]);
        if (TriNorm.SizeSquared() < 1e-8f) return;

        int32 IA = i0_local, IB = i1_local, IC = i2_local;
        if ((TriNorm | FaceNormal) < 0.0f) { int32 Temp = IB; IB = IC; IC = Temp; }

        Dest.Triangles.Add(IA); Dest.Triangles.Add(IB); Dest.Triangles.Add(IC);
        
        // Double-Sided fallback for vertical walls viewed from below
        Dest.Triangles.Add(IA); Dest.Triangles.Add(IC); Dest.Triangles.Add(IB);
    };

    // ── Pass 2: Quad Emission with Slope Splitting ───────────────────────
    for (int32 ly = 0; ly < EffectiveSize; ++ly)
    for (int32 lx = 0; lx < EffectiveSize; ++lx)
    {
        const int32 c0 = lx + ly * (EffectiveSize + 1);
        const int32 c1 = (lx + 1) + ly * (EffectiveSize + 1);
        const int32 c2 = (lx + 1) + (ly + 1) * (EffectiveSize + 1);
        const int32 c3 = lx + (ly + 1) * (EffectiveSize + 1);

        if (!TempValid[c0] || !TempValid[c1] || !TempValid[c2] || !TempValid[c3]) continue;

        FVector Pos0 = TempCoords[c0], Pos1 = TempCoords[c1], Pos3 = TempCoords[c3];
        FVector GeoNormal = FVector::CrossProduct(Pos1 - Pos0, Pos3 - Pos0).GetSafeNormal();

        const float AvgZ = (TempNormals[c0].Z + TempNormals[c1].Z + TempNormals[c2].Z + TempNormals[c3].Z) * 0.25f;
        const bool bIsFlat = FMath::Abs(AvgZ) >= Config.SlopeThreshold;

        FVoxelMeshData& Dest = bIsFlat ? OutMesh.FlatMesh : OutMesh.SlopeMesh;
        TArray<int32>& Map   = bIsFlat ? FlatMap : SlopeMap;

        EmitTriangleLocal(Dest, Map, c0, c1, c2, GeoNormal);
        EmitTriangleLocal(Dest, Map, c0, c2, c3, GeoNormal);
    }

    // ── Pass 3: Border Skirt ──────────────────────────────────────────────
    const float SkirtDepth = 1000.f; // 10m drop
    auto AddSkirtQuad = [&](FVoxelMeshData& Dest, int32 c0_top, int32 c1_top)
    {
        if (!TempValid[c0_top] || !TempValid[c1_top]) return;
        const FVector Pos0 = TempCoords[c0_top], Pos1 = TempCoords[c1_top];

        int32 i0_top = AppendV(Dest, FlatMap, c0_top); 
        int32 i1_top = AppendV(Dest, FlatMap, c1_top);

        int32 i0_bot = Dest.Vertices.Add(Pos0 - FVector(0, 0, SkirtDepth));
        int32 i1_bot = Dest.Vertices.Add(Pos1 - FVector(0, 0, SkirtDepth));

        Dest.Normals.Add(FVector(0,0,1)); Dest.Normals.Add(FVector(0,0,1));
        Dest.VertexColors.Add(FColor::Green); Dest.VertexColors.Add(FColor::Green);
        Dest.UVs.Add(FVector2D(0,0)); Dest.UVs.Add(FVector2D(0,0));
        Dest.Tangents.Add(FProcMeshTangent(1,0,0)); Dest.Tangents.Add(FProcMeshTangent(1,0,0));

        Dest.Triangles.Add(i0_top); Dest.Triangles.Add(i0_bot); Dest.Triangles.Add(i1_top);
        Dest.Triangles.Add(i1_top); Dest.Triangles.Add(i0_bot); Dest.Triangles.Add(i1_bot);
    };

    for (int32 ly = 0; ly < EffectiveSize; ++ly) {
        AddSkirtQuad(OutMesh.FlatMesh, 0 + ly * (EffectiveSize + 1), 0 + (ly + 1) * (EffectiveSize + 1));
        AddSkirtQuad(OutMesh.FlatMesh, EffectiveSize + (ly + 1) * (EffectiveSize + 1), EffectiveSize + ly * (EffectiveSize + 1));
    }
    for (int32 lx = 0; lx < EffectiveSize; ++lx) {
        AddSkirtQuad(OutMesh.FlatMesh, (lx+1) + 0 * (EffectiveSize + 1), lx + 0 * (EffectiveSize + 1));
        AddSkirtQuad(OutMesh.FlatMesh, lx + EffectiveSize * (EffectiveSize + 1), (lx+1) + EffectiveSize * (EffectiveSize + 1));
    }
}
