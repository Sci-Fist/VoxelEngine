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
#include "VoxelNoiseSIMD.h"
#include "Generation/IVoxelGenerationStage.h"
#include "Voxel/Core/VoxelDataMap.h"
#include "Biomes/VoxelBiomeManager.h"
#include "Biomes/VoxelBiomeGenerators.h"
#include "Async/ParallelFor.h"
#include "Templates/Atomic.h"
#include "VoxelLogger.h"

static FCriticalSection& GetDensityPoolLock() { static FCriticalSection* L = new FCriticalSection(); return *L; }
static TArray<TArray<float>>& GetDensityPool() { static TArray<TArray<float>>* P = new TArray<TArray<float>>(); return *P; }

static FCriticalSection& GetScratchPoolLock() { static FCriticalSection* L = new FCriticalSection(); return *L; }
static TArray<FVoxelMeshScratchBuffers>& GetScratchPool() { static TArray<FVoxelMeshScratchBuffers>* P = new TArray<FVoxelMeshScratchBuffers>(); return *P; }

static FCriticalSection& GetColumnPoolLock() { static FCriticalSection* L = new FCriticalSection(); return *L; }
static TArray<FVoxelGeneratorTask::FColumnScratchData>& GetColumnPool() { static TArray<FVoxelGeneratorTask::FColumnScratchData>* P = new TArray<FVoxelGeneratorTask::FColumnScratchData>(); return *P; }

#define GDensityPoolLock GetDensityPoolLock()
#define GDensityPool GetDensityPool()
#define GScratchPoolLock GetScratchPoolLock()
#define GScratchPool GetScratchPool()
#define GColumnPoolLock GetColumnPoolLock()
#define GColumnPool GetColumnPool()

static const EVoxelBiome GBiomeOrder[] =
{
    EVoxelBiome::Forest, EVoxelBiome::Peaks, EVoxelBiome::Cliffs,
    EVoxelBiome::Mesa,   EVoxelBiome::Craters, EVoxelBiome::Desert, EVoxelBiome::Ocean,
};
static_assert(UE_ARRAY_COUNT(GBiomeOrder) == FVoxelBiomeWeightMap::MaxBiomes,
    "GBiomeOrder must contain exactly one entry per EVoxelBiome value.");

// ── Density Passes ──────────────────────────────────────────────────────────
namespace {
    struct FDensityPassContext
    {
        float WX, WY;
        float ExactMinZ;
        float EffVoxSz;
        int32 EffSize;
        float* SF_Densities;
        const FColumnContext& Ctx;
        const FVoxelGenerationConfig& Config;
    };

    static void Pass_Surface(const FDensityPassContext& C, float SteepW, int32& OutStartZIdx, int32& OutEndZIdx)
    {
        const float ExactMinZ = C.ExactMinZ;
        const float EffVoxSz  = C.EffVoxSz;
        const float SurfH     = C.Ctx.SurfaceHeight;
        const float WX = C.WX, WY = C.WY;

        const float CraterRadius = C.Config.Craters.CentralCraterRadius * 1.8f;
        const float dx = WX - C.Config.Craters.ForcedCraterCenter.X;
        const float dy = WY - C.Config.Craters.ForcedCraterCenter.Y;
        const float DistSq = dx * dx + dy * dy;

        float MinCarveZ = SurfH - 1200.f;
        float MaxCarveZ = SurfH + 1200.f;

        if (SteepW > 0.05f || C.Ctx.StepSize > 1) { MinCarveZ -= 30000.f; MaxCarveZ += 30000.f; }
        if (C.Ctx.StepSize > 1) { MinCarveZ = C.ExactMinZ; MaxCarveZ = C.ExactMinZ + C.EffSize * C.EffVoxSz; }
        if (C.Config.Performance.bEnableOverhangs) { MaxCarveZ += C.Config.Overhangs.MaxDistFromSurface + 500.f; }
        
        if (DistSq < CraterRadius * CraterRadius)
        {
            const float SafeBaseH = FMath::Max(SurfH, C.Config.SeaLevel);
            const float MaxRimOverhead = C.Config.Craters.CentralCraterRimHeight * 2.5f + 4500.f;
            MaxCarveZ = FMath::Max(MaxCarveZ, SafeBaseH + MaxRimOverhead);
            
            // Sealing bottom drops for deep crater floor contours
            MinCarveZ = FMath::Min(MinCarveZ, SurfH - FMath::Abs(C.Config.Craters.CentralCraterDepth) - 20000.f);
        }

        OutStartZIdx = FMath::Clamp(FMath::FloorToInt((MinCarveZ - ExactMinZ) / EffVoxSz), 0, C.EffSize);
        OutEndZIdx   = FMath::Clamp(FMath::CeilToInt((MaxCarveZ - ExactMinZ) / EffVoxSz), 0, C.EffSize);

        for (int32 Z = 0; Z < OutStartZIdx; ++Z) C.SF_Densities[Z] = 2.f;
        for (int32 Z = OutEndZIdx; Z < C.EffSize; ++Z) C.SF_Densities[Z] = -2.f;

        const int32 Count = OutEndZIdx - OutStartZIdx;
        if (Count > 0)
        {
            if (C.Ctx.StepSize >= 4)
            {
                // Directive C: Geometric Downsampling for distant chunks (LOD 2+)
                FVoxelNoiseSIMD::EvaluateColumn_Surface_Upsampled_AVX2(
                    WX, WY, 
                    ExactMinZ + OutStartZIdx * EffVoxSz, EffVoxSz, 
                    Count, &C.SF_Densities[OutStartZIdx], 
                    C.Ctx.SurfaceHeight, C.Config.SurfaceGradientScale, SteepW,
                    C.Config.SeaLevel, C.Ctx.CachedSeedOffset,
                    FVoxelNoiseSIMD::GetPermutationTable(),
                    C.Config.Overhangs.MaxDistFromSurface, C.Config.Overhangs.Amplitude, C.Config.Overhangs.NoiseFrequency
                );
            }
            else
            {
                FVoxelNoiseSIMD::EvaluateColumn_Surface_AVX2(
                    WX, WY, 
                    ExactMinZ + OutStartZIdx * EffVoxSz, EffVoxSz, 
                    Count, &C.SF_Densities[OutStartZIdx], 
                    C.Ctx.SurfaceHeight, C.Config.SurfaceGradientScale, SteepW,
                    C.Config.SeaLevel, C.Ctx.CachedSeedOffset,
                    FVoxelNoiseSIMD::GetPermutationTable(),
                    C.Config.Overhangs.MaxDistFromSurface, C.Config.Overhangs.Amplitude, C.Config.Overhangs.NoiseFrequency
                );
            }
        }
    }

    static void Pass_Caves(const FDensityPassContext& C, int32 EndZIdx)
    {
        const FCaveTunnelsConfig& CVC = C.Config.CaveTunnels;
        const float CraterW = C.Ctx.BiomeWeights.GetWeight(EVoxelBiome::Craters);
        float DynamicMinDepth = CVC.MinDepthBelowSurface;

        if (CraterW > 0.05f) {
            const float CraterDrop = FMath::Max(0.f, C.Ctx.NeutralSurfaceHeight - C.Ctx.SurfaceHeight);
            DynamicMinDepth += CraterDrop + (CraterW * 4500.f);
        }

        const int32 CaveCount = EndZIdx; 
        if (CaveCount > 0)
        {
            FVoxelNoiseSIMD::EvaluateColumn_Caves_AVX2(
                C.WX, C.WY, C.ExactMinZ, C.EffVoxSz, 
                CaveCount, C.SF_Densities, 
                C.Ctx.SurfaceHeight, C.Ctx.BedrockJag,
                C.Ctx.CachedSeedOffset,
                FVoxelNoiseSIMD::GetPermutationTable(),
                CVC.Scale, CVC.Threshold, CVC.WobbleAmplitude, CVC.WobbleFrequency, CVC.Strength,
                DynamicMinDepth, CVC.SurfaceFadeDepth, CVC.BedrockDepth
            );
        }
    }

    static void Pass_CrystalCaverns(const FDensityPassContext& C)
    {
        const FCrystalCavernsConfig& CCC = C.Config.CaveCrystals;
        FVoxelNoiseSIMD::EvaluateColumn_CrystalCaverns_AVX2(
            C.WX, C.WY, C.ExactMinZ, C.EffVoxSz, 
            C.EffSize, C.SF_Densities, 
            C.Ctx.SurfaceHeight, C.Ctx.CachedSeedOffset,
            FVoxelNoiseSIMD::GetPermutationTable(),
            CCC.DepthStart, CCC.FadeDepth, CCC.ChamberFrequency, CCC.ChamberThreshold, CCC.ChamberStrength,
            CCC.bEnableConnectingVeins, CCC.VeinPower, CCC.VeinStrength,
            CCC.CrystalDetailFrequency, CCC.CrystalThreshold, CCC.CrystalAmplitude,
            C.Config.Performance.MaxNoiseOctaves
        );
    }

    static void Pass_Skylands(const FDensityPassContext& C)
    {
        FVoxelNoiseSIMD::EvaluateColumn_Skylands_AVX2(
            C.WX, C.WY, C.ExactMinZ, C.EffVoxSz, 
            C.EffSize, C.SF_Densities, 
            C.Ctx.SkylandCache, C.Ctx.CachedSeedOffset,
            FVoxelNoiseSIMD::GetPermutationTable(),
            C.Config
        );
    }
}

// ============================================================
//  Constructor
// ============================================================
FVoxelGeneratorTask::FVoxelGeneratorTask(
    const FIntVector& InChunkCoord, const FVector& InWorldOrigin,
    const FVector& InCameraPos,
    int32 InChunkSize, float InVoxelSize, int32 InStepSize,
    const FVoxelGenerationConfig& InConfig, IVoxelDensityProvider* InProvider,
    float InFoliageDensity, float InMaxFoliageSlope, struct FVoxelDataMap* InDataMap,
    uint32 InFrameNumber,
    bool InIsDistantHeightmesh)
    : ChunkCoord(InChunkCoord), WorldOrigin(InWorldOrigin), CameraPos(InCameraPos), ChunkSize(InChunkSize)
    , VoxelSize(InVoxelSize), StepSize(InStepSize), Config(InConfig)
    , DensityProvider(InProvider), FoliageDensity(InFoliageDensity)
    , MaxFoliageSlope(InMaxFoliageSlope), DataMap(InDataMap)
    , FrameNumber(InFrameNumber)
    , bIsDistantHeightmesh(InIsDistantHeightmesh)
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
        if (GDensityPool.Num() < 16)
            GDensityPool.Add(MoveTemp(Densities));
    }

    if (ScratchBuffers.VertexIndices.Num() > 0)
    {
        FScopeLock Lock(&GScratchPoolLock);
        if (GScratchPool.Num() < 16)
            GScratchPool.Add(MoveTemp(ScratchBuffers));
    }

    if (ColScratch.PrecalcColumns.Num() > 0)
    {
        FScopeLock Lock(&GColumnPoolLock);
        if (GColumnPool.Num() < 16)
            GColumnPool.Add(MoveTemp(ColScratch));
    }
}

// ============================================================
//  Execute
// ============================================================
void FVoxelGeneratorTask::Execute()
{
    if (bCancelled) return;
    
    double T0 = FPlatformTime::Seconds();
    BuildDensityField();
    double T1 = FPlatformTime::Seconds();

    if (bCancelled || bIsFullSolid || bIsFullAir)
    {
        if (bIsFullAir || bIsFullSolid)
        {
            float TDense = (T1 - T0) * 1000.f;
            if (TDense > 20.0f) // Only log noticeable ones to prevent spam
            {
                UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("Task [%d,%d,%d] Done (Empty/Solid). Density=%.2fms"),
                    ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, TDense));
            }
        }
        return;
    }

    BuildMesh();
    double T2 = FPlatformTime::Seconds();

    ComputeWaterColumns(); 
    if (bCancelled) return;

    if (StepSize > 1) return; 

    CalculateFoliage();
    double T3 = FPlatformTime::Seconds();
    
    PlaceWaterSources();
    double T4 = FPlatformTime::Seconds();

    float TDense  = (T1 - T0) * 1000.f;
    float TMesh   = (T2 - T1) * 1000.f;
    float TFoli   = (T3 - T2) * 1000.f;
    float TWater  = (T4 - T3) * 1000.f;
    float TTotal  = (T4 - T0) * 1000.f;

    // if (TTotal > 100.0f) // Only log slow chunks to find hotspots
    // {
    //     UE_LOG(LogTemp, Log, TEXT("Chunk [%d,%d,%d] LOD%d: Total=%.1fms (Dense=%.1fms, Mesh=%.1fms, Foliage=%.1fms, Water=%.1fms)"),
    //         ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, StepSize - 1, TTotal, TDense, TMesh, TFoli, TWater);
    // }
}

// ============================================================
//  BuildDensityField
// ============================================================
void FVoxelGeneratorTask::BuildDensityField()
{
    const double StartTime = FPlatformTime::Seconds();
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

    {
        FScopeLock Lock(&GColumnPoolLock);
        if (GColumnPool.Num() > 0)
        {
            ColScratch = MoveTemp(GColumnPool.Last());
            GColumnPool.RemoveAt(GColumnPool.Num() - 1, 1, EAllowShrinking::No);
        }
    }

    // Speedup Tier 3: Disable sub-voxel overhangs and caves on distant high-LOD chunks (silhouette only)
    FVoxelGenerationConfig LocalConfig = Config;
    if (StepSize > 1)
    {
        LocalConfig.Performance.bEnableCaves = false;
        LocalConfig.Performance.bEnableOverhangs = false;
        // Removed to prevent height-shifting artifacts on distant chunks (LOD mesh holes)
        // LocalConfig.Performance.MaxNoiseOctaves = (StepSize >= 4) ? 1 : 2;
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
    ColScratch.PrecalcColumns.SetNum(EffSize * EffSize);
    ColScratch.ColumnWeights .SetNum(EffSize * EffSize);
    ColScratch.ColumnSurfaceH.SetNum(EffSize * EffSize);

    const int32 NumCols = EffSize * EffSize;
    const int32* PermTable = FVoxelNoiseSIMD::GetPermutationTable();
    
    const float CenterH = FVoxelBiomeManager::GetNeutralSurfaceHeightStatic(
        LocalConfig.Craters.ForcedCraterCenter.X, LocalConfig.Craters.ForcedCraterCenter.Y, LocalConfig);
        
    TMap<FIntPoint, TArray<FSkylandIslandData>> SharedSkylandCache;
    if (bEnSkylands)
    {
        const float GridSize = LocalConfig.SkylandsLayer.BaseIslandSize * 3.0f;
        if (GridSize > 0.f)
        {
            const float SafeMinX = WorldOrigin.X - EffVoxSz;
            const float SafeMaxX = WorldOrigin.X + (EffSize + 1) * EffVoxSz;
            const float SafeMinY = WorldOrigin.Y - EffVoxSz;
            const float SafeMaxY = WorldOrigin.Y + (EffSize + 1) * EffVoxSz;

            const int32 MinCellX = FMath::FloorToInt(SafeMinX / GridSize) - 2;
            const int32 MaxCellX = FMath::FloorToInt(SafeMaxX / GridSize) + 2;
            const int32 MinCellY = FMath::FloorToInt(SafeMinY / GridSize) - 2;
            const int32 MaxCellY = FMath::FloorToInt(SafeMaxY / GridSize) + 2;

            FVoxelBiomeWeightMap DummyW;
            for (int32 cy = MinCellY; cy <= MaxCellY; ++cy)
            {
                for (int32 cx = MinCellX; cx <= MaxCellX; ++cx)
                {
                    FVoxelBiomeGenerators::GetSkylandColumnCache(
                        cx * GridSize, cy * GridSize, CenterH, DummyW, LocalConfig, &SharedSkylandCache);
                }
            }
        }
    }
    
    ParallelFor(NumCols / 8, [&](int32 idx)
    {
        const int32 ColIdx = idx * 8;
        // ── 8-WIDE SIMD PRE-CALC ───────────────────────────────────────────────
        float CX[8], CY[8];
        for (int32 k = 0; k < 8; ++k)
        {
            const int32 curIdx = ColIdx + k;
            const int32 Y = curIdx / EffSize;
            const int32 X = curIdx % EffSize;
            CX[k] = WorldOrigin.X + (X - 1.f) * EffVoxSz;
            CY[k] = WorldOrigin.Y + (Y - 1.f) * EffVoxSz;
        }

        __m256 CX_v = _mm256_loadu_ps(CX);
        __m256 CY_v = _mm256_loadu_ps(CY);

        FVoxelNoiseSIMD::FBiomeWeights_AVX2 Weights_v;
        __m256 Temp_v, Eros_v;
        // FIX: Use accurate Config (not LocalConfig) for height estimation to prevent distant LOD mesh holes
        FVoxelNoiseSIMD::EvaluateColumn_BiomeWeights_AVX2(CX_v, CY_v, Config, PermTable, Weights_v, Temp_v, Eros_v);

        // SIMD NEUTRAL: request both crater-modified AND neutral heights in a single pass.
        __m256 SurfH_v, NeutralH_v;
        FVoxelNoiseSIMD::EvaluateColumn_SurfaceHeight_AVX2(CX_v, CY_v, Weights_v, Config, PermTable, Temp_v, Eros_v, SurfH_v, CenterH, &NeutralH_v);

        float Forest[8], Desert[8], Peaks[8], Cliffs[8], Mesa[8], Craters[8], Ocean[8], SurfH[8], NeutralH[8], Temp[8], Eros[8];
        _mm256_storeu_ps(Forest,   Weights_v.Forest);
        _mm256_storeu_ps(Desert,   Weights_v.Desert);
        _mm256_storeu_ps(Peaks,    Weights_v.Peaks);
        _mm256_storeu_ps(Cliffs,   Weights_v.Cliffs);
        _mm256_storeu_ps(Mesa,     Weights_v.Mesa);
        _mm256_storeu_ps(Craters,  Weights_v.Craters);
        _mm256_storeu_ps(Ocean,    Weights_v.Ocean);
        _mm256_storeu_ps(SurfH,    SurfH_v);
        _mm256_storeu_ps(NeutralH, NeutralH_v); // crater-free: used for caves & skylands
        _mm256_storeu_ps(Temp,     Temp_v);
        _mm256_storeu_ps(Eros,     Eros_v);

        for (int32 k = 0; k < 8; ++k)
        {
            const int32 curIdx = ColIdx + k;
            FColumnCacheItem& Item = ColScratch.PrecalcColumns[curIdx];

            Item.Weights.SetWeight(EVoxelBiome::Forest,  Forest[k]);
            Item.Weights.SetWeight(EVoxelBiome::Desert,  Desert[k]);
            Item.Weights.SetWeight(EVoxelBiome::Peaks,   Peaks[k]);
            Item.Weights.SetWeight(EVoxelBiome::Cliffs,  Cliffs[k]);
            Item.Weights.SetWeight(EVoxelBiome::Mesa,    Mesa[k]);
            Item.Weights.SetWeight(EVoxelBiome::Craters, Craters[k]);
            Item.Weights.Normalize();

            if (!LocalConfig.Performance.bEnableForest)  Item.Weights.SetWeight(EVoxelBiome::Forest,  0.f);
            if (!LocalConfig.Performance.bEnableDesert)  Item.Weights.SetWeight(EVoxelBiome::Desert,  0.f);
            if (!LocalConfig.Performance.bEnablePeaks)   Item.Weights.SetWeight(EVoxelBiome::Peaks,   0.f);
            if (!LocalConfig.Performance.bEnableCliffs)  Item.Weights.SetWeight(EVoxelBiome::Cliffs,  0.f);
            if (!LocalConfig.Performance.bEnableMesa)    Item.Weights.SetWeight(EVoxelBiome::Mesa,    0.f);
            if (!LocalConfig.Performance.bEnableCraters) Item.Weights.SetWeight(EVoxelBiome::Craters, 0.f);
            Item.Weights.Normalize();

            Item.SurfH = SurfH[k];
            // SIMD NEUTRAL: NeutralH[k] is already computed above (biome blend, no crater).
            // No scalar GetNeutralSurfaceHeightStatic call needed here.
            Item.NeutralH = NeutralH[k];
            
            // Tier 4 fallback: If any unsupported biome is active, re-calculate scalar.
            if (Item.SurfH == 0.f)
            {
                Item.SurfH    = FVoxelBiomeManager::GetSurfaceHeightStatic(CX[k], CY[k], Item.Weights, Config, Temp[k], Eros[k]);
                Item.NeutralH = FVoxelBiomeManager::GetNeutralSurfaceHeightStatic(CX[k], CY[k], Config, Temp[k], Eros[k]);
            }
            Item.SkylandCache = FVoxelBiomeGenerators::GetSkylandColumnCache(CX[k], CY[k], Item.NeutralH, Item.Weights, Config, &SharedSkylandCache);
            

            ColScratch.ColumnWeights[curIdx]  = Item.Weights;
            ColScratch.ColumnSurfaceH[curIdx] = Item.SurfH;
        }
    });

    int32 ColIdx = (NumCols / 8) * 8;
    // ── REMAINDER FALLBACK ──────────────────────────────────────────────────
    for (; ColIdx < NumCols; ++ColIdx)
    {
        const int32 Y  = ColIdx / EffSize;
        const int32 X  = ColIdx % EffSize;
        const float CX = WorldOrigin.X + (X - 1.f) * EffVoxSz;
        const float CY = WorldOrigin.Y + (Y - 1.f) * EffVoxSz;

        FColumnCacheItem& Item = ColScratch.PrecalcColumns[ColIdx];

        // FIX: Use accurate Config for fallback height estimation
        float Temp = -999.f, Erosion = -999.f;
        Item.Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(CX, CY, Config, &Temp, &Erosion);
        if (!LocalConfig.Performance.bEnableForest)  Item.Weights.SetWeight(EVoxelBiome::Forest,  0.f);
        if (!LocalConfig.Performance.bEnableDesert)  Item.Weights.SetWeight(EVoxelBiome::Desert,  0.f);
        if (!LocalConfig.Performance.bEnablePeaks)   Item.Weights.SetWeight(EVoxelBiome::Peaks,   0.f);
        if (!LocalConfig.Performance.bEnableCliffs)  Item.Weights.SetWeight(EVoxelBiome::Cliffs,  0.f);
        if (!LocalConfig.Performance.bEnableMesa)    Item.Weights.SetWeight(EVoxelBiome::Mesa,    0.f);
        if (!LocalConfig.Performance.bEnableCraters) Item.Weights.SetWeight(EVoxelBiome::Craters, 0.f);
        Item.Weights.Normalize();

        Item.SurfH    = FVoxelBiomeManager::GetSurfaceHeightStatic(CX, CY, Item.Weights, Config, Temp, Erosion);
        Item.NeutralH = FVoxelBiomeManager::GetNeutralSurfaceHeightStatic(CX, CY, Config, Temp, Erosion);
        Item.SkylandCache = FVoxelBiomeGenerators::GetSkylandColumnCache(CX, CY, Item.NeutralH, Item.Weights, Config, &SharedSkylandCache);

        ColScratch.ColumnWeights[ColIdx]  = Item.Weights;
        ColScratch.ColumnSurfaceH[ColIdx] = Item.SurfH;
    }

    if (bIsDistantHeightmesh)
    {
        PostProcessDensities(TotalSamples);
        return;
    }

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
    TAtomic<int32> SolidCount{0};
    TAtomic<int32> AirCount{0};

    ParallelFor(EffSize * EffSize, [&](int32 FlatXY)
    {
        if (bCancelled) return;
        int32 LocalSolid = 0;
        int32 LocalAir   = 0;

        const int32 Y  = FlatXY / EffSize;
        const int32 X  = FlatXY % EffSize;
        const float WX = WorldOrigin.X + (X-1.f)*EffVoxSz;
        const float WY = WorldOrigin.Y + (Y-1.f)*EffVoxSz;

        const FColumnCacheItem& Item = ColScratch.PrecalcColumns[FlatXY];
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
            const float CraterW = Weights.GetWeight(EVoxelBiome::Craters);
            const float SkyLB   = NeutralH + SC.MinAltitudeAboveTerrain
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
            if (Ctx.StepSize == 1 && MinWZ > SurfH + OvH + 200.f && MaxWZ < SkyLB2)
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
                SolidCount += LocalSolid;
                AirCount   += LocalAir;
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
            SolidCount += LocalSolid;
            AirCount   += LocalAir;
            return;
        }

        // ── SIMD Pass Chaining (@/improve componentisation) ────────────────────
        // Periodic cancellation check inside ParallelFor
        if (FlatXY % 8 == 0 && bCancelled) return;

        float SF_Densities[256];
        const float SteepW = Ctx.BiomeWeights.Cliffs + Ctx.BiomeWeights.Peaks;
        const float ExactMinZ = WorldOrigin.Z - EffVoxSz;

        FDensityPassContext PassCtx { WX, WY, ExactMinZ, EffVoxSz, EffSize, SF_Densities, Ctx, LocalConfig };

        int32 StartZIdx = 0;
        int32 EndZIdx   = 0;

        Pass_Surface(PassCtx, SteepW, StartZIdx, EndZIdx);

        // FIX CAVES-NEVER-RUN: EffVoxSz = VoxelSize * StepSize = 100 * 1 = 100.
        // The old condition (EffVoxSz <= 1) was ALWAYS false — caves silently
        // skipped for every chunk at every LOD. Correct gate is StepSize == 1
        // (full-resolution only; LOD chunks intentionally skip caves).
        if (bEnCaves && StepSize == 1)
        {
            Pass_Caves(PassCtx, EndZIdx);
        }

        if (bEnCaves)                             Pass_CrystalCaverns(PassCtx);
        if (bEnSkylands && Ctx.SkylandCache.bHasSkyland) Pass_Skylands(PassCtx);

        // Cascade upsampling cache is now handled directly by SIMD setup layout securely !!
        for (int32 CurrZ = 0; CurrZ < EffSize; ++CurrZ)
        {
            const int32 CurrIdx = X + Y*EffSize + CurrZ*EffSize*EffSize;
            float D = SF_Densities[CurrZ];

            if (!DenseHasEdit.IsEmpty() && DenseHasEdit[CurrIdx])
            {
                const float Ov = DenseEditVals[CurrIdx];
                D = (Ov < 0.f) ? FMath::Min(D, Ov) : FMath::Max(D, Ov);
            }
            
            Densities[CurrIdx] = D;
            if (D > 0.f) LocalSolid++; else LocalAir++;
        }
        SolidCount += LocalSolid;
        AirCount += LocalAir;
    });

    bIsFullSolid = (SolidCount.Load() == TotalSamples);
    bIsFullAir   = (AirCount.Load() == TotalSamples);
    PostProcessDensities(TotalSamples);

    const double EndTime = FPlatformTime::Seconds();
    UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("BuildDensityField took %.2fms"), (EndTime - StartTime) * 1000.f));
}

void FVoxelGeneratorTask::PostProcessDensities(int32) {}

// ============================================================
//  BuildMesh
// ============================================================
void FVoxelGeneratorTask::BuildMesh()
{
    {
        FScopeLock Lock(&GScratchPoolLock);
        if (GScratchPool.Num() > 0)
        {
            ScratchBuffers = MoveTemp(GScratchPool.Last());
            GScratchPool.RemoveAt(GScratchPool.Num() - 1, 1, EAllowShrinking::No);
        }
    }

    MeshOutput.Reset();
    if (bIsDistantHeightmesh)
    {
        FVoxelMeshGenerator::GenerateHeightmapMesh(
            ColScratch.ColumnSurfaceH, ColScratch.ColumnWeights, ChunkSize, VoxelSize, WorldOrigin, MeshOutput, Config, StepSize);
    }
    else
    {
        FVoxelMeshGenerator::GenerateMesh(
            Densities, ChunkSize, VoxelSize, WorldOrigin, CameraPos, FrameNumber, MeshOutput, Config, StepSize,
            &ScratchBuffers,
            &ColScratch.ColumnWeights,
            [this]() { return !!bCancelled; }); // Directive 3: Pass preemption guard
    }
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
    if (ColScratch.PrecalcColumns.Num() == 0 || EffSize == 0) return; // safety: BuildDensityField must have run

    const int32 CS = ChunkSize;
    ColScratch.WaterColOceanWeights  .SetNumZeroed(CS * CS);
    ColScratch.WaterColCraterWeights .SetNumZeroed(CS * CS);
    ColScratch.WaterColNeutralHeights.SetNumZeroed(CS * CS);
    ColScratch.WaterColSurfaceHeights.SetNumZeroed(CS * CS);

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
        const FColumnCacheItem& Item = ColScratch.PrecalcColumns[EIdx];

        const int32 ColIdx = lx + ly * CS;
        ColScratch.WaterColOceanWeights  [ColIdx] = Item.Weights.GetWeight(EVoxelBiome::Ocean);
        ColScratch.WaterColCraterWeights [ColIdx] = Item.Weights.GetWeight(EVoxelBiome::Craters);
        ColScratch.WaterColNeutralHeights[ColIdx] = Item.NeutralH;
        ColScratch.WaterColSurfaceHeights[ColIdx] = Item.SurfH;
    }
}
