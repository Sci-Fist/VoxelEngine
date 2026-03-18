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
#include "Biomes/VoxelBiomeGenerators.h"
#include "Async/ParallelFor.h"
#include "VoxelLogger.h"

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

    if (bCancelled || bIsFullSolid || bIsFullAir) return;
    BuildMesh();

    if (bCancelled) return;
    CalculateFoliage();

    if (bCancelled) return;
    PlaceWaterSources();
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
    // Parallelized across both X and Y dimensions to fully utilize multi-core CPUs.
    ParallelFor(EffectiveSize * EffectiveSize, [&](int32 Index)
    {
        if (bCancelled) return;

        const int32 Y = Index / EffectiveSize;
        const int32 X = Index % EffectiveSize;

        const float WorldX = WorldOrigin.X + (X - 1.f) * EffVoxelSize;
        const float WorldY = WorldOrigin.Y + (Y - 1.f) * EffVoxelSize;

        // ---- Per-column work (O(n^2)) ----
        // Biome weights and surface height are the same for the entire
        // vertical column, so they are computed once here and reused
        // for every Z below.
        const FVoxelBiomeWeightMap Weights       = Provider->GetBiomeWeights(WorldX, WorldY, Config);
        const float               SurfaceHeight  = FVoxelBiomeManager::GetSurfaceHeightStatic(
                                                      WorldX, WorldY, Weights, Config);

        // Neutral layout for Crystal Caverns to prevent breacking into crater floor
        FVoxelBiomeWeightMap CavernWeights = Weights;
        CavernWeights.SetWeight(EVoxelBiome::Craters, 0.f);
        CavernWeights.Normalize();
        const float NeutralSurfaceHeight = FVoxelBiomeManager::GetSurfaceHeightStatic(WorldX, WorldY, CavernWeights, Config);

        // Cache Skyland data for the column to avoid running 2D cellular lists for every Z step.
        const FSkylandColumnCache SkylandCache = FVoxelBiomeGenerators::GetSkylandColumnCache(
            WorldX, WorldY, SurfaceHeight, Weights, Config);

        // Cache weights for foliage pass (inner non-padding columns only).
        const int32 LX = X - 1, LY = Y - 1;
        if (LX >= 0 && LX < EffCS && LY >= 0 && LY < EffCS)
            ColumnWeights[LX + LY * EffCS] = Weights;

        // ---- Column Range Checks for Early-Out (Area 2 Optimization) ----
        const float MinWorldZ = WorldOrigin.Z - EffVoxelSize;
        const float MaxWorldZ = WorldOrigin.Z + (EffectiveSize + 1) * EffVoxelSize;

        // 1. Bedrock fully solid check
        if (MaxWorldZ < Config.CaveTunnels.BedrockDepth)
        {
            for (int32 Z = 0; Z < EffectiveSize; ++Z)
            {
                const int32 Idx = X + Y * EffectiveSize + Z * EffectiveSize * EffectiveSize;
                Densities[Idx] = 2.0f;
            }
            return;
        }

        // 2. Air column check (above surface, below skylands)
        const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
        const float OverhangMaxDist = Config.Performance.bEnableOverhangs ? Config.Overhangs.MaxDistFromSurface : 0.f;
        const float SafeAirMinZ = SurfaceHeight + OverhangMaxDist + 200.f;
        
        const float SkyLowerBound = SurfaceHeight
            + SC.MinAltitudeAboveTerrain
            - (SC.BaseIslandSize * SC.ThicknessRatio)
            - 400.f;

        if (MinWorldZ > SafeAirMinZ && MaxWorldZ < SkyLowerBound)
        {
            for (int32 Z = 0; Z < EffectiveSize; ++Z)
            {
                const int32 Idx = X + Y * EffectiveSize + Z * EffectiveSize * EffectiveSize;
                Densities[Idx] = -2.0f; // constant air
            }
            return;
        }

        // ---- Per-voxel work (O(n^3)) ----
        for (int32 Z = 0; Z < EffectiveSize; ++Z)
        {
            const float WorldZ = WorldOrigin.Z + (Z - 1.f) * EffVoxelSize;
            const int32 Idx    = X + Y * EffectiveSize + Z * EffectiveSize * EffectiveSize;

            float D = Provider->GetDensityFull(
                FVector(WorldX, WorldY, WorldZ), Weights, SurfaceHeight, NeutralSurfaceHeight, Config, StepSize, &SkylandCache);

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
    });

    CountDensityStates(TotalSamples);
    PostProcessDensities(TotalSamples);
}

void FVoxelGeneratorTask::PostProcessDensities(int32 TotalSamples)
{
    // FIX: The old implementation clamped extreme densities to ±1.5 when a chunk
    // was >90% solid or air. This was harmful:
    //   - Clamping high-density values shifts the isosurface position — terrain
    //     appears to thin or inflate depending on gradient direction.
    //   - Deep underground chunks are legitimately 100% solid; clamping them
    //     produces phantom surface vertices in chunks that should be inert.
    //   - The bIsFullSolid / bIsFullAir guards in Execute() already skip mesh
    //     generation for entirely uniform chunks.
    //
    // PostProcessDensities now does nothing except exist as a named hook so
    // future per-voxel post-passes (erosion, smoothing) have a clear place to go.
    // The bIsFullSolid / bIsFullAir early-outs in Execute() are sufficient.
}




// ============================================================
//  BuildMesh
// ============================================================
void FVoxelGeneratorTask::BuildMesh()
{
    MeshOutput.Reset();
    FVoxelMeshGenerator::GenerateMesh(
        Densities, ChunkSize, VoxelSize, WorldOrigin, MeshOutput, Config, StepSize);

	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelMesh: Chunk X=%d Y=%d Z=%d Verts=%d"),
		ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, MeshOutput.FlatMesh.Vertices.Num()));
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

    if (bIsFullAir || bIsFullSolid) return;

    // Size output arrays to match the pre-cached slot count.
    PerFoliageTransforms.SetNum(FoliageSlots.Num());
    PerFoliageMeshes.SetNum(FoliageSlots.Num());
    for (int32 s = 0; s < FoliageSlots.Num(); ++s)
        PerFoliageMeshes[s] = FoliageSlots[s].Mesh;

    const int32 EffCS        = ChunkSize / StepSize;
    const float EffVoxelSize = VoxelSize * StepSize;
    const int32 EffectiveSize = EffCS;

    auto RandHashFloat = [](int32 x, int32 y, int32 s, int32 seed) -> float
    {
        uint32 h = (uint32)(x * 73856093 ^ y * 19349663 ^ s * 83492791 ^ seed);
        h = (h ^ (h >> 16)) * 0x45d9f3b;
        h = (h ^ (h >> 16));
        return (float)(h & 0xFFFFFF) / 16777215.f; // 0xFFFFFF
    };

    // Iterate the 2D inner columns
    for (int32 LY = 0; LY < EffCS; ++LY)
    for (int32 LX = 0; LX < EffCS; ++LX)
    {
        const int32 CacheIdx = LX + LY * EffCS;
        if (!ColumnWeights.IsValidIndex(CacheIdx)) continue;

        const FVoxelBiomeWeightMap& weights = ColumnWeights[CacheIdx];
        const float SurfaceHeight = FVoxelBiomeManager::GetSurfaceHeightStatic(
            WorldOrigin.X + LX * EffVoxelSize, WorldOrigin.Y + LY * EffVoxelSize, weights, Config);

        // Map height to local cell Z for normal lookup
        const int32 CellZ = FMath::Clamp(FMath::RoundToInt((SurfaceHeight - WorldOrigin.Z) / EffVoxelSize) + 1, 1, EffectiveSize + 1);
        const FVector Normal = FVoxelMeshGenerator::ComputeNormal(Densities, LX + 1, LY + 1, CellZ, EffectiveSize);
        const float SlopeZ = Normal.Z;

        const FVector ColumnWorldPos(WorldOrigin.X + LX * EffVoxelSize, WorldOrigin.Y + LY * EffVoxelSize, SurfaceHeight);

        if (bHasPerBiomeFoliage)
        {
            for (int32 s = 0; s < FoliageSlots.Num(); ++s)
            {
                const FFoliageSlot& Slot = FoliageSlots[s];
                if (!Slot.Mesh) continue;

                const FVoxelFoliageEntry& Entry = Config.GetBiomeRender(Slot.Biome).FoliageTypes[Slot.EntryIdx];

                if (SlopeZ < Entry.MinSlopeAlignment) continue;
                if (weights.GetWeight(Slot.Biome) < Entry.MinBiomeWeight) continue;
                if (ColumnWorldPos.Z < Entry.MinWorldZ || ColumnWorldPos.Z > Entry.MaxWorldZ) continue;

                const int32 Attempts = Entry.SpawnAttemptsPerTriangle; // keep standard as base
                for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
                {
                    const float Roll = RandHashFloat(LX, LY, s * 100 + Attempt, Config.Seed);
                    if (Roll >= Entry.SpawnChance) continue;

                    // Deterministic jitter inside the column box
                    const float JitterX = RandHashFloat(LX, LY, Attempt * 7, Config.Seed) * EffVoxelSize;
                    const float JitterY = RandHashFloat(LX, LY, Attempt * 13, Config.Seed) * EffVoxelSize;

                    const FVector LocalPos = FVector(
                        LX * EffVoxelSize + JitterX, 
                        LY * EffVoxelSize + JitterY, 
                        SurfaceHeight - WorldOrigin.Z + Entry.HeightOffset);

                    FRotator Rot = FRotator::ZeroRotator;
                    if (Entry.bAlignToSurface)
                    {
                        Rot = Normal.ToOrientationRotator();
                        Rot.Pitch += 90.f;
                    }
                    Rot.Yaw = Entry.bRandomYaw ? RandHashFloat(LX, LY, Attempt * 19, Config.Seed) * 360.f : Entry.FixedYaw;

                    const float Scale = FMath::Lerp(Entry.ScaleMin, Entry.ScaleMax, RandHashFloat(LX, LY, Attempt * 23, Config.Seed));

                    PerFoliageTransforms[s].Add(FTransform(Rot, LocalPos, FVector(Scale)));
                }
            }
        }
    }
// End of grid-based foliage generation

    const int32 MaxMeshesPerChunk = 15000;
    TrimFoliageToCap(MaxMeshesPerChunk);
}

void FVoxelGeneratorTask::ProcessLegacyFoliage(const FVector& Center, float SlopeZ, const FVoxelBiomeWeightMap& TriWeights, const FVector& WorldCenter)
{
    // ---- Legacy fallback (no per-biome foliage configured) ----
    if (SlopeZ < MaxFoliageSlope) return;
    
    // FIX: Don't spawn legacy foliage in/near water
    if (WorldCenter.Z <= Config.SeaLevel + 200.f) return;

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


// ============================================================
//  PlaceWaterSources
//  Scans the completed density field for:
//    (a) Surface depressions — air voxels sitting directly on solid terrain
//        that are enclosed enough to hold a pool (3+ solid cardinal neighbours).
//    (b) Skyland flat surfaces — solid voxels in the skyland altitude band
//        whose top face is air, indicating a flat island surface suitable for pools.
//
//  Water source probability is gated by the biome water config so each biome
//  has its own lake/pool density (Craters ~90%, Desert ~6%, etc.).
// ============================================================
void FVoxelGeneratorTask::PlaceWaterSources()
{
    WaterSources.Reset();

    // Only full-resolution (LOD 0) chunks get water sources.
    if (StepSize > 1) return;

    const int32 CS  = ChunkSize;
    const int32 S   = CS + 3; // density array stride (with +3 padding)
    const int32 EffCS = CS / StepSize;

    // Helper: get density at local voxel (0..CS-1 range), accounts for +1 padding offset
    auto Dens = [&](int32 lx, int32 ly, int32 lz) -> float
    {
        const int32 px = FMath::Clamp(lx + 1, 0, S - 1);
        const int32 py = FMath::Clamp(ly + 1, 0, S - 1);
        const int32 pz = FMath::Clamp(lz + 1, 0, S - 1);
        return Densities[px + py * S + pz * S * S];
    };

    auto IsSolid = [&](int32 lx, int32 ly, int32 lz) -> bool { return Dens(lx, ly, lz) > 0.f; };
    auto IsAir   = [&](int32 lx, int32 ly, int32 lz) -> bool { return Dens(lx, ly, lz) <= 0.f; };

    // Sea level in local-voxel Z coordinates (WorldOrigin.Z + lz * VoxelSize = SeaLevel)
    const float SeaLevelLocal = (Config.SeaLevel - WorldOrigin.Z) / VoxelSize;

    // Per-column biome water configs for fast lookup
    const int32 EffS = EffCS;

    // Deterministic hash for per-voxel probability roll (avoids FMath::FRand on bg thread)
    auto RandHash = [](int32 x, int32 y, int32 z, int32 seed) -> float
    {
        uint32 h = (uint32)(x * 73856093 ^ y * 19349663 ^ z * 83492791 ^ seed);
        h = (h ^ (h >> 16)) * 0x45d9f3b;
        h = (h ^ (h >> 16));
        return (float)(h & 0xFFFFFF) / (float)0xFFFFFF;
    };

    // ---- Scan interior voxels (skip padding border) ----
    for (int32 lz = 0; lz < CS; ++lz)
    for (int32 ly = 0; ly < CS; ++ly)
    for (int32 lx = 0; lx < CS; ++lx)
    {
        if (bCancelled) return;

        // Only place water in air cells
        if (!IsAir(lx, ly, lz)) continue;

        // Must have solid directly below (resting surface)
        if (!IsSolid(lx, ly, lz - 1)) continue;

        // Skip cells that are at or below sea level — ocean handles those
        const float WorldZ = WorldOrigin.Z + lz * VoxelSize;
        if (!Config.Water.bUseVoxelOcean && Config.Water.bEnableOcean && WorldZ <= Config.SeaLevel + VoxelSize) continue;

        // Counts solid cardinal horizontal neighbours to gauge enclosure
        int32 SolidNeighbours = 0;
        if (IsSolid(lx + 1, ly, lz)) ++SolidNeighbours;
        if (IsSolid(lx - 1, ly, lz)) ++SolidNeighbours;
        if (IsSolid(lx, ly + 1, lz)) ++SolidNeighbours;
        if (IsSolid(lx, ly - 1, lz)) ++SolidNeighbours;

        const float MinSkyAlt = Config.SeaLevel + Config.SkylandsLayer.MinAltitudeAboveTerrain;
        const bool bIsSkylands = WorldZ >= MinSkyAlt;

        // ---- ☁️ INJECT CAVE/SKYLAND CEILING MASKING ----
        const bool bIsOpenOcean = (WorldZ <= Config.SeaLevel + VoxelSize);
        // Inside a cave, the ceiling above would be solid terrain
        const bool bIsCave = IsSolid(lx, ly, lz+1); 

        // Skip below SeaLevel ONLY FOR OPEN OCEAN. Caves below ocean can spawn streams!
        if (Config.Water.bEnableOcean && bIsOpenOcean && !bIsCave) continue;

        // ---- (a) Surface & Skyland Depression pool detection ----
        // Require at least 2 solid walls around the cell so water doesn’t
        // immediately drain. Depression = somewhat enclosed air on solid ground.
        if (SolidNeighbours >= 2)
        {
            float SpawnChance = 0.f;

            if (bIsSkylands)
            {
                // Skyland rule: Pull config directly bypassing surface biome lookup
                const FVoxelBiomeWaterConfig& BWC = Config.SkylandsWater;
                if (!BWC.bEnableLakes) continue;

                // Enforce minimum thickness beneath pool base
                if (!IsSolid(lx, ly, lz - 2)) continue;

                SpawnChance = FMath::Clamp(BWC.LakeSpawnProbability * 0.12f, 0.f, 1.f);
            }
            else
            {
                // Standard Surface check: Get biome weights at this column
                const int32 gX = FMath::Clamp(lx, 0, EffS - 1);
                const int32 gY = FMath::Clamp(ly, 0, EffS - 1);
                FVoxelBiomeWeightMap W;
                const int32 CacheIdx = gX + gY * EffS;
                if (ColumnWeights.IsValidIndex(CacheIdx))
                    W = ColumnWeights[CacheIdx];

                const EVoxelBiome Dom = W.GetDominantBiome();
                const FVoxelBiomeWaterConfig& BWC = Config.GetBiomeWater(Dom);

                if (!BWC.bEnableLakes) continue;

                const float EnclosureFactor = (SolidNeighbours == 4) ? 1.5f : (SolidNeighbours == 3) ? 1.1f : 0.7f;
                SpawnChance = FMath::Clamp(BWC.LakeSpawnProbability * EnclosureFactor * 0.15f, 0.f, 1.f);
            }

            if (RandHash(lx, ly, lz, Config.Seed) < SpawnChance)
            {
                const FIntVector WV(
                    ChunkCoord.X * CS + lx,
                    ChunkCoord.Y * CS + ly,
                    ChunkCoord.Z * CS + lz);
                WaterSources.Add(WV);
            }
        }
    }
}

// ============================================================
//  CountDensityStates
//  Sums total solid/air voxels to detect full chunks.
// ============================================================
void FVoxelGeneratorTask::CountDensityStates(int32 TotalSamples)
{
    int32 SolidCount = 0;
    int32 AirCount = 0;

    for (int32 i = 0; i < TotalSamples; ++i)
    {
        if (Densities[i] > 0.0f) SolidCount++;
        else AirCount++;
    }

    bIsFullSolid = (SolidCount == TotalSamples);
    bIsFullAir   = (AirCount == TotalSamples);
}

// ============================================================
//  TrimFoliageToCap
//  Truncates slots to preserve GPU ceiling.
// ============================================================
void FVoxelGeneratorTask::TrimFoliageToCap(const int32 MaxMeshesPerChunk)
{
    int32 TotalInstances = LegacyTreeTransforms.Num() + LegacyGrassTransforms.Num();
    for (const auto& SlotTransforms : PerFoliageTransforms)
        TotalInstances += SlotTransforms.Num();

    if (TotalInstances > MaxMeshesPerChunk)
    {
        UE_LOG(LogVoxelChunk, Warning,
            TEXT("Chunk [%d,%d,%d] foliage cap hit: %d > %d — truncating."),
            ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, TotalInstances, MaxMeshesPerChunk);

        for (auto& SlotTransforms : PerFoliageTransforms)
        {
            if (TotalInstances <= MaxMeshesPerChunk) break;
            const int32 Excess   = TotalInstances - MaxMeshesPerChunk;
            const int32 TrimThis = FMath::Min(Excess, SlotTransforms.Num());
            if (TrimThis > 0)
            {
                SlotTransforms.RemoveAt(SlotTransforms.Num() - TrimThis, TrimThis);
                TotalInstances -= TrimThis;
            }
        }

        if (TotalInstances > MaxMeshesPerChunk)
        {
            const int32 Trim = FMath::Min(TotalInstances - MaxMeshesPerChunk, LegacyGrassTransforms.Num());
            LegacyGrassTransforms.RemoveAt(LegacyGrassTransforms.Num() - Trim, Trim);
            TotalInstances -= Trim;
        }
    }
}
