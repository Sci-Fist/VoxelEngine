// VoxelGeneratorTask.cpp
//
// ROOT FIX applied here (pre-compute loop + main density loop):
//
// Pre-compute loop was calling:
//   GetSkylandColumnCache(CX, CY, SH, W, Config)
// where SH = GetSurfaceHeightStatic() = CRATER-MODIFIED height.
// Inside the crater floor (SH ≈ 1840cm), HeightNorm = 0.07 → islands scheduled
// at SkyAlt ≈ 9840cm, buried inside solid crater wall material.
//
// Fix: pass NeutralSurfaceHeight = GetNeutralSurfaceHeightStatic() (pre-crater)
// → HeightNorm ≈ 0.39 → SkyAlt ≈ 19840cm → visible above the crater rim.
//
// Also fixed the SkyLB early-out which had wrong percentage-scaling constants.
// SkyLB now uses NeutralSurfaceHeight so the lower-bound correctly reflects
// where islands will actually appear, not where the crater floor is.

#include "Generation/VoxelGeneratorTask.h"
#include "Generation/VoxelMeshGenerator.h"
#include "Generation/VoxelDensityGenerator.h"
#include "Generation/IVoxelGenerationStage.h"
#include "Voxel/Core/VoxelDataMap.h"
#include "Biomes/VoxelBiomeManager.h"
#include "Biomes/VoxelBiomeGenerators.h"
#include "Async/ParallelFor.h"
#include "Templates/Atomic.h"
#include "VoxelLogger.h"

static FCriticalSection      GDensityPoolLock;
static TArray<TArray<float>> GDensityPool;

static const EVoxelBiome GBiomeOrder[] =
{
    EVoxelBiome::Forest, EVoxelBiome::Peaks, EVoxelBiome::Cliffs,
    EVoxelBiome::Mesa,   EVoxelBiome::Craters, EVoxelBiome::Desert, EVoxelBiome::Ocean,
};
static_assert(UE_ARRAY_COUNT(GBiomeOrder) == FVoxelBiomeWeightMap::MaxBiomes,
    "GBiomeOrder must contain exactly one entry per EVoxelBiome value.");

// ============================================================
//  Constructor
// ============================================================
FVoxelGeneratorTask::FVoxelGeneratorTask(
    const FIntVector& InChunkCoord, const FVector& InWorldOrigin,
    int32 InChunkSize, float InVoxelSize, int32 InStepSize,
    const FVoxelGenerationConfig& InConfig, IVoxelDensityProvider* InProvider,
    float InFoliageDensity, float InMaxFoliageSlope, struct FVoxelDataMap* InDataMap)
    : ChunkCoord(InChunkCoord), WorldOrigin(InWorldOrigin), ChunkSize(InChunkSize)
    , VoxelSize(InVoxelSize), StepSize(InStepSize), Config(InConfig)
    , DensityProvider(InProvider), FoliageDensity(InFoliageDensity)
    , MaxFoliageSlope(InMaxFoliageSlope), DataMap(InDataMap)
{
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

FVoxelGeneratorTask::~FVoxelGeneratorTask()
{
    if (Densities.Num() > 0)
    {
        FScopeLock Lock(&GDensityPoolLock);
        if (GDensityPool.Num() < 12)
            GDensityPool.Add(MoveTemp(Densities));
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
    ComputeWaterColumns(); // OPT-4: pre-bake water column data on background thread
    if (bCancelled) return;
    if (StepSize > 1) return; // Skip foliage and water on distant silhouette chunks
    CalculateFoliage();
    if (bCancelled) return;
    PlaceWaterSources();
}

// ============================================================
//  BuildDensityField
// ============================================================
void FVoxelGeneratorTask::BuildDensityField()
{
    const int32 EffCS        = ChunkSize / StepSize;
    this->EffSize            = EffCS + 3;  // PERF-2: write to member so ComputeWaterColumns can use it
    const int32 TotalSamples = EffSize * EffSize * EffSize;
    const float EffVoxSz     = VoxelSize * (float)StepSize;

    {
        FScopeLock Lock(&GDensityPoolLock);
        if (GDensityPool.Num() > 0)
        {
            Densities = MoveTemp(GDensityPool.Last());
            GDensityPool.RemoveAt(GDensityPool.Num()-1, 1, EAllowShrinking::No);
        }
    }
    Densities.SetNumUninitialized(TotalSamples);

    // Speedup Tier 3: Disable sub-voxel overhangs and caves on distant high-LOD chunks (silhouette only)
    FVoxelGenerationConfig LocalConfig = Config;
    if (StepSize > 1)
    {
        LocalConfig.Performance.bEnableCaves = false;
        LocalConfig.Performance.bEnableOverhangs = false;
        LocalConfig.Performance.MaxNoiseOctaves = (StepSize >= 4) ? 1 : 2;
    }

    static FVoxelDensityGenerator FallbackGen;
    IVoxelDensityProvider* Provider = DensityProvider ? DensityProvider : &FallbackGen;

    const FVoxelSurfacePass SurfacePass;
    const FVoxelCavePass    CavePass;
    const FVoxelSkylandPass SkylandPass;

    const bool bEnForest   = LocalConfig.Performance.bEnableForest;
    const bool bEnDesert   = LocalConfig.Performance.bEnableDesert;
    const bool bEnPeaks    = LocalConfig.Performance.bEnablePeaks;
    const bool bEnCliffs   = LocalConfig.Performance.bEnableCliffs;
    const bool bEnMesa     = LocalConfig.Performance.bEnableMesa;
    const bool bEnCraters  = LocalConfig.Performance.bEnableCraters;
    const bool bEnSurface  = LocalConfig.Performance.bEnableSurface;
    const bool bEnCaves    = LocalConfig.Performance.bEnableCaves;
    const bool bEnSkylands = LocalConfig.Performance.bEnableSkylands;

    // PERF-2: PrecalcColumns is a member TArray; EffSize is a member set above.
    // ComputeWaterColumns() will read directly from them after BuildDensityField returns.
    PrecalcColumns.SetNum(EffSize * EffSize);
    ColumnWeights .SetNum(EffSize * EffSize);
    ColumnSurfaceH.SetNum(EffSize * EffSize);

    // OPT-1: Parallelise the per-column pre-compute loop.
    // All called functions are pure/stateless — safe for concurrent execution.
    // SkylandTaskCache (intra-chunk dedup TMap) removed: it required shared mutable
    // state and the benefit was marginal vs the cost of a mutex or lock-free workaround.
    ParallelFor(EffSize * EffSize, [&](int32 Idx)
    {
        const int32 Y  = Idx / EffSize;
        const int32 X  = Idx % EffSize;
        const float CX = FMath::RoundToFloat(WorldOrigin.X + (X - 1.f) * EffVoxSz);
        const float CY = FMath::RoundToFloat(WorldOrigin.Y + (Y - 1.f) * EffVoxSz);

        FColumnCacheItem& Item = PrecalcColumns[Idx];

        // OPT-3: Get biome weights via GetBiomeWeightsStatic directly so we also
        // capture Temp/Erosion in one call. We then apply the same performance-flag
        // masking, saving 2 redundant Perlin2D evaluations vs calling GetBiomeWeights
        // (provider) + a separate GetBiomeWeightsStatic for Temp/Erosion.
        float Temp = -999.f, Erosion = -999.f;
        Item.Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(CX, CY, LocalConfig, &Temp, &Erosion);
        if (!bEnForest)  Item.Weights.SetWeight(EVoxelBiome::Forest,  0.f);
        if (!bEnDesert)  Item.Weights.SetWeight(EVoxelBiome::Desert,  0.f);
        if (!bEnPeaks)   Item.Weights.SetWeight(EVoxelBiome::Peaks,   0.f);
        if (!bEnCliffs)  Item.Weights.SetWeight(EVoxelBiome::Cliffs,  0.f);
        if (!bEnMesa)    Item.Weights.SetWeight(EVoxelBiome::Mesa,    0.f);
        if (!bEnCraters) Item.Weights.SetWeight(EVoxelBiome::Craters, 0.f);
        Item.Weights.Normalize();

        Item.SurfH    = FVoxelBiomeManager::GetSurfaceHeightStatic(CX, CY, Item.Weights, LocalConfig, Temp, Erosion);
        // OPT-3: pass cached Temp/Erosion — avoids 2× redundant Perlin2D per column
        Item.NeutralH = FVoxelBiomeManager::GetNeutralSurfaceHeightStatic(CX, CY, LocalConfig, Temp, Erosion);

        // No shared SkylandTaskCache — each column builds its FSkylandColumnCache independently
        Item.SkylandCache = FVoxelBiomeGenerators::GetSkylandColumnCache(CX, CY, Item.NeutralH, Item.Weights, LocalConfig);

        // Backward-compat arrays (written at same Idx — no race condition)
        ColumnWeights[Idx]  = Item.Weights;
        ColumnSurfaceH[Idx] = Item.SurfH;
    });

    // ── DataMap edit arrays ────────────────────────────────────────────────
    TArray<bool>  DenseHasEdit;
    TArray<float> DenseEditVals;
    if (DataMap)
    {
        DenseHasEdit .Init(false, TotalSamples);
        DenseEditVals.Init(0.f,   TotalSamples);

        for (int32 cz=-1; cz<=1; ++cz)
        for (int32 cy=-1; cy<=1; ++cy)
        for (int32 cx=-1; cx<=1; ++cx)
        {
            const FIntVector TCC = ChunkCoord + FIntVector(cx, cy, cz);
            TMap<int32,float> MV;
            if (!DataMap->GetChunkData(TCC, MV)) continue;

            const float BaseX = WorldOrigin.X + (float)(TCC.X - ChunkCoord.X) * ChunkSize * VoxelSize;
            const float BaseY = WorldOrigin.Y + (float)(TCC.Y - ChunkCoord.Y) * ChunkSize * VoxelSize;
            const float BaseZ = WorldOrigin.Z + (float)(TCC.Z - ChunkCoord.Z) * ChunkSize * VoxelSize;

            for (const auto& Pair : MV)
            {
                const int32 LIdx = Pair.Key;
                const int32 lz   = LIdx / (ChunkSize*ChunkSize);
                const int32 ly   = (LIdx / ChunkSize) % ChunkSize;
                const int32 lx   = LIdx % ChunkSize;

                const float AX = BaseX + lx * VoxelSize;
                const float AY = BaseY + ly * VoxelSize;
                const float AZ = BaseZ + lz * VoxelSize;

                const int32 GX = FMath::RoundToInt((AX - WorldOrigin.X) / EffVoxSz + 1.f);
                const int32 GY = FMath::RoundToInt((AY - WorldOrigin.Y) / EffVoxSz + 1.f);
                const int32 GZ = FMath::RoundToInt((AZ - WorldOrigin.Z) / EffVoxSz + 1.f);

                if (GX<0||GX>=EffSize||GY<0||GY>=EffSize||GZ<0||GZ>=EffSize) continue;
                const int32 FI = GX + GY*EffSize + GZ*EffSize*EffSize;
                DenseHasEdit [FI] = true;
                DenseEditVals[FI] = Pair.Value;
            }
        }
    }

    // ── Main density loop ─────────────────────────────────────────────────
    // PERF-3: plain int32 — the loop is serial, TAtomic<int32> added lock-prefix
    // overhead (lock xadd per increment) with zero thread-safety benefit here.
    TAtomic<int32> AtomicSolid(0);
    TAtomic<int32> AtomicAir(0);

    ParallelFor(EffSize * EffSize, [&](int32 FlatXY)
    {
        if (bCancelled) return;
        int32 LocalSolid = 0;
        int32 LocalAir   = 0;

        const int32 Y  = FlatXY / EffSize;
        const int32 X  = FlatXY % EffSize;
        const float WX = FMath::RoundToFloat(WorldOrigin.X + (X-1.f)*EffVoxSz);
        const float WY = FMath::RoundToFloat(WorldOrigin.Y + (Y-1.f)*EffVoxSz);

        const FColumnCacheItem& Item = PrecalcColumns[FlatXY];
        const FVoxelBiomeWeightMap Weights = Item.Weights;
        const float SurfH    = Item.SurfH;
        const float NeutralH = Item.NeutralH;

        const float MaxWZ = WorldOrigin.Z + (EffSize+1)*EffVoxSz;
        const float MinWZ = WorldOrigin.Z - EffVoxSz;

        FColumnContext Ctx;
        Ctx.SurfaceHeight        = SurfH;
        Ctx.NeutralSurfaceHeight = NeutralH;  // passed through to CavePass and SkylandPass
        Ctx.BiomeWeights         = Weights;
        Ctx.MaxWorldZ            = MaxWZ;
        Ctx.CachedSeedOffset     = LocalConfig.GetSeedOffset();
        Ctx.SkylandCache         = Item.SkylandCache;
        Ctx.StepSize             = StepSize;

        if (bEnSurface) SurfacePass.PrepareColumn(WX, WY, LocalConfig, Ctx);
        // CavePass.PrepareColumn sets NeutralSurfaceHeight via GetNeutralSurfaceHeightStatic
        // It will overwrite our value — that's fine, both calls produce the same result.
        if (bEnCaves)   CavePass   .PrepareColumn(WX, WY, LocalConfig, Ctx);

        if (bEnSkylands)
        {
            const FSkylandsLayerConfig& SC = LocalConfig.SkylandsLayer;

            // ROOT FIX: use NeutralH for SkyLB.
            // Old: used SurfH (crater floor = 1840cm) → SkyLB = ~8240cm
            //      → chunks covering 8240-16240cm got skylands → buried in wall.
            // New: uses NeutralH (~9840cm) → SkyLB = ~16240cm
            //      → only chunks above 16240cm get skylands → visible above rim.
            const float SkyLB = NeutralH + SC.MinAltitudeAboveTerrain
                              - SC.BaseIslandSize * SC.ThicknessRatio - 1000.f;

            if (MaxWZ < SkyLB)
                Ctx.SkylandCache.bHasSkyland = false;
            else
            {
                // Already assigned from Item.SkylandCache
            }

            // Early-out for pure-air columns well below the skyland band
            const float OvH     = LocalConfig.Performance.bEnableOverhangs ? LocalConfig.Overhangs.MaxDistFromSurface : 0.f;
            const float SkyLB2  = NeutralH + SC.MinAltitudeAboveTerrain
                                - SC.BaseIslandSize * SC.ThicknessRatio - 400.f;
            if (MinWZ > SurfH + OvH + 200.f && MaxWZ < SkyLB2)
            {
                for (int32 Z=0; Z<EffSize; ++Z)
                {
                    const int32 Idx = X+Y*EffSize+Z*EffSize*EffSize;
                    float D = -2.f;
                    if (!DenseHasEdit.IsEmpty() && DenseHasEdit[Idx])
                    { const float Ov = DenseEditVals[Idx]; D = (Ov<0.f) ? FMath::Min(D,Ov) : FMath::Max(D,Ov); }
                    Densities[Idx] = D;
                    if (D > 0.f) LocalSolid++; else LocalAir++;
                }
                if (LocalSolid > 0) AtomicSolid += LocalSolid;
                if (LocalAir > 0)   AtomicAir   += LocalAir;
                return;
            }
        }

        // Below-bedrock early-out
        if (MaxWZ < LocalConfig.CaveTunnels.BedrockDepth)
        {
            for (int32 Z=0; Z<EffSize; ++Z)
            {
                const int32 Idx = X+Y*EffSize+Z*EffSize*EffSize;
                float D = 2.f;
                if (!DenseHasEdit.IsEmpty() && DenseHasEdit[Idx])
                { const float Ov = DenseEditVals[Idx]; D = (Ov<0.f) ? FMath::Min(D,Ov) : FMath::Max(D,Ov); }
                Densities[Idx] = D;
                if (D > 0.f) LocalSolid++; else LocalAir++;
            }
            if (LocalSolid > 0) AtomicSolid += LocalSolid;
            if (LocalAir > 0)   AtomicAir   += LocalAir;
            return;
        }

        // Cascade upsampling cache
        float LastD = -2.f;
        const int32 ZStep = 2; // 1=Off, 2=Fast, 4=Ultrafast

        for (int32 Z = 0; Z < EffSize; Z += ZStep)
        {
            const float WZ0  = WorldOrigin.Z + (Z-1.f)*EffVoxSz;
            float D0 = LastD;
            
            if (Z == 0) // First step exact eval
            {
                D0 = -2.f;
                if (bEnSurface)  D0 = SurfacePass.EvaluateVoxel(FVector(WX,WY,WZ0), Ctx, LocalConfig, D0);
                if (bEnCaves)    D0 = CavePass   .EvaluateVoxel(FVector(WX,WY,WZ0), Ctx, LocalConfig, D0);
                if (bEnSkylands) D0 = SkylandPass.EvaluateVoxel(FVector(WX,WY,WZ0), Ctx, LocalConfig, D0);
            }

            const int32 EdgeZ = FMath::Min(Z + ZStep, EffSize - 1);
            const float WZ1  = WorldOrigin.Z + (EdgeZ-1.f)*EffVoxSz;
            
            float D1 = -2.f;
            if (bEnSurface)  D1 = SurfacePass.EvaluateVoxel(FVector(WX,WY,WZ1), Ctx, LocalConfig, D1);
            if (bEnCaves)    D1 = CavePass   .EvaluateVoxel(FVector(WX,WY,WZ1), Ctx, LocalConfig, D1);
            if (bEnSkylands) D1 = SkylandPass.EvaluateVoxel(FVector(WX,WY,WZ1), Ctx, LocalConfig, D1);

            LastD = D1; // Save for next cycle

            const int32 Span = EdgeZ - Z;
            for (int32 i = 0; i <= Span; ++i)
            {
                const int32 CurrZ = Z + i;
                if (CurrZ >= EffSize) break;

                const int32 CurrIdx = X + Y*EffSize + CurrZ*EffSize*EffSize;
                float D = FMath::Lerp(D0, D1, (Span > 0) ? (float)i / Span : 0.f);

                if (!DenseHasEdit.IsEmpty() && DenseHasEdit[CurrIdx])
                {
                    const float Ov = DenseEditVals[CurrIdx];
                    D = (Ov < 0.f) ? FMath::Min(D, Ov) : FMath::Max(D, Ov);
                }
                Densities[CurrIdx] = D;
                if (D > 0.f) LocalSolid++; else LocalAir++;
            }
        }
        if (LocalSolid > 0) AtomicSolid += LocalSolid;
        if (LocalAir > 0)   AtomicAir   += LocalAir;
    });

    int32 SolidCount = AtomicSolid.Load();
    int32 AirCount   = AtomicAir.Load();

    bIsFullSolid = (SolidCount == TotalSamples);
    bIsFullAir   = (AirCount   == TotalSamples);
    PostProcessDensities(TotalSamples);
}

void FVoxelGeneratorTask::PostProcessDensities(int32) {}

// ============================================================
//  BuildMesh
// ============================================================
void FVoxelGeneratorTask::BuildMesh()
{
    MeshOutput.Reset();
    FVoxelMeshGenerator::GenerateMesh(
        Densities, ChunkSize, VoxelSize, WorldOrigin, MeshOutput, Config, StepSize,
        &ScratchBuffers,
        &ColumnWeights); // PERF-1: pass precomputed weights → ColumnColors skips noise
    UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelMesh: [%d,%d,%d] Flat=%d Slope=%d"),
        ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z,
        MeshOutput.FlatMesh.Vertices.Num(), MeshOutput.SlopeMesh.Vertices.Num()));
}

void FVoxelGeneratorTask::CountDensityStates(int32) {}

// ============================================================
//  ComputeWaterColumns — OPT-4 + PERF-2
//  PERF-2: Samples from PrecalcColumns built in BuildDensityField's parallel
//  precompute instead of calling 256 full noise evaluations serially.
//  Column mapping: (lx, ly) in [0, ChunkSize) → PrecalcColumns[(lx+1) + (ly+1)*EffSize]
//  The +1 accounts for the 1-cell border padding in the EffSize grid.
//  For higher LOD steps (StepSize>1): use lx/StepSize and ly/StepSize indices.
// ============================================================
void FVoxelGeneratorTask::ComputeWaterColumns()
{
    if (bCancelled) return;
    if (PrecalcColumns.Num() == 0 || EffSize == 0) return; // safety: BuildDensityField must have run

    const int32 CS = ChunkSize;
    WaterColOceanWeights  .SetNumZeroed(CS * CS);
    WaterColCraterWeights .SetNumZeroed(CS * CS);
    WaterColNeutralHeights.SetNumZeroed(CS * CS);
    WaterColSurfaceHeights.SetNumZeroed(CS * CS);

    // EffSize = ChunkSize/StepSize + 3 (with 1-cell border on each side).
    // For column (lx, ly) in [0, CS), the best PrecalcColumns index is:
    //   effX = lx/StepSize + 1   (clamped to [1, EffSize-2])
    //   effY = ly/StepSize + 1
    for (int32 ly = 0; ly < CS; ++ly)
    for (int32 lx = 0; lx < CS; ++lx)
    {
        if (bCancelled) return;
        const int32 EffX   = FMath::Clamp(lx / StepSize + 1, 0, EffSize - 1);
        const int32 EffY   = FMath::Clamp(ly / StepSize + 1, 0, EffSize - 1);
        const int32 EIdx   = EffX + EffY * EffSize;
        const FColumnCacheItem& Item = PrecalcColumns[EIdx];

        const int32 ColIdx = lx + ly * CS;
        WaterColOceanWeights  [ColIdx] = Item.Weights.GetWeight(EVoxelBiome::Ocean);
        WaterColCraterWeights [ColIdx] = Item.Weights.GetWeight(EVoxelBiome::Craters);
        WaterColNeutralHeights[ColIdx] = Item.NeutralH;
        WaterColSurfaceHeights[ColIdx] = Item.SurfH;
    }
}
