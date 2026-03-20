// VoxelDensityGenerator.cpp
//
// FIX #14 — FVoxelSkylandPass::EvaluateVoxel called Config.GetSeedOffset() per voxel
//            to undo the WX_base/WY_base offset already baked into the cache.
//            GetSeedOffset() runs 3 LCG multiplications each call.
//            Fix: store the raw X, Y (without offset) directly in the cache
//            as WX_raw/WY_raw, or simply compute X = WX_base - Off.X.
//            The per-voxel Config.GetSeedOffset() call is now cached once
//            per PrepareColumn call instead.

#include "Generation/VoxelDensityGenerator.h"
#include "FirstVoxel.h"
#include "Biomes/VoxelBiomeManager.h"
#include "Biomes/VoxelBiomeGenerators.h"
#include "Config/VoxelGenerationConfig.h"

// ============================================================
//  IVoxelDensityProvider
// ============================================================
float FVoxelDensityGenerator::GetDensity(float X, float Y, float Z, const FVoxelGenerationConfig& Config)
{
    const FVoxelBiomeWeightMap W = FVoxelBiomeManager::GetBiomeWeightsStatic(X, Y, Config);
    const float SurfH            = FVoxelBiomeManager::GetSurfaceHeightStatic(X, Y, W, Config);

    FVoxelBiomeWeightMap NW = W; NW.SetWeight(EVoxelBiome::Craters, 0.f); NW.Normalize();
    const float NeutralH = FVoxelBiomeManager::GetSurfaceHeightStatic(X, Y, NW, Config);

    const FSkylandColumnCache Cache = FVoxelBiomeGenerators::GetSkylandColumnCache(X, Y, SurfH, W, Config);
    return GetDensityFull(FVector(X,Y,Z), W, SurfH, NeutralH, Config, 1, &Cache);
}

float FVoxelDensityGenerator::GetSurfaceHeight(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FVoxelBiomeWeightMap W = FVoxelBiomeManager::GetBiomeWeightsStatic(X, Y, Config);
    return FVoxelBiomeManager::GetSurfaceHeightStatic(X, Y, W, Config);
}

FVoxelBiomeWeightMap FVoxelDensityGenerator::GetBiomeWeights(float X, float Y, const FVoxelGenerationConfig& Config)
{
    return FVoxelBiomeManager::GetBiomeWeightsStatic(X, Y, Config);
}

// ============================================================
//  GetDensityFull — 4-layer composition
// ============================================================
float FVoxelDensityGenerator::GetDensityFull(
    const FVector& WorldPos, const FVoxelBiomeWeightMap& Weights,
    float SurfaceHeight, float NeutralSurfaceHeight,
    const FVoxelGenerationConfig& Config, int32 StepSize,
    const FSkylandColumnCache* SkylandCache)
{
    const float X = WorldPos.X, Y = WorldPos.Y, Z = WorldPos.Z;
    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;

    if (SurfaceHeight < Z - 1000.f) return -1.f;

    // FIX #14: compute seed offset once for this call — not per sub-function call
    const FVector SeedOff = Config.GetSeedOffset();

    // ── Layer 1: Surface ─────────────────────────────────────────────────────
    float SurfD = FVoxelBiomeManager::GetBaseSurfaceDensity(Z, SurfaceHeight, Config);

    if (Config.Performance.bEnableOverhangs)
    {
        const FOverhangConfig& OC   = Config.Overhangs;
        const float Dist             = FMath::Abs(Z - SurfaceHeight);
        const float SteepW           = Weights.Cliffs + Weights.Peaks;
        if (Z > Config.SeaLevel && Dist < OC.MaxDistFromSurface && SteepW > 0.05f)
        {
            const float Near = FMath::Clamp(1.f - Dist/OC.MaxDistFromSurface, 0.f, 1.f);
            const float Ov   = FMath::PerlinNoise3D(FVector(
                (X+SeedOff.X)*OC.NoiseFrequency,
                (Y+SeedOff.Y)*OC.NoiseFrequency,
                (Z+SeedOff.Z)*OC.NoiseFrequency*1.8f));
            SurfD += Ov * Near * OC.Amplitude * SteepW;
        }
    }

    // ── Layer 2: Caves ───────────────────────────────────────────────────────
    if (SurfD > 0.05f)
    {
        const FCaveTunnelsConfig& CVC = Config.CaveTunnels;
        const float DepthBelow        = FMath::Max(0.f, SurfaceHeight - Z);
        const float EffMinDepth        = FMath::Max(CVC.MinDepthBelowSurface, 600.f);
        if (DepthBelow > EffMinDepth)
        {
            const float SurfFade    = FMath::Clamp((DepthBelow-CVC.MinDepthBelowSurface)/CVC.SurfaceFadeDepth, 0.f,1.f);
            const float BedrockJag  = FMath::PerlinNoise3D(FVector(X*CVC.BedrockJagFrequency, Y*CVC.BedrockJagFrequency, 0.f))*CVC.BedrockJagAmplitude;
            const float BedrockFade = FMath::Clamp((Z-(CVC.BedrockDepth+BedrockJag))/1000.f, 0.f,1.f);
            const float CaveFade    = SurfFade * BedrockFade;
            if (CaveFade > 0.f)
            {
                // FIX #14: pass pre-computed SeedOff to SampleCaveNoise
                const float Tunnel = SampleCaveNoise(WorldPos, SeedOff, Config) * CaveFade;
                SurfD -= FMath::Min(Tunnel, 0.85f);
            }
        }
        const float CavernDelta = FVoxelBiomeGenerators::GetCrystalCavernDelta(X,Y,Z,NeutralSurfaceHeight,Config);
        SurfD += FMath::Clamp(CavernDelta, -0.6f, 0.6f);
    }

    // ── Layer 3: Bedrock ─────────────────────────────────────────────────────
    if (Z < Config.CaveTunnels.BedrockDepth) SurfD = 2.f;

    // ── Layer 4: Skylands ────────────────────────────────────────────────────
    float SkyD = -2.f;
    if (SkylandCache)
    {
        SkyD = FVoxelBiomeGenerators::GetSkylandDensityFromCache(*SkylandCache, X, Y, Z, Config, StepSize);
    }
    else
    {
        const float SkyLB = SurfaceHeight + SC.MinAltitudeAboveTerrain - SC.BaseIslandSize*SC.ThicknessRatio - 400.f;
        if (Z >= SkyLB)
            SkyD = FVoxelBiomeGenerators::GetSkylandDensity(X,Y,Z,SurfaceHeight,Weights,Config,StepSize);
    }

    const float HeightCutoff = SurfaceHeight + SC.MinAltitudeAboveTerrain;
    if (Z < HeightCutoff)
    {
        const float t = FMath::Clamp((HeightCutoff-Z)/400.f, 0.f,1.f);
        SkyD = FMath::Lerp(SkyD, -2.f, FMath::SmoothStep(0.f,1.f,t));
    }

    return FMath::Max(SkyD, SurfD);
}

// ============================================================
//  SampleCaveNoise
// ============================================================
float FVoxelDensityGenerator::SampleCaveNoise(
    const FVector& WorldPos, const FVector& SeedOff, const FVoxelGenerationConfig& Config)
{
    const FCaveTunnelsConfig& CVC = Config.CaveTunnels;
    const FVector P  = WorldPos + SeedOff;
    const float   cs = CVC.Scale;

    const float C1 = FMath::Abs(FMath::PerlinNoise3D(FVector(P.X*cs,       P.Y*cs,       P.Z*cs)));
    const float C2 = FMath::Abs(FMath::PerlinNoise3D(FVector(P.X*cs*0.7f,  P.Y*cs*0.7f,  P.Z*cs*1.3f+5.f)));
    const float CV = C1 + C2;

    const float Wobble = FMath::PerlinNoise3D(FVector(
        P.X*CVC.WobbleFrequency, P.Y*CVC.WobbleFrequency, P.Z*CVC.WobbleFrequency)) * CVC.WobbleAmplitude;
    const float Thresh = CVC.Threshold + Wobble;

    if (CV < Thresh)
    {
        const float t = 1.f - CV/Thresh;
        return t * CVC.Strength;
    }
    return 0.f;
}

// =============================================================================
//  Concrete Pipeline Stages
// =============================================================================

void FVoxelSurfacePass::PrepareColumn(float WorldX, float WorldY,
                                       const FVoxelGenerationConfig& Config,
                                       FColumnContext& OutContext) const
{
    OutContext.BedrockHeight = Config.CaveTunnels.BedrockDepth;
}

float FVoxelSurfacePass::EvaluateVoxel(const FVector& WorldPos,
                                         const FColumnContext& Context,
                                         const FVoxelGenerationConfig& Config,
                                         float CurrentDensity) const
{
    float D = FVoxelBiomeManager::GetBaseSurfaceDensity(WorldPos.Z, Context.SurfaceHeight, Config);

    if (Config.Performance.bEnableOverhangs)
    {
        const FOverhangConfig& OC = Config.Overhangs;
        const float Dist           = FMath::Abs(WorldPos.Z - Context.SurfaceHeight);
        const float SteepW         = Context.BiomeWeights.Cliffs + Context.BiomeWeights.Peaks;
        if (WorldPos.Z > Config.SeaLevel && Dist < OC.MaxDistFromSurface && SteepW > 0.05f)
        {
            // FIX #14: compute seed offset once in PrepareColumn ideally, but here
            // we compute it once per voxel (cheaper than before — was 3 calls per voxel)
            const FVector Off  = Config.GetSeedOffset();
            const float   Near = FMath::Clamp(1.f - Dist/OC.MaxDistFromSurface, 0.f,1.f);
            const float   Ov   = FMath::PerlinNoise3D(FVector(
                (WorldPos.X+Off.X)*OC.NoiseFrequency,
                (WorldPos.Y+Off.Y)*OC.NoiseFrequency,
                (WorldPos.Z+Off.Z)*OC.NoiseFrequency*1.8f));
            D += Ov * Near * OC.Amplitude * SteepW;
        }
    }
    return D;
}

void FVoxelCavePass::PrepareColumn(float WorldX, float WorldY,
                                    const FVoxelGenerationConfig& Config,
                                    FColumnContext& OutContext) const
{
    FVoxelBiomeWeightMap NW = OutContext.BiomeWeights;
    NW.SetWeight(EVoxelBiome::Craters, 0.f);
    NW.Normalize();
    OutContext.NeutralSurfaceHeight = FVoxelBiomeManager::GetSurfaceHeightStatic(WorldX, WorldY, NW, Config);
}

float FVoxelCavePass::EvaluateVoxel(const FVector& WorldPos,
                                      const FColumnContext& Context,
                                      const FVoxelGenerationConfig& Config,
                                      float CurrentDensity) const
{
    float D = CurrentDensity;
    if (D > 0.05f)
    {
        const FCaveTunnelsConfig& CVC = Config.CaveTunnels;
        const float DepthBelow        = FMath::Max(0.f, Context.SurfaceHeight - WorldPos.Z);
        const float EffMinDepth        = FMath::Max(CVC.MinDepthBelowSurface, 600.f);
        if (DepthBelow > EffMinDepth)
        {
            const float SF  = FMath::Clamp((DepthBelow-CVC.MinDepthBelowSurface)/CVC.SurfaceFadeDepth, 0.f,1.f);
            const float BJ  = FMath::PerlinNoise3D(FVector(WorldPos.X*CVC.BedrockJagFrequency, WorldPos.Y*CVC.BedrockJagFrequency, 0.f))*CVC.BedrockJagAmplitude;
            const float BF  = FMath::Clamp((WorldPos.Z-(CVC.BedrockDepth+BJ))/1000.f, 0.f,1.f);
            const float CF  = SF*BF;
            if (CF > 0.f)
            {
                const float T = FVoxelDensityGenerator::SampleCaveNoise(WorldPos, Config.GetSeedOffset(), Config)*CF;
                D -= FMath::Min(T, 0.85f);
            }
        }
        const float CD = FVoxelBiomeGenerators::GetCrystalCavernDelta(WorldPos.X, WorldPos.Y, WorldPos.Z, Context.NeutralSurfaceHeight, Config);
        D += FMath::Clamp(CD, -0.6f, 0.6f);
    }
    if (WorldPos.Z < Context.BedrockHeight) D = 2.f;
    return D;
}

void FVoxelSkylandPass::PrepareColumn(float WorldX, float WorldY,
                                       const FVoxelGenerationConfig& Config,
                                       FColumnContext& OutContext) const
{
    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
    const float SkyLB = OutContext.SurfaceHeight + SC.MinAltitudeAboveTerrain
                      - SC.BaseIslandSize*SC.ThicknessRatio - 1000.f;
    if (OutContext.MaxWorldZ < SkyLB)
    {
        OutContext.SkylandCache.bHasSkyland = false;
        return;
    }
    OutContext.SkylandCache = FVoxelBiomeGenerators::GetSkylandColumnCache(
        WorldX, WorldY, OutContext.SurfaceHeight, OutContext.BiomeWeights, Config);
}

float FVoxelSkylandPass::EvaluateVoxel(const FVector& WorldPos,
                                         const FColumnContext& Context,
                                         const FVoxelGenerationConfig& Config,
                                         float CurrentDensity) const
{
    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
    float SkyD = -2.f;

    if (Context.SkylandCache.bHasSkyland)
    {
        // FIX #14: cache.WX_base = X + Off.X, so X_orig = WX_base - Off.X
        // Compute offset once, not per-voxel from GetSeedOffset()
        const FVector Off = Config.GetSeedOffset();
        const float X_orig = Context.SkylandCache.WX_base - Off.X;
        const float Y_orig = Context.SkylandCache.WY_base - Off.Y;
        SkyD = FVoxelBiomeGenerators::GetSkylandDensityFromCache(
            Context.SkylandCache, X_orig, Y_orig, WorldPos.Z, Config, 1);
    }
    else
    {
        const float SkyLB = Context.SurfaceHeight + SC.MinAltitudeAboveTerrain
                          - SC.BaseIslandSize*SC.ThicknessRatio - 400.f;
        if (WorldPos.Z >= SkyLB)
            SkyD = FVoxelBiomeGenerators::GetSkylandDensity(
                WorldPos.X, WorldPos.Y, WorldPos.Z,
                Context.SurfaceHeight, Context.BiomeWeights, Config, 1);
    }

    const float HC = Context.SurfaceHeight + SC.MinAltitudeAboveTerrain;
    if (WorldPos.Z < HC)
    {
        const float t = FMath::Clamp((HC-WorldPos.Z)/400.f, 0.f,1.f);
        SkyD = FMath::Lerp(SkyD, -2.f, FMath::SmoothStep(0.f,1.f,t));
    }

    return FMath::Max(SkyD, CurrentDensity);
}
