// VoxelGeneratorTask.cpp
// Thread-safe task: Density → Mesh → Per-Biome Foliage → Water Sources
//
// FIX #2  — GDensityPool capped at 12 entries (was unbounded → OOM during bursts)
// FIX #4  — ProcessLegacyFoliage now called when !bHasPerBiomeFoliage so
//            legacy tree/grass actually spawns when no per-biome config exists
// FIX #5  — SkylandColumnCaches flattened from 2D TArray<TArray<>> to 1D
//            (ChunkSize*ChunkSize) — eliminates 33+ heap allocations per chunk
// FIX #6  — DenseHasEdit/DenseEditVals only allocated when DataMap != null
//            (saves 250KB zero-init per chunk for worlds with no player edits)
// FIX #9  — Dead commented-out air column block removed
// FIX #10 — CountDensityStates folded into the main parallel loop using
//            TAtomic counters — eliminates the redundant serial O(N) scan
// FIX #11 — TrimFoliageToCap now uses proportional trimming so all biome
//            slots lose instances equally instead of always trimming last slots

#include "Generation/VoxelGeneratorTask.h"
#include "Generation/VoxelMeshGenerator.h"
#include "Generation/VoxelDensityGenerator.h"
#include "Generation/IVoxelGenerationStage.h"
#include "Voxel/Core/VoxelDataMap.h"
#include "Biomes/VoxelBiomeManager.h"
#include "Biomes/VoxelBiomeGenerators.h"
#include "Async/ParallelFor.h"
#include "Containers/Atomic.h"
#include "VoxelLogger.h"

static FCriticalSection      GDensityPoolLock;
static TArray<TArray<float>> GDensityPool;

static const EVoxelBiome GBiomeOrder[] =
{
    EVoxelBiome::Forest,
    EVoxelBiome::Peaks,
    EVoxelBiome::Cliffs,
    EVoxelBiome::Mesa,
    EVoxelBiome::Craters,
    EVoxelBiome::Desert,
    EVoxelBiome::Ocean,
};
static_assert(UE_ARRAY_COUNT(GBiomeOrder) == FVoxelBiomeWeightMap::MaxBiomes,
    "GBiomeOrder must contain exactly one entry per EVoxelBiome value.");

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
    struct FVoxelDataMap*         InDataMap)
    : ChunkCoord     (InChunkCoord)
    , WorldOrigin    (InWorldOrigin)
    , ChunkSize      (InChunkSize)
    , VoxelSize      (InVoxelSize)
    , StepSize       (InStepSize)
    , Config         (InConfig)
    , DensityProvider(InProvider)
    , FoliageDensity (InFoliageDensity)
    , MaxFoliageSlope(InMaxFoliageSlope)
    , DataMap        (InDataMap)
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
        // FIX #2: cap pool at 12 recycled buffers (~2.4MB max at ChunkSize=16 LOD0)
        if (GDensityPool.Num() < 12)
            GDensityPool.Add(MoveTemp(Densities));
        // else: array frees normally when destructor exits scope
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
    const int32 EffCS      = ChunkSize / StepSize;
    const int32 EffSize    = EffCS + 3;        // padded grid side
    const int32 TotalSamples = EffSize * EffSize * EffSize;
    const float EffVoxSz   = VoxelSize * (float)StepSize;

    // Recycle a density buffer from the pool if available
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

    // Pre-evaluate toggle flags once to avoid repeated config member reads in hot loop
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

    // FIX #5: flat 1D skyland cache array — one allocation, indexed [i*ChunkSize+j]
    SkylandColumnCaches.SetNum(ChunkSize * ChunkSize);

    ParallelFor(ChunkSize * ChunkSize, [&](int32 Idx)
    {
        const int32 i = Idx / ChunkSize;
        const int32 j = Idx % ChunkSize;
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
        // FIX #5: flat access
        SkylandColumnCaches[i * ChunkSize + j] =
            FVoxelBiomeGenerators::GetSkylandColumnCache(CX, CY, SH, W, Config);
    });

    // FIX #6: only allocate edit arrays when player modifications exist
    TArray<bool>  DenseHasEdit;
    TArray<float> DenseEditVals;

    if (DataMap)
    {
        DenseHasEdit .Init(false, TotalSamples);
        DenseEditVals.Init(0.f,   TotalSamples);

        for (int32 cz = -1; cz <= 1; ++cz)
        for (int32 cy = -1; cy <= 1; ++cy)
        for (int32 cx = -1; cx <= 1; ++cx)
        {
            const FIntVector TargetCC = ChunkCoord + FIntVector(cx, cy, cz);
            TMap<int32, float> ModVoxels;
            if (!DataMap->GetChunkData(TargetCC, ModVoxels)) continue;

            for (const auto& Pair : ModVoxels)
            {
                const int32 LIdx = Pair.Key;
                const int32 lz   = LIdx / (ChunkSize * ChunkSize);
                const int32 ly   = (LIdx / ChunkSize) % ChunkSize;
                const int32 lx   = LIdx % ChunkSize;
                const float AX   = (TargetCC.X * ChunkSize + lx) * VoxelSize;
                const float AY   = (TargetCC.Y * ChunkSize + ly) * VoxelSize;
                const float AZ   = (TargetCC.Z * ChunkSize + lz) * VoxelSize;
                const int32 GX   = FMath::RoundToInt((AX - WorldOrigin.X) / EffVoxSz + 1.f);
                const int32 GY   = FMath::RoundToInt((AY - WorldOrigin.Y) / EffVoxSz + 1.f);
                const int32 GZ   = FMath::RoundToInt((AZ - WorldOrigin.Z) / EffVoxSz + 1.f);
                if (GX < 0 || GX >= EffSize || GY < 0 || GY >= EffSize || GZ < 0 || GZ >= EffSize) continue;
                const int32 FlatIdx = GX + GY * EffSize + GZ * EffSize * EffSize;
                DenseHasEdit [FlatIdx] = true;
                DenseEditVals[FlatIdx] = Pair.Value;
            }
        }
    }

    // FIX #10: fold solid/air counting into the parallel loop with atomics
    TAtomic<int32> SolidCount{0};
    TAtomic<int32> AirCount  {0};

    ParallelFor(EffSize * EffSize, [&](int32 FlatXY)
    {
        if (bCancelled) return;

        const int32 Y = FlatXY / EffSize;
        const int32 X = FlatXY % EffSize;
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

        float NeutralSurfH = SurfH;
        if (bEnCraters && Weights.GetWeight(EVoxelBiome::Craters) > 0.01f)
        {
            FVoxelBiomeWeightMap NW = Weights;
            NW.SetWeight(EVoxelBiome::Craters, 0.f);
            NW.Normalize();
            NeutralSurfH = FVoxelBiomeManager::GetSurfaceHeightStatic(WX, WY, NW, Config);
        }

        const int32 LX = X-1, LY2 = Y-1;
        if (LX >= 0 && LX < EffCS && LY2 >= 0 && LY2 < EffCS)
        {
            ColumnWeights [LX + LY2 * EffCS] = Weights;
            ColumnSurfaceH[LX + LY2 * EffCS] = SurfH;
        }

        const float MaxWZ = WorldOrigin.Z + (EffSize+1) * EffVoxSz;
        const float MinWZ = WorldOrigin.Z - EffVoxSz;

        FColumnContext Ctx;
        Ctx.SurfaceHeight = SurfH;
        Ctx.BiomeWeights  = Weights;
        Ctx.MaxWorldZ     = MaxWZ;

        if (bEnSurface)  SurfacePass.PrepareColumn(WX, WY, Config, Ctx);
        if (bEnCaves)    CavePass.PrepareColumn(WX, WY, Config, Ctx);

        if (bEnSkylands)
        {
            const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
            const float SkyLB = SurfH + SC.MinAltitudeAboveTerrain - SC.BaseIslandSize*SC.ThicknessRatio - 1000.f;
            if (MaxWZ < SkyLB)
            {
                Ctx.SkylandCache.bHasSkyland = false;
            }
            else
            {
                const int32 AX = (X-1)*StepSize;
                const int32 AY = (Y-1)*StepSize;
                if (AX >= 0 && AX < ChunkSize && AY >= 0 && AY < ChunkSize)
                    Ctx.SkylandCache = SkylandColumnCaches[AX * ChunkSize + AY]; // FIX #5
                else
                    Ctx.SkylandCache = FVoxelBiomeGenerators::GetSkylandColumnCache(WX,WY,SurfH,Weights,Config);
            }
        }

        // Air column early-out
        if (bEnSkylands)
        {
            const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
            const float OvH = Config.Performance.bEnableOverhangs ? Config.Overhangs.MaxDistFromSurface : 0.f;
            const float SafeAirMinZ = SurfH + OvH + 200.f;
            const float SkyLB = SurfH + SC.MinAltitudeAboveTerrain - SC.BaseIslandSize*SC.ThicknessRatio - 400.f;
            if (MinWZ > SafeAirMinZ && MaxWZ < SkyLB)
            {
                for (int32 Z = 0; Z < EffSize; ++Z)
                {
                    const int32 Idx = X + Y*EffSize + Z*EffSize*EffSize;
                    float D = -2.f;
                    // FIX #6: guard with IsEmpty() check
                    if (!DenseHasEdit.IsEmpty() && DenseHasEdit[Idx])
                    {   const float Ov = DenseEditVals[Idx];
                        D = (Ov < 0.f) ? FMath::Min(D,Ov) : FMath::Max(D,Ov); }
                    Densities[Idx] = D;
                    AirCount.IncrementExchange(); // FIX #10
                }
                return;
            }
        }

        if (MaxWZ < Config.CaveTunnels.BedrockDepth)
        {
            for (int32 Z = 0; Z < EffSize; ++Z)
            {
                const int32 Idx = X + Y*EffSize + Z*EffSize*EffSize;
                float D = 2.f;
                if (!DenseHasEdit.IsEmpty() && DenseHasEdit[Idx])
                {   const float Ov = DenseEditVals[Idx];
                    D = (Ov < 0.f) ? FMath::Min(D,Ov) : FMath::Max(D,Ov); }
                Densities[Idx] = D;
                SolidCount.IncrementExchange(); // FIX #10
            }
            return;
        }

        for (int32 Z = 0; Z < EffSize; ++Z)
        {
            const float WZ  = WorldOrigin.Z + (Z-1.f)*EffVoxSz;
            const int32 Idx = X + Y*EffSize + Z*EffSize*EffSize;

            float D = -2.f;
            if (bEnSurface)  D = SurfacePass.EvaluateVoxel(FVector(WX,WY,WZ), Ctx, Config, D);
            if (bEnCaves)    D = CavePass   .EvaluateVoxel(FVector(WX,WY,WZ), Ctx, Config, D);
            if (bEnSkylands) D = SkylandPass.EvaluateVoxel(FVector(WX,WY,WZ), Ctx, Config, D);

            if (!DenseHasEdit.IsEmpty() && DenseHasEdit[Idx]) // FIX #6
            {   const float Ov = DenseEditVals[Idx];
                D = (Ov < 0.f) ? FMath::Min(D,Ov) : FMath::Max(D,Ov); }

            Densities[Idx] = D;
            // FIX #10: count in-place, no separate serial scan needed
            if (D > 0.f) SolidCount.IncrementExchange();
            else         AirCount  .IncrementExchange();
        }
    });

    // FIX #10: results from atomics, CountDensityStates() no longer needed
    bIsFullSolid = ((int32)SolidCount == TotalSamples);
    bIsFullAir   = ((int32)AirCount   == TotalSamples);

    PostProcessDensities(TotalSamples);
}

void FVoxelGeneratorTask::PostProcessDensities(int32) { /* intentional no-op */ }

// ============================================================
//  BuildMesh
// ============================================================
void FVoxelGeneratorTask::BuildMesh()
{
    MeshOutput.Reset();
    FVoxelMeshGenerator::GenerateMesh(Densities, ChunkSize, VoxelSize, WorldOrigin, MeshOutput, Config, StepSize);
    UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelMesh: [%d,%d,%d] Verts=%d"),
        ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, MeshOutput.FlatMesh.Vertices.Num()));
}

// ============================================================
//  CalculateFoliage
// ============================================================
void FVoxelGeneratorTask::CalculateFoliage()
{
    LegacyTreeTransforms .Reset();
    LegacyGrassTransforms.Reset();
    PerFoliageTransforms .Reset();
    PerFoliageMeshes     .Reset();

    if (bIsFullAir || bIsFullSolid) return;

    PerFoliageTransforms.SetNum(FoliageSlots.Num());
    PerFoliageMeshes    .SetNum(FoliageSlots.Num());
    for (int32 s = 0; s < FoliageSlots.Num(); ++s)
        PerFoliageMeshes[s] = FoliageSlots[s].Mesh;

    const int32 EffCS     = ChunkSize / StepSize;
    const float EffVoxSz  = VoxelSize * StepSize;
    const int32 EffSize   = EffCS;

    auto RandH = [](int32 x, int32 y, int32 s, int32 seed) -> float
    {
        uint32 h = (uint32)(x*73856093 ^ y*19349663 ^ s*83492791 ^ seed);
        h = (h ^ (h>>16))*0x45d9f3b; h ^= h>>16;
        return (float)(h & 0xFFFFFF) / 16777215.f;
    };

    for (int32 LY = 0; LY < EffCS; ++LY)
    for (int32 LX = 0; LX < EffCS; ++LX)
    {
        const int32 CI = LX + LY * EffCS;
        if (!ColumnWeights.IsValidIndex(CI)) continue;

        const FVoxelBiomeWeightMap& W = ColumnWeights[CI];
        const float SurfH = ColumnSurfaceH.IsValidIndex(CI)
            ? ColumnSurfaceH[CI]
            : FVoxelBiomeManager::GetSurfaceHeightStatic(
                WorldOrigin.X + LX*EffVoxSz, WorldOrigin.Y + LY*EffVoxSz, W, Config);

        const int32 CellZ  = FMath::Clamp(FMath::RoundToInt((SurfH-WorldOrigin.Z)/EffVoxSz)+1, 1, EffSize+1);
        const FVector Norm = FVoxelMeshGenerator::ComputeNormal(Densities, LX+1, LY+1, CellZ, EffCS);
        const float SlopeZ = Norm.Z;
        const FVector ColPos(WorldOrigin.X + LX*EffVoxSz, WorldOrigin.Y + LY*EffVoxSz, SurfH);

        if (bHasPerBiomeFoliage)
        {
            for (int32 s = 0; s < FoliageSlots.Num(); ++s)
            {
                const FFoliageSlot&      Slot  = FoliageSlots[s];
                if (!Slot.Mesh) continue;
                const FVoxelFoliageEntry& Entry = Config.GetBiomeRender(Slot.Biome).FoliageTypes[Slot.EntryIdx];

                if (SlopeZ            < Entry.MinSlopeAlignment) continue;
                if (W.GetWeight(Slot.Biome) < Entry.MinBiomeWeight) continue;
                if (ColPos.Z < Entry.MinWorldZ || ColPos.Z > Entry.MaxWorldZ) continue;

                for (int32 A = 0; A < Entry.SpawnAttemptsPerTriangle; ++A)
                {
                    if (RandH(LX,LY,s*100+A,Config.Seed) >= Entry.SpawnChance) continue;

                    const float JX  = RandH(LX,LY,A*7, Config.Seed)*EffVoxSz;
                    const float JY  = RandH(LX,LY,A*13,Config.Seed)*EffVoxSz;
                    const FVector LP(LX*EffVoxSz+JX, LY*EffVoxSz+JY, SurfH-WorldOrigin.Z+Entry.HeightOffset);

                    FRotator Rot = FRotator::ZeroRotator;
                    if (Entry.bAlignToSurface) { Rot = Norm.ToOrientationRotator(); Rot.Pitch += 90.f; }
                    Rot.Yaw = Entry.bRandomYaw ? RandH(LX,LY,A*19,Config.Seed)*360.f : Entry.FixedYaw;
                    const float Scale = FMath::Lerp(Entry.ScaleMin, Entry.ScaleMax, RandH(LX,LY,A*23,Config.Seed));
                    PerFoliageTransforms[s].Add(FTransform(Rot, LP, FVector(Scale)));
                }
            }
        }
        else
        {
            // FIX #4: legacy fallback actually invoked when no per-biome slots configured
            const FVector WorldCenter(ColPos.X, ColPos.Y, SurfH);
            ProcessLegacyFoliage(FVector(LX*EffVoxSz, LY*EffVoxSz, SurfH-WorldOrigin.Z),
                                  SlopeZ, W, WorldCenter);
        }
    }

    TrimFoliageToCap(4000);
}

void FVoxelGeneratorTask::ProcessLegacyFoliage(const FVector& Center, float SlopeZ,
                                                const FVoxelBiomeWeightMap& W,
                                                const FVector& WorldCenter)
{
    if (SlopeZ < MaxFoliageSlope) return;
    if (WorldCenter.Z <= Config.SeaLevel + 200.f) return;

    const float ForestW = W.GetWeight(EVoxelBiome::Forest);
    const float RoughW  = W.GetRoughness();

    if (FMath::FRand() < FoliageDensity * 5.f * (ForestW + RoughW*0.5f))
        LegacyGrassTransforms.Add(FTransform(
            FRotator(0.f, FMath::FRand()*360.f, 0.f), Center,
            FVector(FMath::FRandRange(0.8f, 1.2f))));

    if (ForestW > 0.5f && FMath::FRand() < FoliageDensity)
        LegacyTreeTransforms.Add(FTransform(
            FRotator(0.f, FMath::FRand()*360.f, 0.f), Center,
            FVector(FMath::FRandRange(0.7f, 1.4f))));
}

// ============================================================
//  PlaceWaterSources
// ============================================================
void FVoxelGeneratorTask::PlaceWaterSources()
{
    WaterSources.Reset();
    if (StepSize > 1) return;

    const int32 CS  = ChunkSize;
    const int32 S   = CS + 3;
    const int32 EffS = CS;

    auto Dens    = [&](int32 lx,int32 ly,int32 lz) -> float
    {
        return Densities[FMath::Clamp(lx+1,0,S-1) + FMath::Clamp(ly+1,0,S-1)*S
                        + FMath::Clamp(lz+1,0,S-1)*S*S];
    };
    auto IsSolid = [&](int32 x,int32 y,int32 z) { return Dens(x,y,z) > 0.f; };
    auto IsAir   = [&](int32 x,int32 y,int32 z) { return Dens(x,y,z) <= 0.f; };

    auto RandH = [](int32 x, int32 y, int32 z, int32 seed) -> float
    {
        uint32 h = (uint32)(x*73856093^y*19349663^z*83492791^seed);
        h = (h^(h>>16))*0x45d9f3b; h^=h>>16;
        return (float)(h&0xFFFFFF)/(float)0xFFFFFF;
    };

    for (int32 lz = 0; lz < CS; ++lz)
    for (int32 ly = 0; ly < CS; ++ly)
    for (int32 lx = 0; lx < CS; ++lx)
    {
        if (bCancelled) return;
        if (!IsAir(lx,ly,lz) || !IsSolid(lx,ly,lz-1)) continue;

        const float WZ = WorldOrigin.Z + lz*VoxelSize;
        if (!Config.Water.bUseVoxelOcean && Config.Water.bEnableOcean && WZ <= Config.SeaLevel+VoxelSize) continue;

        const bool bCave = IsSolid(lx,ly,lz+1);
        if (Config.Water.bEnableOcean && WZ <= Config.SeaLevel+VoxelSize && !bCave) continue;

        int32 SN = 0;
        if (IsSolid(lx+1,ly,lz)) SN++;
        if (IsSolid(lx-1,ly,lz)) SN++;
        if (IsSolid(lx,ly+1,lz)) SN++;
        if (IsSolid(lx,ly-1,lz)) SN++;
        if (SN < 2) continue;

        const float MinSkyAlt = Config.SeaLevel + Config.SkylandsLayer.MinAltitudeAboveTerrain;
        const bool  bSky      = WZ >= MinSkyAlt;

        float SpawnChance = 0.f;
        if (bSky)
        {
            const FVoxelBiomeWaterConfig& BWC = Config.SkylandsWater;
            if (!BWC.bEnableLakes || !IsSolid(lx,ly,lz-2)) continue;
            SpawnChance = FMath::Clamp(BWC.LakeSpawnProbability*0.12f, 0.f,1.f);
        }
        else
        {
            const int32 gX = FMath::Clamp(lx, 0, EffS-1);
            const int32 gY = FMath::Clamp(ly, 0, EffS-1);
            const int32 CI = gX + gY*EffS;
            FVoxelBiomeWeightMap W;
            if (ColumnWeights.IsValidIndex(CI)) W = ColumnWeights[CI];
            const FVoxelBiomeWaterConfig& BWC = Config.GetBiomeWater(W.GetDominantBiome());
            if (!BWC.bEnableLakes) continue;
            const float EF = (SN==4)?1.5f:(SN==3)?1.1f:0.7f;
            SpawnChance = FMath::Clamp(BWC.LakeSpawnProbability*EF*0.15f, 0.f,1.f);
        }

        if (RandH(lx,ly,lz,Config.Seed) < SpawnChance)
            WaterSources.Add(FIntVector(ChunkCoord.X*CS+lx, ChunkCoord.Y*CS+ly, ChunkCoord.Z*CS+lz));
    }
}

// ============================================================
//  CountDensityStates — FIX #10: folded into parallel loop, kept as no-op
// ============================================================
void FVoxelGeneratorTask::CountDensityStates(int32) { /* folded into parallel loop */ }

// ============================================================
//  TrimFoliageToCap — FIX #11: proportional trim
// ============================================================
void FVoxelGeneratorTask::TrimFoliageToCap(const int32 Cap)
{
    int32 Total = LegacyTreeTransforms.Num() + LegacyGrassTransforms.Num();
    for (const auto& ST : PerFoliageTransforms) Total += ST.Num();
    if (Total <= Cap) return;

    UE_LOG(LogVoxelChunk, Warning,
        TEXT("Chunk [%d,%d,%d] foliage cap: %d > %d — proportional trim."),
        ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, Total, Cap);

    // FIX #11: trim each slot proportionally to its share of the excess
    const float TrimRatio = FMath::Clamp(1.f - (float)Cap / (float)Total, 0.f, 1.f);

    for (auto& ST : PerFoliageTransforms)
    {
        const int32 Remove = FMath::CeilToInt(ST.Num() * TrimRatio);
        if (Remove > 0) ST.SetNum(FMath::Max(0, ST.Num() - Remove));
    }
    {
        const int32 R = FMath::CeilToInt(LegacyGrassTransforms.Num() * TrimRatio);
        if (R > 0) LegacyGrassTransforms.SetNum(FMath::Max(0, LegacyGrassTransforms.Num()-R));
    }
    {
        const int32 R = FMath::CeilToInt(LegacyTreeTransforms.Num() * TrimRatio);
        if (R > 0) LegacyTreeTransforms.SetNum(FMath::Max(0, LegacyTreeTransforms.Num()-R));
    }
}
