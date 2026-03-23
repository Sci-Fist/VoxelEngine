// VoxelGeneratorTask_Foliage.cpp
//
// FIX N21 — This file was missing. VoxelGeneratorTask.cpp declares
//            CalculateFoliage(), ProcessLegacyFoliage(), TrimFoliageToCap()
//            in the header and calls them from Execute(), but no .cpp provided
//            the implementations → linker failure.
//
// FOLIAGE PIPELINE:
//   1. Iterate FlatMesh triangles (top-facing quads from Surface Nets).
//   2. For each tri, use FoliageDensity × SpawnChance to determine attempt count.
//   3. Sample a random point on the tri using barycentric coordinates.
//   4. Lookup biome weights at the sample position (from pre-built ColumnWeights).
//   5. Check MinBiomeWeight, MinSlopeAlignment, MinWorldZ/MaxWorldZ per slot.
//   6. Emit a FTransform with random yaw/scale per the FVoxelFoliageEntry config.
//   7. TrimFoliageToCap: if total instances exceed cap, proportionally cull all slots.
//
// FIX #4  — ProcessLegacyFoliage called only when !bHasPerBiomeFoliage.
// FIX #11 — TrimFoliageToCap uses proportional trim ratio across all slots.

#include "Generation/VoxelGeneratorTask.h"
#include "Biomes/VoxelBiomeManager.h"
#include "Config/VoxelGenerationConfig.h"
#include "Math/RandomStream.h"

// ---------------------------------------------------------------------------
// RNG — per-chunk deterministic stream seeded from coord + global seed
// ---------------------------------------------------------------------------
static FORCEINLINE FRandomStream MakeFoliageRNG(const FIntVector& Coord, int32 Seed)
{
    const int32 S = Coord.X * 73856093 ^ Coord.Y * 19349663 ^ Coord.Z * 83492791 ^ Seed;
    return FRandomStream(S);
}

// ---------------------------------------------------------------------------
// LookupColumnWeights — return biome weights for a world-space X,Y position
// Uses the pre-built ColumnWeights array when the point falls in the interior
// grid, otherwise samples directly (for near-border triangles).
// ---------------------------------------------------------------------------
static FVoxelBiomeWeightMap LookupColumnWeights(
    float WX, float WY,
    const FVector& WorldOrigin,
    float VoxelSize, int32 EffCS,
    const TArray<FVoxelBiomeWeightMap>& ColumnWeights,
    const FVoxelGenerationConfig& Config)
{
    const int32 LX = FMath::FloorToInt((WX - WorldOrigin.X) / VoxelSize);
    const int32 LY = FMath::FloorToInt((WY - WorldOrigin.Y) / VoxelSize);
    if (LX >= 0 && LX < EffCS && LY >= 0 && LY < EffCS)
        return ColumnWeights[LX + LY * EffCS];
    return FVoxelBiomeManager::GetBiomeWeightsStatic(WX, WY, Config);
}

// ---------------------------------------------------------------------------
// CalculateFoliage
// ---------------------------------------------------------------------------
void FVoxelGeneratorTask::CalculateFoliage()
{
    if (bCancelled) return;

    const int32 EffCS = ChunkSize / StepSize;
    const float EffVS = VoxelSize * (float)StepSize;

    // Build per-slot output arrays
    PerFoliageTransforms.SetNum(FoliageSlots.Num());
    PerFoliageMeshes.SetNumZeroed(FoliageSlots.Num());
    for (int32 i = 0; i < FoliageSlots.Num(); ++i)
        PerFoliageMeshes[i] = FoliageSlots[i].Mesh;

    FRandomStream RNG = MakeFoliageRNG(ChunkCoord, Config.Seed);

    auto SampleTriangle = [&](const FVector& V0, const FVector& V1, const FVector& V2) -> FVector
    {
        float U = RNG.GetFraction(), V = RNG.GetFraction();
        if (U + V > 1.f) { U = 1.f - U; V = 1.f - V; }
        return V0 + (V1-V0)*U + (V2-V0)*V;
    };

    // ── Per-biome foliage ─────────────────────────────────────────────────
    if (bHasPerBiomeFoliage)
    {
        const TArray<FVector>& Verts   = MeshOutput.FlatMesh.Vertices;
        const TArray<int32>&   Tris    = MeshOutput.FlatMesh.Triangles;
        const TArray<FVector>& Normals = MeshOutput.FlatMesh.Normals;

        FCriticalSection FoliageLock;
        const int32 TriCount = Tris.Num() / 3;

        const int32 BatchSize = 64;
        const int32 NumBatches = (TriCount + BatchSize - 1) / BatchSize;

        ParallelFor(NumBatches, [&](int32 BatchIdx)
        {
            if (bCancelled) return;

            // Thread-Local Accumulator avoids inner-loop lock contention
            TArray<TArray<FTransform>> LocalTransforms;
            LocalTransforms.SetNum(FoliageSlots.Num());

            const int32 StartTri = BatchIdx * BatchSize;
            const int32 EndTri   = FMath::Min((BatchIdx + 1) * BatchSize, TriCount);

            for (int32 TriIdx = StartTri; TriIdx < EndTri; ++TriIdx)
            {
                if (bCancelled) break;

                const int32 ti = TriIdx * 3;
                const int32 i0 = Tris[ti], i1 = Tris[ti+1], i2 = Tris[ti+2];
                if (!Normals.IsValidIndex(i0)) continue;

                const FVector N = Normals[i0];
                if (N.Z < 0.5f) continue;

                const FVector V0 = Verts[i0] + WorldOrigin;
                const FVector V1 = Verts[i1] + WorldOrigin;
                const FVector V2 = Verts[i2] + WorldOrigin;

                const FVector Centroid = (V0 + V1 + V2) / 3.f;
                const FVoxelBiomeWeightMap W = LookupColumnWeights(
                    Centroid.X, Centroid.Y, WorldOrigin, EffVS, EffCS, ColumnWeights, Config);

                FRandomStream TriRNG = MakeFoliageRNG(ChunkCoord, Config.Seed + ti);
                auto SampleTriangleLocal = [&](const FVector& V0_l, const FVector& V1_l, const FVector& V2_l) -> FVector
                {
                    float U = TriRNG.GetFraction(), V = TriRNG.GetFraction();
                    if (U + V > 1.f) { U = 1.f - U; V = 1.f - V; }
                    return V0_l + (V1_l-V0_l)*U + (V2_l-V0_l)*V;
                };

                for (int32 SlotIdx = 0; SlotIdx < FoliageSlots.Num(); ++SlotIdx)
                {
                    const FFoliageSlot& Slot = FoliageSlots[SlotIdx];
                    if (!Slot.Mesh) continue;

                    const FVoxelBiomeRenderConfig& BR = Config.GetBiomeRender(Slot.Biome);
                    if (!BR.FoliageTypes.IsValidIndex(Slot.EntryIdx)) continue;

                    const FVoxelFoliageEntry& E = BR.FoliageTypes[Slot.EntryIdx];
                    if (!E.Mesh) continue;

                    if (W.GetWeight(Slot.Biome) < E.MinBiomeWeight) continue;
                    if (N.Z < E.MinSlopeAlignment) continue;

                    const int32 Attempts = FMath::Max(1, E.SpawnAttemptsPerTriangle);
                    for (int32 a = 0; a < Attempts; ++a)
                    {
                        if (TriRNG.GetFraction() > E.SpawnChance * FoliageDensity) continue;

                        FVector SpawnPos = SampleTriangleLocal(V0, V1, V2);
                        SpawnPos.Z += E.HeightOffset;

                        if (SpawnPos.Z < E.MinWorldZ || SpawnPos.Z > E.MaxWorldZ) continue;

                        const float Scale = FMath::Lerp(E.ScaleMin, E.ScaleMax, TriRNG.GetFraction());

                        FRotator Rot = FRotator::ZeroRotator;
                        if (E.bAlignToSurface)
                        {
                            Rot = FRotationMatrix::MakeFromZX(N, FVector::ForwardVector).Rotator();
                        }
                        if (E.bRandomYaw) Rot.Yaw += TriRNG.GetFraction() * 360.f;
                        else              Rot.Yaw += E.FixedYaw;

                        LocalTransforms[SlotIdx].Add(FTransform(Rot, SpawnPos, FVector(Scale)));
                    }
                }
            }

            // Flush Batch to Shared Main Output Container
            {
                FScopeLock Lock(&FoliageLock);
                for (int32 i = 0; i < FoliageSlots.Num(); ++i)
                {
                    if (LocalTransforms[i].Num() > 0)
                        PerFoliageTransforms[i].Append(LocalTransforms[i]);
                }
            }
        });


        TrimFoliageToCap(4096);
    }
    else
    {
        // ── Legacy fallback: global TreeMesh / GrassMesh ──────────────────
        const TArray<FVector>& Verts   = MeshOutput.FlatMesh.Vertices;
        const TArray<int32>&   Tris    = MeshOutput.FlatMesh.Triangles;
        const TArray<FVector>& Normals = MeshOutput.FlatMesh.Normals;

        FCriticalSection LegacyFoliageLock;
        const int32 LegacyTriCount = Tris.Num() / 3;

        const int32 LegacyBatchSize = 64;
        const int32 NumLegacyBatches = (LegacyTriCount + LegacyBatchSize - 1) / LegacyBatchSize;

        ParallelFor(NumLegacyBatches, [&](int32 BatchIdx)
        {
            if (bCancelled) return;

            TArray<FTransform> LocalTrees;
            TArray<FTransform> LocalGrass;

            const int32 StartTri = BatchIdx * LegacyBatchSize;
            const int32 EndTri   = FMath::Min((BatchIdx + 1) * LegacyBatchSize, LegacyTriCount);

            for (int32 TriIdx = StartTri; TriIdx < EndTri; ++TriIdx)
            {
                if (bCancelled) break;

                const int32 ti = TriIdx * 3;
                const int32 i0 = Tris[ti], i1 = Tris[ti+1], i2 = Tris[ti+2];
                if (!Normals.IsValidIndex(i0)) continue;

                const FVector N = Normals[i0];
                if (N.Z < MaxFoliageSlope) continue;

                const FVector V0 = Verts[i0] + WorldOrigin;
                const FVector V1 = Verts[i1] + WorldOrigin;
                const FVector V2 = Verts[i2] + WorldOrigin;
                const FVector C  = (V0 + V1 + V2) / 3.f;
                const FVoxelBiomeWeightMap W = LookupColumnWeights(
                    C.X, C.Y, WorldOrigin, EffVS, EffCS, ColumnWeights, Config);

                FRandomStream TriRNG = MakeFoliageRNG(ChunkCoord, Config.Seed + ti);
                
                // Inline equivalent of ProcessLegacyFoliage to avoid inner mutex locks
                const float PlainW = W.Forest + W.Desert + W.Mesa;
                const bool  bFlat  = N.Z > 0.85f;

                auto SampleTriangleLocal = [&](const FVector& V0_l, const FVector& V1_l, const FVector& V2_l) -> FVector
                {
                    float U = TriRNG.GetFraction(), V = TriRNG.GetFraction();
                    if (U + V > 1.f) { U = 1.f - U; V = 1.f - V; }
                    return V0_l + (V1_l-V0_l)*U + (V2_l-V0_l)*V;
                };

                const FVector SpawnPos = SampleTriangleLocal(V0, V1, V2);

                if (bFlat && PlainW > 0.3f && TriRNG.GetFraction() < FoliageDensity * 0.3f)
                {
                    FRotator Rot(0.f, TriRNG.GetFraction() * 360.f, 0.f);
                    const float S = FMath::Lerp(0.8f, 1.2f, TriRNG.GetFraction());
                    LocalTrees.Add(FTransform(Rot, SpawnPos, FVector(S)));
                }
                if (bFlat && PlainW > 0.2f && TriRNG.GetFraction() < FoliageDensity * 0.8f)
                {
                    FRotator Rot(0.f, TriRNG.GetFraction() * 360.f, 0.f);
                    const float S = FMath::Lerp(0.6f, 1.0f, TriRNG.GetFraction());
                    LocalGrass.Add(FTransform(Rot, SpawnPos, FVector(S)));
                }
            }

            {
                FScopeLock Lock(&LegacyFoliageLock);
                if (LocalTrees.Num() > 0) LegacyTreeTransforms.Append(LocalTrees);
                if (LocalGrass.Num() > 0) LegacyGrassTransforms.Append(LocalGrass);
            }
        });
    }
}

// ---------------------------------------------------------------------------
// ProcessLegacyFoliage — FIX #4: only called when !bHasPerBiomeFoliage
// ---------------------------------------------------------------------------
void FVoxelGeneratorTask::ProcessLegacyFoliage(
    const FVector& Centroid, float SlopeZ,
    const FVoxelBiomeWeightMap& W, const FVector& WorldCenter)
{
    // Simple tree + grass heuristic
    FRandomStream R = MakeFoliageRNG(ChunkCoord, Config.Seed + (int32)(Centroid.X+Centroid.Y));

    const float PlainW = W.Forest + W.Desert + W.Mesa;
    const float RoughW = W.Peaks + W.Cliffs;
    const bool  bFlat  = SlopeZ > 0.85f;

    if (bFlat && PlainW > 0.3f && R.GetFraction() < FoliageDensity * 0.3f)
    {
        FRotator Rot(0.f, R.GetFraction()*360.f, 0.f);
        const float S = FMath::Lerp(0.8f, 1.2f, R.GetFraction());
        LegacyTreeTransforms.Add(FTransform(Rot, WorldCenter, FVector(S)));
    }
    if (bFlat && PlainW > 0.2f && R.GetFraction() < FoliageDensity * 0.8f)
    {
        FRotator Rot(0.f, R.GetFraction()*360.f, 0.f);
        const float S = FMath::Lerp(0.6f, 1.0f, R.GetFraction());
        LegacyGrassTransforms.Add(FTransform(Rot, WorldCenter, FVector(S)));
    }
}

// ---------------------------------------------------------------------------
// TrimFoliageToCap — FIX #11: proportional trim across all slots
// ---------------------------------------------------------------------------
void FVoxelGeneratorTask::TrimFoliageToCap(const int32 Cap)
{
    int32 Total = 0;
    for (const auto& Slot : PerFoliageTransforms) Total += Slot.Num();
    if (Total <= Cap) return;

    const float Ratio = (float)Cap / (float)Total;
    for (auto& Slot : PerFoliageTransforms)
    {
        const int32 Target = FMath::Max(0, FMath::FloorToInt(Slot.Num() * Ratio));
        if (Slot.Num() > Target)
            Slot.SetNum(Target, EAllowShrinking::Yes);
    }
}
