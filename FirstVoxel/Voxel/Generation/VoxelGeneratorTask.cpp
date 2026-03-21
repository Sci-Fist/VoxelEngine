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
    const int32 EffSize      = EffCS + 3;
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

    static FVoxelDensityGenerator FallbackGen;
    IVoxelDensityProvider* Provider = DensityProvider ? DensityProvider : &FallbackGen;

    const FVoxelSurfacePass SurfacePass;
    const FVoxelCavePass    CavePass;
    const FVoxelSkylandPass SkylandPass;

    const bool bEnForest   = Config.Performance.bEnableForest;
    const bool bEnDesert   = Config.Performance.bEnableDesert;
    const bool bEnPeaks    = Config.Performance.bEnablePeaks;
    const bool bEnCliffs   = Config.Performance.bEnableCliffs;
    const bool bEnMesa     = Config.Performance.bEnableMesa;
    const bool bEnCraters  = Config.Performance.bEnableCraters;
    const bool bEnSurface  = Config.Performance.bEnableSurface;
    const bool bEnCaves    = Config.Performance.bEnableCaves;
    const bool bEnSkylands = Config.Performance.bEnableSkylands;

    ColumnWeights .SetNumUninitialized(EffCS * EffCS);
    ColumnSurfaceH.SetNumUninitialized(EffCS * EffCS);
    SkylandColumnCaches.SetNum(ChunkSize * ChunkSize);

    // ── Per-column pre-compute ────────────────────────────────────────────
    // ROOT FIX: pass NeutralSH (pre-crater terrain) to GetSkylandColumnCache,
    // not SH (crater-modified). Islands must float above neutral terrain,
    // not above the crater floor (which may be 8000cm below the terrain).
    ParallelFor(ChunkSize * ChunkSize, [&](int32 Idx)
    {
        const int32 i  = Idx / ChunkSize;
        const int32 j  = Idx % ChunkSize;
        const float CX = WorldOrigin.X + i * VoxelSize;
        const float CY = WorldOrigin.Y + j * VoxelSize;

        FVoxelBiomeWeightMap W = Provider->GetBiomeWeights(CX, CY, Config);
        if (!bEnForest)  W.SetWeight(EVoxelBiome::Forest,  0.f);
        if (!bEnDesert)  W.SetWeight(EVoxelBiome::Desert,  0.f);
        if (!bEnPeaks)   W.SetWeight(EVoxelBiome::Peaks,   0.f);
        if (!bEnCliffs)  W.SetWeight(EVoxelBiome::Cliffs,  0.f);
        if (!bEnMesa)    W.SetWeight(EVoxelBiome::Mesa,    0.f);
        if (!bEnCraters) W.SetWeight(EVoxelBiome::Craters, 0.f);
        W.Normalize();

        // ROOT FIX: use neutral (pre-crater) height for skyland altitude reference
        const float NeutralSH = FVoxelBiomeManager::GetNeutralSurfaceHeightStatic(CX, CY, Config);
        SkylandColumnCaches[i * ChunkSize + j] =
            FVoxelBiomeGenerators::GetSkylandColumnCache(CX, CY, NeutralSH, W, Config);
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
    TAtomic<int32> SolidCount{0};
    TAtomic<int32> AirCount  {0};

    ParallelFor(EffSize * EffSize, [&](int32 FlatXY)
    {
        if (bCancelled) return;
        const int32 Y  = FlatXY / EffSize;
        const int32 X  = FlatXY % EffSize;
        const float WX = FMath::RoundToFloat(WorldOrigin.X + (X-1.f)*EffVoxSz);
        const float WY = FMath::RoundToFloat(WorldOrigin.Y + (Y-1.f)*EffVoxSz);

        FVoxelBiomeWeightMap Weights = Provider->GetBiomeWeights(WX, WY, Config);
        if (!bEnForest)  Weights.SetWeight(EVoxelBiome::Forest,  0.f);
        if (!bEnDesert)  Weights.SetWeight(EVoxelBiome::Desert,  0.f);
        if (!bEnPeaks)   Weights.SetWeight(EVoxelBiome::Peaks,   0.f);
        if (!bEnCliffs)  Weights.SetWeight(EVoxelBiome::Cliffs,  0.f);
        if (!bEnMesa)    Weights.SetWeight(EVoxelBiome::Mesa,    0.f);
        if (!bEnCraters) Weights.SetWeight(EVoxelBiome::Craters, 0.f);
        Weights.Normalize();

        const float SurfH    = FVoxelBiomeManager::GetSurfaceHeightStatic(WX, WY, Weights, Config);
        // ROOT FIX: get the pre-crater neutral height for skyland altitude reference
        const float NeutralH = FVoxelBiomeManager::GetNeutralSurfaceHeightStatic(WX, WY, Config);

        const int32 LX = X-1, LY2 = Y-1;
        if (LX>=0&&LX<EffCS&&LY2>=0&&LY2<EffCS)
        { ColumnWeights[LX+LY2*EffCS]=Weights; ColumnSurfaceH[LX+LY2*EffCS]=SurfH; }

        const float MaxWZ = WorldOrigin.Z + (EffSize+1)*EffVoxSz;
        const float MinWZ = WorldOrigin.Z - EffVoxSz;

        FColumnContext Ctx;
        Ctx.SurfaceHeight        = SurfH;
        Ctx.NeutralSurfaceHeight = NeutralH;  // passed through to CavePass and SkylandPass
        Ctx.BiomeWeights         = Weights;
        Ctx.MaxWorldZ            = MaxWZ;

        if (bEnSurface) SurfacePass.PrepareColumn(WX, WY, Config, Ctx);
        // CavePass.PrepareColumn sets NeutralSurfaceHeight via GetNeutralSurfaceHeightStatic
        // It will overwrite our value — that's fine, both calls produce the same result.
        if (bEnCaves)   CavePass   .PrepareColumn(WX, WY, Config, Ctx);

        if (bEnSkylands)
        {
            const FSkylandsLayerConfig& SC = Config.SkylandsLayer;

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
                const int32 AX = (X-1)*StepSize, AY = (Y-1)*StepSize;
                // Pre-built cache (built with NeutralSH above) — use directly.
                // Fallback (border columns): build now with NeutralH.
                Ctx.SkylandCache = (AX>=0&&AX<ChunkSize&&AY>=0&&AY<ChunkSize)
                    ? SkylandColumnCaches[AX*ChunkSize+AY]
                    : FVoxelBiomeGenerators::GetSkylandColumnCache(WX, WY, NeutralH, Weights, Config);
            }

            // Early-out for pure-air columns well below the skyland band
            const float OvH     = Config.Performance.bEnableOverhangs ? Config.Overhangs.MaxDistFromSurface : 0.f;
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
                    if (D > 0.f) SolidCount.IncrementExchange(); else AirCount.IncrementExchange();
                }
                return;
            }
        }

        // Below-bedrock early-out
        if (MaxWZ < Config.CaveTunnels.BedrockDepth)
        {
            for (int32 Z=0; Z<EffSize; ++Z)
            {
                const int32 Idx = X+Y*EffSize+Z*EffSize*EffSize;
                float D = 2.f;
                if (!DenseHasEdit.IsEmpty() && DenseHasEdit[Idx])
                { const float Ov = DenseEditVals[Idx]; D = (Ov<0.f) ? FMath::Min(D,Ov) : FMath::Max(D,Ov); }
                Densities[Idx] = D;
                if (D > 0.f) SolidCount.IncrementExchange(); else AirCount.IncrementExchange();
            }
            return;
        }

        // Per-voxel evaluation
        for (int32 Z=0; Z<EffSize; ++Z)
        {
            const float WZ  = WorldOrigin.Z + (Z-1.f)*EffVoxSz;
            const int32 Idx = X+Y*EffSize+Z*EffSize*EffSize;
            float D = -2.f;
            if (bEnSurface)  D = SurfacePass.EvaluateVoxel(FVector(WX,WY,WZ), Ctx, Config, D);
            if (bEnCaves)    D = CavePass   .EvaluateVoxel(FVector(WX,WY,WZ), Ctx, Config, D);
            if (bEnSkylands) D = SkylandPass.EvaluateVoxel(FVector(WX,WY,WZ), Ctx, Config, D);
            if (!DenseHasEdit.IsEmpty() && DenseHasEdit[Idx])
            { const float Ov = DenseEditVals[Idx]; D = (Ov<0.f) ? FMath::Min(D,Ov) : FMath::Max(D,Ov); }
            Densities[Idx] = D;
            if (D > 0.f) SolidCount.IncrementExchange(); else AirCount.IncrementExchange();
        }
    });

    bIsFullSolid = ((int32)SolidCount == TotalSamples);
    bIsFullAir   = ((int32)AirCount   == TotalSamples);
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
        Densities, ChunkSize, VoxelSize, WorldOrigin, MeshOutput, Config, StepSize);
    UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelMesh: [%d,%d,%d] Flat=%d Slope=%d"),
        ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z,
        MeshOutput.FlatMesh.Vertices.Num(), MeshOutput.SlopeMesh.Vertices.Num()));
}

void FVoxelGeneratorTask::CountDensityStates(int32) {}
