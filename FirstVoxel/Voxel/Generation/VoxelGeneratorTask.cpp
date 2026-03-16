// VoxelGeneratorTask.cpp
// Thread-safe task: Density -> Mesh -> Per-Biome Foliage
//
// PIPELINE:
//   BuildDensityField  -- samples the 3-layer density generator for every voxel
//                         in the padded (ChunkSize/StepSize+3)^3 grid.
//   BuildMesh          -- runs Surface Nets on the density field.
//   CalculateFoliage   -- scatters per-biome foliage on flat mesh triangles.

#include "Generation/VoxelGeneratorTask.h"
#include "Generation/VoxelMeshGenerator.h"
#include "Generation/VoxelDensityGenerator.h"
#include "Biomes/VoxelBiomeManager.h"
#include "Async/ParallelFor.h"

// All surface biomes in EVoxelBiome order (cast to uint8 gives array index).
// Desert was previously missing here, silently preventing desert foliage from
// ever being pre-cached or spawned.
// The static_assert below catches any future mismatch at compile time.
static const EVoxelBiome GBiomeOrder[] =
{
    EVoxelBiome::Forest,
    EVoxelBiome::Peaks,
    EVoxelBiome::Cliffs,
    EVoxelBiome::Mesa,
    EVoxelBiome::Craters,
    EVoxelBiome::Desert,
};
static_assert(UE_ARRAY_COUNT(GBiomeOrder) == FVoxelBiomeWeightMap::MaxBiomes,
    "GBiomeOrder must contain exactly one entry per EVoxelBiome value.");
static constexpr int32 GNumBiomes = UE_ARRAY_COUNT(GBiomeOrder);

// ============================================================
//  Constructor
// ============================================================
FVoxelGeneratorTask::FVoxelGeneratorTask(
    const FIntVector&             InChunkCoord,
    const FVector&                InWorldOrigin,
    int32                         InChunkSize,
    float                         InVoxelSize,
    int32                         InStepSize,
    const FVoxelGenerationConfig& InConfig,
    IVoxelDensityProvider*        InProvider,
    float                         InFoliageDensity,
    float                         InMaxFoliageSlope,
    const TMap<int32, float>&     InLocalDataCache)
    : ChunkCoord     (InChunkCoord)
    , WorldOrigin    (InWorldOrigin)
    , ChunkSize      (InChunkSize)
    , VoxelSize      (InVoxelSize)
    , StepSize       (InStepSize)
    , Config         (InConfig)
    , DensityProvider(InProvider)
    , FoliageDensity (InFoliageDensity)
    , MaxFoliageSlope(InMaxFoliageSlope)
    , LocalDataCache (InLocalDataCache)
{
    // Pre-cache the foliage slot schema from the config.
    // Doing this in the constructor means CalculateFoliage() iterates a flat
    // local array instead of walking the nested biome/foliage config on each
    // triangle, eliminating repeated GetBiomeRender() calls inside the hot loop.
    for (EVoxelBiome Biome : GBiomeOrder)
    {
        const FVoxelBiomeRenderConfig& BR = Config.GetBiomeRender(Biome);
        if (!BR.bEnableFoliage) continue;

        for (int32 EIdx = 0; EIdx < BR.FoliageTypes.Num(); ++EIdx)
        {
            const FVoxelFoliageEntry& Entry = BR.FoliageTypes[EIdx];
            FoliageSlots.Add({ Biome, EIdx, Entry.Mesh.Get() });
            if (Entry.Mesh) bHasPerBiomeFoliage = true;
        }
    }
}

// ============================================================
//  Execute
// ============================================================
void FVoxelGeneratorTask::Execute()
{
    if (bCancelled) return;
    BuildDensityField();

    if (bCancelled) return;
    BuildMesh();

    if (bCancelled) return;
    CalculateFoliage();
}

// ============================================================
//  BuildDensityField
//  Fills the flat Densities array for every voxel in the padded grid.
//  Inner loop structure:
//    ParallelFor(Y)          -- one job per row, each job processes all X in that row
//      for X                 -- per-column: get biome weights + surface height (O(n^2) noise)
//        for Z               -- per-voxel: evaluate full 3-layer density   (O(n^3) noise)
//  The biome+surface computation is hoisted above the Z loop so it runs
//  exactly once per XY column instead of once per voxel.
// ============================================================
void FVoxelGeneratorTask::BuildDensityField()
{
    const int32 EffectiveSize = (ChunkSize / StepSize) + 3;
    const int32 TotalSamples  = EffectiveSize * EffectiveSize * EffectiveSize;
    const float EffVoxelSize  = VoxelSize * (float)StepSize;

    // VoxelCS = raw chunk voxel count  (used to unpack the DataMap flat key).
    // EffCS   = effective column count (used for ColumnWeights array stride).
    const int32 VoxelCS = ChunkSize;
    const int32 EffCS   = ChunkSize / StepSize;

    Densities.SetNumUninitialized(TotalSamples);

    // ---- Player voxel edits: sparse TMap -> flat dense array ----
    // This allows the inner Z loop to do a constant-time array read instead
    // of a hash-map lookup for every voxel.
    TArray<float> DenseEdits;
    const bool    bHasEdits = LocalDataCache.Num() > 0;
    if (bHasEdits)
    {
        DenseEdits.SetNumUninitialized(TotalSamples);
        for (int32 i = 0; i < TotalSamples; ++i) DenseEdits[i] = 1e9f; // sentinel: no override

        // Unpack key (LX + LY*CS + LZ*CS^2) into the padded density grid index.
        for (const auto& Pair : LocalDataCache)
        {
            const int32 Idx     = Pair.Key;
            const int32 LZ      = Idx / (VoxelCS * VoxelCS);
            const int32 Remnant = Idx % (VoxelCS * VoxelCS);
            const int32 LY      = Remnant / VoxelCS;
            const int32 LX      = Remnant % VoxelCS;

            // Padded array: voxel (LX, LY, LZ) lives at index (LX+1, LY+1, LZ+1).
            const int32 DenseIdx = (LX + 1)
                                 + (LY + 1) * EffectiveSize
                                 + (LZ + 1) * EffectiveSize * EffectiveSize;
            if (DenseIdx >= 0 && DenseIdx < TotalSamples)
                DenseEdits[DenseIdx] = Pair.Value;
        }
    }

    static FVoxelDensityGenerator FallbackGenerator;
    IVoxelDensityProvider* Provider = DensityProvider ? DensityProvider : &FallbackGenerator;

    // Allocate column weight cache without zero-constructing -- every entry will
    // be overwritten in the parallel loop below.
    ColumnWeights.SetNumUninitialized(EffCS * EffCS);

    // ---- Main density loop ----
    // Outer dispatch is by Y row so each thread handles a contiguous X stripe,
    // giving good spatial locality in the XZ-ordered density array.
    ParallelFor(EffectiveSize, [&](int32 Y)
    {
        if (bCancelled) return;

        for (int32 X = 0; X < EffectiveSize; ++X)
        {
            const float WorldX = WorldOrigin.X + (X - 1.f) * EffVoxelSize;
            const float WorldY = WorldOrigin.Y + (Y - 1.f) * EffVoxelSize;

            // ---- Per-column work (O(n^2)) ----
            // Biome weights and surface height are the same for the entire
            // vertical column, so they are computed once here and reused
            // for every Z below.
            const FVoxelBiomeWeightMap Weights       = Provider->GetBiomeWeights(WorldX, WorldY, Config);
            const float               SurfaceHeight  = FVoxelBiomeManager::GetSurfaceHeightStatic(
                                                          WorldX, WorldY, Weights, Config);

            // Cache weights for foliage pass (inner non-padding columns only).
            const int32 LX = X - 1, LY = Y - 1;
            if (LX >= 0 && LX < EffCS && LY >= 0 && LY < EffCS)
                ColumnWeights[LX + LY * EffCS] = Weights;

            // ---- Per-voxel work (O(n^3)) ----
            for (int32 Z = 0; Z < EffectiveSize; ++Z)
            {
                const float WorldZ = WorldOrigin.Z + (Z - 1.f) * EffVoxelSize;
                const int32 Idx    = X + Y * EffectiveSize + Z * EffectiveSize * EffectiveSize;

                float D = Provider->GetDensityFull(
                    FVector(WorldX, WorldY, WorldZ), Weights, SurfaceHeight, Config);

                // Apply player edits (constant-time dense array lookup).
                if (bHasEdits)
                {
                    const float Override = DenseEdits[Idx];
                    if (Override != 1e9f)
                    {
                        D = (Override < 0.f) ? FMath::Min(D, Override)
                                             : FMath::Max(D, Override);
                    }
                }

                Densities[Idx] = D;
            }
        }
    });
}

// ============================================================
//  BuildMesh
// ============================================================
void FVoxelGeneratorTask::BuildMesh()
{
    MeshOutput.Reset();
    FVoxelMeshGenerator::GenerateMesh(
        Densities, ChunkSize, VoxelSize, WorldOrigin, MeshOutput, Config, 0.7f, StepSize);
}

// ============================================================
//  CalculateFoliage
//  Scatters per-biome foliage instances onto flat mesh triangles.
//  Uses the ColumnWeights cache built during BuildDensityField to
//  avoid re-running biome noise for every triangle.
// ============================================================
void FVoxelGeneratorTask::CalculateFoliage()
{
    LegacyTreeTransforms.Reset();
    LegacyGrassTransforms.Reset();
    PerFoliageTransforms.Reset();
    PerFoliageMeshes.Reset();

    if (MeshOutput.FlatMesh.Vertices.Num() == 0) return;

    // Size output arrays to match the pre-cached slot count.
    PerFoliageTransforms.SetNum(FoliageSlots.Num());
    PerFoliageMeshes.SetNum(FoliageSlots.Num());
    for (int32 s = 0; s < FoliageSlots.Num(); ++s)
        PerFoliageMeshes[s] = FoliageSlots[s].Mesh;

    const auto& Tris   = MeshOutput.FlatMesh.Triangles;
    const auto& Verts  = MeshOutput.FlatMesh.Vertices;
    const auto& Colors = MeshOutput.FlatMesh.VertexColors;

    // These are constant for every triangle in this chunk -- compute once.
    const int32 EffCS        = ChunkSize / StepSize;
    const float EffVoxelSize = VoxelSize * StepSize;

    // Process every triangle in the flat (top-facing) mesh section.
    for (int32 i = 0; i + 2 < Tris.Num(); i += 3)
    {
        const FVector v0 = Verts[Tris[i]];
        const FVector v1 = Verts[Tris[i + 1]];
        const FVector v2 = Verts[Tris[i + 2]];

        const FVector FaceNormal  = FVector::CrossProduct(v1 - v0, v2 - v0).GetSafeNormal();
        const float   SlopeZ      = FVector::DotProduct(FaceNormal, FVector::UpVector);
        const FVector Center      = (v0 + v1 + v2) / 3.f;
        const FVector WorldCenter = WorldOrigin + Center;

        // ---- Cheap gate: skip triangles that cannot satisfy any slot's filters ----
        bool bAnyCanPass = false;
        if (bHasPerBiomeFoliage)
        {
            for (const FFoliageSlot& Slot : FoliageSlots)
            {
                if (!Slot.Mesh) continue;
                const FVoxelFoliageEntry& Entry = Config.GetBiomeRender(Slot.Biome).FoliageTypes[Slot.EntryIdx];
                if (SlopeZ >= Entry.MinSlopeAlignment
                    && WorldCenter.Z >= Entry.MinWorldZ
                    && WorldCenter.Z <= Entry.MaxWorldZ)
                {
                    bAnyCanPass = true;
                    break;
                }
            }
        }
        else
        {
            bAnyCanPass = (SlopeZ >= MaxFoliageSlope);
        }

        if (!bAnyCanPass) continue;

        // ---- Map triangle centre to cached column weights (O(1) lookup) ----
        const int32 gX = FMath::Clamp(FMath::RoundToInt(Center.X / EffVoxelSize), 0, EffCS - 1);
        const int32 gY = FMath::Clamp(FMath::RoundToInt(Center.Y / EffVoxelSize), 0, EffCS - 1);

        FVoxelBiomeWeightMap TriWeights;
        const int32 CacheIdx = gX + gY * EffCS;
        if (ColumnWeights.IsValidIndex(CacheIdx))
            TriWeights = ColumnWeights[CacheIdx];
        else
            TriWeights = FVoxelBiomeManager::GetBiomeWeightsStatic(WorldCenter.X, WorldCenter.Y, Config);

        // ---- Per-biome foliage system ----
        if (bHasPerBiomeFoliage)
        {
            for (int32 s = 0; s < FoliageSlots.Num(); ++s)
            {
                const FFoliageSlot&   Slot  = FoliageSlots[s];
                if (!Slot.Mesh) continue;

                const FVoxelFoliageEntry& Entry = Config.GetBiomeRender(Slot.Biome).FoliageTypes[Slot.EntryIdx];

                if (SlopeZ < Entry.MinSlopeAlignment) continue;
                if (TriWeights.GetWeight(Slot.Biome) < Entry.MinBiomeWeight) continue;
                if (WorldCenter.Z < Entry.MinWorldZ || WorldCenter.Z > Entry.MaxWorldZ) continue;

                for (int32 Attempt = 0; Attempt < Entry.SpawnAttemptsPerTriangle; ++Attempt)
                {
                    if (FMath::FRand() >= Entry.SpawnChance) continue;

                    // Uniform random point inside the triangle (barycentric method).
                    float r1 = FMath::FRand(), r2 = FMath::FRand();
                    if (r1 + r2 > 1.f) { r1 = 1.f - r1; r2 = 1.f - r2; }
                    const FVector SpawnPos = v0 + r1 * (v1 - v0) + r2 * (v2 - v0)
                                          + FVector(0.f, 0.f, Entry.HeightOffset);

                    FRotator Rot = FRotator::ZeroRotator;
                    if (Entry.bAlignToSurface)
                    {
                        Rot = FaceNormal.ToOrientationRotator();
                        Rot.Pitch += 90.f; // face-normal -> standing-on-normal
                    }
                    Rot.Yaw = Entry.bRandomYaw ? FMath::FRand() * 360.f : Entry.FixedYaw;

                    const float Scale = FMath::FRandRange(Entry.ScaleMin, Entry.ScaleMax);
                    PerFoliageTransforms[s].Add(FTransform(Rot, SpawnPos, FVector(Scale)));
                }
            }
        }
        else
        {
            // ---- Legacy fallback (no per-biome foliage configured) ----
            if (SlopeZ < MaxFoliageSlope) continue;

            // Use cached biome weights; vertex color is biome blend for materials, not Forest/Skyland weights.
            const float ForestW = TriWeights.GetWeight(EVoxelBiome::Forest);
            const float RoughW  = TriWeights.GetRoughness(); // grass on varied terrain (peaks/cliffs)

            if (FMath::FRand() < FoliageDensity * 5.f * (ForestW + RoughW * 0.5f))
            {
                LegacyGrassTransforms.Add(FTransform(
                    FRotator(0.f, FMath::FRand() * 360.f, 0.f),
                    Center,
                    FVector(FMath::FRandRange(0.8f, 1.2f))));
            }

            if (ForestW > 0.5f && FMath::FRand() < FoliageDensity)
            {
                LegacyTreeTransforms.Add(FTransform(
                    FRotator(0.f, FMath::FRand() * 360.f, 0.f),
                    Center,
                    FVector(FMath::FRandRange(0.7f, 1.4f))));
            }
        }
    }
}
