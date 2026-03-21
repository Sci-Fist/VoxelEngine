// VoxelGeneratorTask.cpp
// Core pipeline only: Constructor / Destructor / Execute / BuildDensityField / BuildMesh
// Foliage: VoxelGeneratorTask_Foliage.cpp
// Water:   VoxelGeneratorTask_Water.cpp
//
// FIX #2  GDensityPool capped at 12 entries
// FIX #5  SkylandColumnCaches flattened to 1D indexed [i*ChunkSize+j]
// FIX #6  DenseHasEdit/DenseEditVals allocated only when DataMap != null
// FIX #9  Dead air-column commented block removed
// FIX #10 Solid/air counting folded into parallel loop via TAtomic

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
        if (GDensityPool.Num() < 12) // FIX #2
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
    CalculateFoliage();   // VoxelGeneratorTask_Foliage.cpp
    if (bCancelled) return;
    PlaceWaterSources();  // VoxelGeneratorTask_Water.cpp
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

    // FIX #5: 1D flat cache — zero heap reallocations per chunk
    SkylandColumnCaches.SetNum(ChunkSize * ChunkSize);

    // ── Pre-compute per-column biome weights and skyland caches ──────────
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

        const float SH = FVoxelBiomeManager::GetSurfaceHeightStatic(CX, CY, W, Config);
        SkylandColumnCaches[i * ChunkSize + j] =
            FVoxelBiomeGenerators::GetSkylandColumnCache(CX, CY, SH, W, Config);
    });

    // FIX #6: DataMap edit arrays only when edits exist
    TArray<bool>  DenseHasEdit;
    TArray<float> DenseEditVals;
    if (DataMap)
    {
        DenseHasEdit .Init(false, TotalSamples);
        DenseEditVals.Init(0.f,   TotalSamples);
        for (int32 cz=-1;cz<=1;++cz) for (int32 cy=-1;cy<=1;++cy) for (int32 cx=-1;cx<=1;++cx)
        {
            const FIntVector TCC = ChunkCoord + FIntVector(cx,cy,cz);
            TMap<int32,float> MV;
            if (!DataMap->GetChunkData(TCC, MV)) continue;
            for (const auto& Pair : MV)
            {
                const int32 LIdx=Pair.Key;
                const int32 lz=LIdx/(ChunkSize*ChunkSize), ly=(LIdx/ChunkSize)%ChunkSize, lx=LIdx%ChunkSize;
                const float BaseX = WorldOrigin.X + (TCC.X - ChunkCoord.X) * ChunkSize * VoxelSize;
                const float BaseY = WorldOrigin.Y + (TCC.Y - ChunkCoord.Y) * ChunkSize * VoxelSize;
                const float BaseZ = WorldOrigin.Z + (TCC.Z - ChunkCoord.Z) * ChunkSize * VoxelSize;
                const float AX = BaseX + lx * VoxelSize;
                const float AY = BaseY + ly * VoxelSize;
                const float AZ = BaseZ + lz * VoxelSize;
                const int32 GX=FMath::RoundToInt((AX-WorldOrigin.X)/EffVoxSz+1.f);
                const int32 GY=FMath::RoundToInt((AY-WorldOrigin.Y)/EffVoxSz+1.f);
                const int32 GZ=FMath::RoundToInt((AZ-WorldOrigin.Z)/EffVoxSz+1.f);
                if (GX<0||GX>=EffSize||GY<0||GY>=EffSize||GZ<0||GZ>=EffSize) continue;
                const int32 FI=GX+GY*EffSize+GZ*EffSize*EffSize;
                DenseHasEdit[FI]=true; DenseEditVals[FI]=Pair.Value;
            }
        }
    }

    // ── Main density loop ────────────────────────────────────────────────
    TAtomic<int32> SolidCount{0}; // FIX #10
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

        const float SurfH = FVoxelBiomeManager::GetSurfaceHeightStatic(WX, WY, Weights, Config);

        const int32 LX=X-1, LY2=Y-1;
        if (LX>=0 && LX<EffCS && LY2>=0 && LY2<EffCS)
        { ColumnWeights[LX+LY2*EffCS]=Weights; ColumnSurfaceH[LX+LY2*EffCS]=SurfH; }

        const float MaxWZ = WorldOrigin.Z + (EffSize+1)*EffVoxSz;
        const float MinWZ = WorldOrigin.Z - EffVoxSz;

        FColumnContext Ctx;
        Ctx.SurfaceHeight = SurfH;
        Ctx.BiomeWeights  = Weights;
        Ctx.MaxWorldZ     = MaxWZ;

        if (bEnSurface) SurfacePass.PrepareColumn(WX, WY, Config, Ctx);
        if (bEnCaves)   CavePass   .PrepareColumn(WX, WY, Config, Ctx);

        if (bEnSkylands)
        {
            const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
            const float SkyLB = SurfH+SC.MinAltitudeAboveTerrain-SC.BaseIslandSize*SC.ThicknessRatio-1000.f;
            if (MaxWZ < SkyLB)
                Ctx.SkylandCache.bHasSkyland = false;
            else
            {
                const int32 AX=(X-1)*StepSize, AY=(Y-1)*StepSize;
                Ctx.SkylandCache = (AX>=0&&AX<ChunkSize&&AY>=0&&AY<ChunkSize)
                    ? SkylandColumnCaches[AX*ChunkSize+AY]   // FIX #5
                    : FVoxelBiomeGenerators::GetSkylandColumnCache(WX,WY,SurfH,Weights,Config);
            }

            // Early-out: pure air column above surface, below sky band
            const float OvH = Config.Performance.bEnableOverhangs ? Config.Overhangs.MaxDistFromSurface : 0.f;
            const float SafeAirMinZ = SurfH + OvH + 200.f;
            const float SkyLB2 = SurfH+SC.MinAltitudeAboveTerrain-SC.BaseIslandSize*SC.ThicknessRatio-400.f;
            if (MinWZ > SafeAirMinZ && MaxWZ < SkyLB2)
            {
                for (int32 Z=0;Z<EffSize;++Z)
                {
                    const int32 Idx=X+Y*EffSize+Z*EffSize*EffSize;
                    float D=-2.f;
                    if (!DenseHasEdit.IsEmpty()&&DenseHasEdit[Idx])
                    { const float Ov=DenseEditVals[Idx]; D=(Ov<0.f)?FMath::Min(D,Ov):FMath::Max(D,Ov); }
                    Densities[Idx]=D; 
                    if (D > 0.f) SolidCount.IncrementExchange();
                    else         AirCount.IncrementExchange();
                }
                return;
            }
        }

        // Early-out: below bedrock
        if (MaxWZ < Config.CaveTunnels.BedrockDepth)
        {
            for (int32 Z=0;Z<EffSize;++Z)
            {
                const int32 Idx=X+Y*EffSize+Z*EffSize*EffSize;
                float D=2.f;
                if (!DenseHasEdit.IsEmpty()&&DenseHasEdit[Idx])
                { const float Ov=DenseEditVals[Idx]; D=(Ov<0.f)?FMath::Min(D,Ov):FMath::Max(D,Ov); }
                Densities[Idx]=D;
                if (D > 0.f) SolidCount.IncrementExchange();
                else         AirCount.IncrementExchange();
            }
            return;
        }

        // Main per-voxel evaluation
        for (int32 Z=0;Z<EffSize;++Z)
        {
            const float WZ  = WorldOrigin.Z + (Z-1.f)*EffVoxSz;
            const int32 Idx = X+Y*EffSize+Z*EffSize*EffSize;

            float D = -2.f;
            if (bEnSurface)  D = SurfacePass.EvaluateVoxel(FVector(WX,WY,WZ), Ctx, Config, D);
            if (bEnCaves)    D = CavePass   .EvaluateVoxel(FVector(WX,WY,WZ), Ctx, Config, D);
            if (bEnSkylands) D = SkylandPass.EvaluateVoxel(FVector(WX,WY,WZ), Ctx, Config, D);

            if (!DenseHasEdit.IsEmpty()&&DenseHasEdit[Idx]) // FIX #6
            { const float Ov=DenseEditVals[Idx]; D=(Ov<0.f)?FMath::Min(D,Ov):FMath::Max(D,Ov); }

            Densities[Idx] = D;
            if (D > 0.f) SolidCount.IncrementExchange(); // FIX #10
            else          AirCount  .IncrementExchange();
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
    UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelMesh: [%d,%d,%d] Verts=%d"),
        ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, MeshOutput.FlatMesh.Vertices.Num()));
}

// FIX #10: no-op stub — folded into BuildDensityField parallel loop
void FVoxelGeneratorTask::CountDensityStates(int32) {}
