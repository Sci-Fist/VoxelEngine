// VoxelDensityGenerator.cpp
//
// ROOT FIX applied here:
//
// FVoxelCavePass::PrepareColumn used to zero crater weights and call
// GetSurfaceHeightStatic(NW) hoping to get pre-crater terrain. But that
// function ignores its weight parameter and always calls GetCraterHeight()
// at the end. The "neutral" height was identical to the crater-modified one.
// Fix: call GetNeutralSurfaceHeightStatic() which genuinely omits the crater.
//
// FVoxelSkylandPass::PrepareColumn used SurfaceHeight (crater-modified) for:
//   1. SkyLB early-out: at crater floor (1840cm), SkyLB = 1840+8000-600-1000 = 8240
//      → chunks at Z=8240-16240 got bHasSkyland=true → islands spawned there
//      → those Z values are INSIDE the solid crater walls → buried islands
//   2. GetSkylandColumnCache height input: HeightNorm = 1840/25000 = 0.07
//      → system thinks terrain is near sea level → small shards, not islands
//      → island SkyAlt = 1840+8000 = 9840cm → inside solid wall material
//
// Fix: use NeutralSurfaceHeight (~9840cm) for both SkyLB and the cache.
// With neutral height: HeightNorm = 9840/25000 = 0.39, SkyAlt ≈ 19840cm
// → islands spawn 7500cm ABOVE the crater rim (12340cm) → visible floating islands.
//
// GetDensityFull fallback also fixed to use NeutralSurfaceHeight.
//
// GEOLOGICAL MICRO-DETAIL Pass (v17.0):
//   Implemented ApplyGlobalBoulders and ApplySpeleothems to add micro-detail 
//   (rocks, stalactites) to high-LOD chunks (StepSize=1).

#include "Voxel/Generation/VoxelDensityGenerator.h"
#include "FirstVoxel.h"
#include "Voxel/Biomes/VoxelBiomeManager.h"
#include "Voxel/Biomes/VoxelBiomeGenerators.h"
#include "Voxel/Config/VoxelGenerationConfig.h"

// ============================================================
//  IVoxelDensityProvider
// ============================================================
float FVoxelDensityGenerator::GetDensity(float X, float Y, float Z, const FVoxelGenerationConfig& Config)
{
    float Temp = -999.f, Erosion = -999.f;
    const FVoxelBiomeWeightMap W = FVoxelBiomeManager::GetBiomeWeightsStatic(X, Y, Config, &Temp, &Erosion);
    const float SurfH   = FVoxelBiomeManager::GetSurfaceHeightStatic(X, Y, W, Config, Temp, Erosion);
    const float NeutralH = FVoxelBiomeManager::GetNeutralSurfaceHeightStatic(X, Y, Config, Temp, Erosion);

    // Build skyland cache with NEUTRAL height so islands aren't anchored to crater floor
    const FSkylandColumnCache Cache = FVoxelBiomeGenerators::GetSkylandColumnCache(
        X, Y, NeutralH, W, Config);
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
//  Micro-Detail Helpers — forward declarations (defined after GetDensityFull)
// ============================================================

/** Remaps PerlinNoise3D [-1,1] output to [0,1] for threshold comparisons. */
FORCEINLINE static float BG_Noise(float X, float Y, float Z)
{
    return FMath::PerlinNoise3D(FVector(X, Y, Z)) * 0.5f + 0.5f;
}

static void ApplyGlobalBoulders(float& D, const FVector& WorldPos,
                                const FVoxelBiomeWeightMap& W,
                                float SurfH, const FVoxelGenerationConfig& C);
static void ApplySpeleothems(float& D, const FVector& WorldPos,
                             float NeutralH, const FVoxelGenerationConfig& C);

// ============================================================
//  GetDensityFull — 4-layer composition
// ============================================================
/** 
 * Main 5-layer density composition pipeline.
 * Evaluates Surface, Caves, Bedrock, Skylands, and Micro-Detail in order.
 */
float FVoxelDensityGenerator::GetDensityFull(
    const FVector& WorldPos, const FVoxelBiomeWeightMap& Weights,
    float SurfaceHeight, float NeutralSurfaceHeight,
    const FVoxelGenerationConfig& Config, int32 StepSize,
    const FSkylandColumnCache* SkylandCache)
{
    const float X = WorldPos.X, Y = WorldPos.Y, Z = WorldPos.Z;
    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;

    // if (SurfaceHeight < Z - 4000.f) return -1.f;

    const FVector SeedOff = Config.GetSeedOffset();

    // ── Layer 1: Surface ─────────────────────────────────────────────────────
    float SurfD = FVoxelBiomeManager::GetBaseSurfaceDensity(Z, SurfaceHeight, Config);

    if (Config.Performance.bEnableOverhangs)
    {
        const FOverhangConfig& OC = Config.Overhangs;
        const float Dist  = FMath::Abs(Z - SurfaceHeight);
        const float SteepW = Weights.Cliffs + Weights.Peaks + Weights.Craters * 1.5f;
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
        const float DepthBelow = FMath::Max(0.f, SurfaceHeight - Z);
        const float EffMinDepth = FMath::Max(CVC.MinDepthBelowSurface, 600.f);
        if (DepthBelow > EffMinDepth)
        {
            const float SF = FMath::Clamp((DepthBelow-CVC.MinDepthBelowSurface)/CVC.SurfaceFadeDepth, 0.f,1.f);
            const float BJ = FMath::PerlinNoise3D(FVector(X*CVC.BedrockJagFrequency,Y*CVC.BedrockJagFrequency,0.f))*CVC.BedrockJagAmplitude;
            const float BF = FMath::Clamp((Z-(CVC.BedrockDepth+BJ))/1000.f, 0.f,1.f);
            const float CF = SF*BF;
            if (CF > 0.f)
            {
                const float Tunnel = SampleCaveNoise(WorldPos, SeedOff, Config) * CF;
                SurfD -= FMath::Min(Tunnel, 0.85f);
            }
        }
        // Use NeutralSurfaceHeight for cavern depth — craters don't affect cave reference
        const float CavernDelta = FVoxelBiomeGenerators::GetCrystalCavernDelta(
            X, Y, Z, NeutralSurfaceHeight, Config);
        SurfD += FMath::Clamp(CavernDelta, -0.6f, 0.6f);
    }

    // ── Layer 3: Bedrock ─────────────────────────────────────────────────────
    if (Z < Config.CaveTunnels.BedrockDepth) SurfD = 2.f;

    // ── Layer 4: Skylands ────────────────────────────────────────────────────
    float SkyD = -2.f;
    if (SkylandCache)
    {
        const float X_orig = SkylandCache->WX_base - SeedOff.X;
        const float Y_orig = SkylandCache->WY_base - SeedOff.Y;
        SkyD = FVoxelBiomeGenerators::GetSkylandDensityFromCache(
            *SkylandCache, X_orig, Y_orig, Z, Config, StepSize);
    }
    else
    {
        // Fallback: build cache on the fly using NEUTRAL height
        const float SkyLB = NeutralSurfaceHeight + SC.MinAltitudeAboveTerrain
                          - SC.BaseIslandSize * SC.ThicknessRatio - 400.f;
        if (Z >= SkyLB)
            SkyD = FVoxelBiomeGenerators::GetSkylandDensity(
                X, Y, Z, NeutralSurfaceHeight, Weights, Config, StepSize);
    }

    // ── Layer 5: Micro-Detail (v17.0) ────────────────────────────────────────
    /**
     * Micro-detail is strictly gated to LOD 0 (StepSize <= 1) to ensure zero 
     * performance impact on distant streaming chunks.
     */
    if (StepSize <= 1)
    {
        ApplyGlobalBoulders(SurfD, WorldPos, Weights, SurfaceHeight, Config);
        if (SurfD < 0.1f) // If inside cave air
        {
            ApplySpeleothems(SurfD, WorldPos, NeutralSurfaceHeight, Config);
        }
    }

    return FMath::Max(SkyD, SurfD);
}

// ============================================================
//  SampleCaveNoise
// ============================================
float FVoxelDensityGenerator::SampleCaveNoise(
    const FVector& WorldPos, const FVector& SeedOff, const FVoxelGenerationConfig& Config)
{
    const FCaveTunnelsConfig& CVC = Config.CaveTunnels;
    const FVector P = WorldPos + SeedOff;
    const float cs  = CVC.Scale;
    const float C1  = FMath::Abs(FMath::PerlinNoise3D(FVector(P.X*cs, P.Y*cs, P.Z*cs)));

    const float MaxThresh = CVC.Threshold + CVC.WobbleAmplitude;
    if (C1 >= MaxThresh) return 0.f;

    const float C2 = FMath::Abs(FMath::PerlinNoise3D(FVector(P.X*cs*0.7f, P.Y*cs*0.7f, P.Z*cs*1.3f+5.f)));
    const float CV = C1 + C2;
    if (CV >= MaxThresh) return 0.f;

    const float Wobble = FMath::PerlinNoise3D(FVector(
        P.X*CVC.WobbleFrequency, P.Y*CVC.WobbleFrequency, P.Z*CVC.WobbleFrequency))
        * CVC.WobbleAmplitude;
    const float Thresh = CVC.Threshold + Wobble;
    if (CV < Thresh) { const float t = 1.f - CV/Thresh; return t * CVC.Strength; }
    return 0.f;
}

// =============================================================================
//  Concrete Pipeline Stages
// =============================================================================

// ── FVoxelSurfacePass ─────────────────────────────────────────────────────────
void FVoxelSurfacePass::PrepareColumn(float WorldX, float WorldY,
                                       const FVoxelGenerationConfig& Config,
                                       FColumnContext& OutContext) const
{
    OutContext.BedrockHeight    = Config.CaveTunnels.BedrockDepth;
    OutContext.CachedSeedOffset = Config.GetSeedOffset();
}

float FVoxelSurfacePass::EvaluateVoxel(const FVector& WorldPos,
                                         const FColumnContext& Context,
                                         const FVoxelGenerationConfig& Config,
                                         float CurrentDensity) const
{
    float D = FVoxelBiomeManager::GetBaseSurfaceDensity(WorldPos.Z, Context.SurfaceHeight, Config);

    if (Config.Performance.bEnableOverhangs && Context.StepSize <= 1)
    {
        const FOverhangConfig& OC = Config.Overhangs;
        const float Dist   = FMath::Abs(WorldPos.Z - Context.SurfaceHeight);
        const float SteepW = Context.BiomeWeights.Cliffs + Context.BiomeWeights.Peaks;
        if (WorldPos.Z > Config.SeaLevel && Dist < OC.MaxDistFromSurface && SteepW > 0.05f)
        {
            const FVector& Off = Context.CachedSeedOffset;
            const float Near   = FMath::Clamp(1.f - Dist/OC.MaxDistFromSurface, 0.f, 1.f);
            const float Ov     = FMath::PerlinNoise3D(FVector(
                (WorldPos.X+Off.X)*OC.NoiseFrequency,
                (WorldPos.Y+Off.Y)*OC.NoiseFrequency,
                (WorldPos.Z+Off.Z)*OC.NoiseFrequency*1.8f));
            D += Ov * Near * OC.Amplitude * SteepW;
        }
    }

    // ── Layer 5: Micro-Detail (v17.0) ─────────────────────────────────────────
    if (Context.StepSize <= 1)
    {
        ApplyGlobalBoulders(D, WorldPos, Context.BiomeWeights, Context.SurfaceHeight, Config);
    }

    return D;
}

// ── FVoxelCavePass ────────────────────────────────────────────────────────────
void FVoxelCavePass::PrepareColumn(float WorldX, float WorldY,
                                    const FVoxelGenerationConfig& Config,
                                    FColumnContext& OutContext) const
{
    OutContext.NeutralSurfaceHeight = FVoxelBiomeManager::GetNeutralSurfaceHeightStatic(
        WorldX, WorldY, Config);

    if (OutContext.CachedSeedOffset == FVector::ZeroVector)
        OutContext.CachedSeedOffset = Config.GetSeedOffset();

    const FCaveTunnelsConfig& CVC = Config.CaveTunnels;
    OutContext.BedrockJag = FMath::PerlinNoise3D(FVector(WorldX*CVC.BedrockJagFrequency, WorldY*CVC.BedrockJagFrequency, 0.f)) * CVC.BedrockJagAmplitude;
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
        const float DepthBelow  = FMath::Max(0.f, Context.SurfaceHeight - WorldPos.Z);
        const float EffMinDepth = FMath::Max(CVC.MinDepthBelowSurface, 600.f);
        if (DepthBelow > EffMinDepth)
        {
            const float SF = FMath::Clamp((DepthBelow-CVC.MinDepthBelowSurface)/CVC.SurfaceFadeDepth, 0.f,1.f);
            const float BJ = Context.BedrockJag;
            const float BF = FMath::Clamp((WorldPos.Z-(CVC.BedrockDepth+BJ))/1000.f, 0.f,1.f);
            const float CF = SF*BF;
            if (CF > 0.f && Context.StepSize <= 1)
            {
                const float T = FVoxelDensityGenerator::SampleCaveNoise(
                    WorldPos, Context.CachedSeedOffset, Config) * CF;
                D -= FMath::Min(T, 0.85f);
            }
        }
        const float CD = FVoxelBiomeGenerators::GetCrystalCavernDelta(
            WorldPos.X, WorldPos.Y, WorldPos.Z, Context.NeutralSurfaceHeight, Config);
        D += FMath::Clamp(CD, -0.6f, 0.6f);
    }

    // ── Layer 5: Micro-Detail (v17.0) ─────────────────────────────────────────
    if (Context.StepSize <= 1 && D < 0.10f)
    {
        ApplySpeleothems(D, WorldPos, Context.NeutralSurfaceHeight, Config);
    }

    if (WorldPos.Z < Context.BedrockHeight) D = 2.f;
    return D;
}

// ── FVoxelSkylandPass ─────────────────────────────────────────────────────────
void FVoxelSkylandPass::PrepareColumn(float WorldX, float WorldY,
                                       const FVoxelGenerationConfig& Config,
                                       FColumnContext& OutContext) const
{
    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;

    // ROOT FIX: use NeutralSurfaceHeight for the lower-bound check.
    // Old: SkyLB used SurfaceHeight (crater floor = 1840cm) → SkyLB = 8240cm
    //      → chunks at 8240-16240cm got bHasSkyland=true → islands buried in wall.
    // New: SkyLB uses NeutralSurfaceHeight (pre-crater = 9840cm) → SkyLB = 16240cm
    //      → only chunks above 16240cm generate skylands → islands float above rim.
    const float NeutralH = OutContext.NeutralSurfaceHeight;
    const float SkyLB = NeutralH + SC.MinAltitudeAboveTerrain
                      - SC.BaseIslandSize * SC.ThicknessRatio - 1000.f;

    if (OutContext.MaxWorldZ < SkyLB)
    {
        OutContext.SkylandCache.bHasSkyland = false;
        return;
    }

    if (OutContext.CachedSeedOffset == FVector::ZeroVector)
        OutContext.CachedSeedOffset = Config.GetSeedOffset();

    // ROOT FIX: build skyland cache with NEUTRAL height.
    // Old: passed SurfaceHeight → HeightNorm = 1840/25000 = 0.07 (near sea level)
    //      → island SkyAlt = 1840 + 8000 = 9840cm → buried in crater wall.
    // New: pass NeutralSurfaceHeight → HeightNorm = 9840/25000 = 0.39
    //      → island SkyAlt = 9840 + ~10000 = ~19840cm → above rim, visible.
    OutContext.SkylandCache = FVoxelBiomeGenerators::GetSkylandColumnCache(
        WorldX, WorldY, NeutralH, OutContext.BiomeWeights, Config);
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
        const FVector& Off = Context.CachedSeedOffset;
        const float X_orig = Context.SkylandCache.WX_base - Off.X;
        const float Y_orig = Context.SkylandCache.WY_base - Off.Y;
        SkyD = FVoxelBiomeGenerators::GetSkylandDensityFromCache(
            Context.SkylandCache, X_orig, Y_orig, WorldPos.Z, Config, Context.StepSize);
    }

    return FMath::Max(SkyD, CurrentDensity);
}

// =============================================================================
//  MICRO-DETAIL IMPLEMENTATIONS
// =============================================================================

/** 
 * Adds high-frequency rock outcroppings to the surface layer.
 * Remaps BG_Noise relative to biome weights for peaks and cliffs.
 */
static void ApplyGlobalBoulders(float& D, const FVector& WorldPos, const FVoxelBiomeWeightMap& W, 
                                float SurfH, const FVoxelGenerationConfig& C)
{
    // Skip if we're clearly below the surface crust
    if (WorldPos.Z < SurfH - 800.f || WorldPos.Z > SurfH + 1200.f) return;

    const float BFreq = 0.0025f;
    const float BWeight = W.GetWeight(EVoxelBiome::Peaks) * 0.8f + W.GetWeight(EVoxelBiome::Cliffs) * 1.2f;
    if (BWeight < 0.2f) return;

    // BG_Noise already remaps [-1,1] -> [0,1]
    const float Noise = BG_Noise(WorldPos.X * BFreq, WorldPos.Y * BFreq, WorldPos.Z * BFreq * 0.5f);
    if (Noise > 0.88f)
    {
        const float Strength = (Noise - 0.88f) / 0.12f;
        D += Strength * 0.45f * BWeight;
    }
}

/** 
 * Seeds stalactites and stalagmites in deep cavern air.
 * Prevents surface breaches by anchoring to NeutralSurfaceHeight.
 */
static void ApplySpeleothems(float& D, const FVector& WorldPos, float NeutralH, 
                             const FVoxelGenerationConfig& C)
{
    const FCrystalCavernsConfig& CVC = C.CaveCrystals;
    
    // Use deep cave threshold to ensure we don't break surface
    const float DeepCaveZ = NeutralH - CVC.DepthStart;
    if (WorldPos.Z > DeepCaveZ) return;

    // Only apply to open air (D < 0) or thin boundaries
    if (D > 0.1f) return;

    const float SFreq = 0.004f;
    const float Noise = BG_Noise(WorldPos.X * SFreq, WorldPos.Y * SFreq, WorldPos.Z * SFreq * 2.5f);
    
    // Remap noise [0,1] — speleothems appear at peak values > 0.92
    if (Noise > 0.92f)
    {
        const float Strength = (Noise - 0.92f) / 0.08f;
        // Add vertical bias to create spike-like shapes
        const float Spikiness = FMath::Clamp(1.0f - FMath::Abs(FMath::Sin(WorldPos.Z * 0.01f)), 0.2f, 1.0f);
        D += Strength * Spikiness * 0.65f;
    }
}
