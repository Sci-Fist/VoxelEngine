// VoxelDensityGenerator.cpp
// 
// Core density field generator that composes the complete 3D voxel world.
// This is the central orchestrator that combines all terrain generation layers
// into a single signed distance field value for each world coordinate.
//
// ARCHITECTURE OVERVIEW:
// The generator follows a layered approach where each layer modifies the density
// field in sequence. Later layers can override earlier ones, creating the final
// terrain shape.
//
// PERFORMANCE OPTIMIZATIONS:
// 1. Seed Offset Caching: Config.GetSeedOffset() is computed once per voxel
//    evaluation and reused across all sub-systems, eliminating redundant LCG
//    hash calculations (previously 2-3 hashes per voxel).
//
// 2. Skyland Early-Out: GetSkylandDensity() is skipped for voxels below the
//    lowest possible island band, saving the most expensive noise evaluation
//    for ground-level chunks where no skylands exist.
//
// 3. Pre-computed Seed Offsets: SampleCaveNoise() accepts pre-computed seed
//    offsets to avoid redundant hash calculations in the cave generation path.
//
// LAYER PRIORITY (highest to lowest):
// - Skylands: Always override air, create floating islands
// - Surface: Base terrain with optional overhangs
// - Caves: Carve tunnels and chambers (only in solid terrain)
// - Bedrock: Force solid below bedrock depth

#include "Generation/VoxelDensityGenerator.h"
#include "FirstVoxel.h"
#include "Biomes/VoxelBiomeManager.h"
#include "Biomes/VoxelBiomeGenerators.h"
#include "Config/VoxelGenerationConfig.h"

// ============================================================
//  IVoxelDensityProvider implementation
// ============================================================
float FVoxelDensityGenerator::GetDensity(float X, float Y, float Z, const FVoxelGenerationConfig& Config)
{
    const FVoxelBiomeWeightMap Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(X, Y, Config);
    const float SurfH                  = FVoxelBiomeManager::GetSurfaceHeightStatic(X, Y, Weights, Config);

    FVoxelBiomeWeightMap NeutralWeights = Weights;
    NeutralWeights.SetWeight(EVoxelBiome::Craters, 0.f);
    NeutralWeights.Normalize();
    const float NeutralSurfH = FVoxelBiomeManager::GetSurfaceHeightStatic(X, Y, NeutralWeights, Config);

    // Compute continuous cache for Central Difference neighbor lookup coherence
    const FSkylandColumnCache ColumnCache = FVoxelBiomeGenerators::GetSkylandColumnCache(X, Y, SurfH, Weights, Config);

    return GetDensityFull(FVector(X, Y, Z), Weights, SurfH, NeutralSurfH, Config, 1, &ColumnCache);
}

float FVoxelDensityGenerator::GetSurfaceHeight(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FVoxelBiomeWeightMap Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(X, Y, Config);
    return FVoxelBiomeManager::GetSurfaceHeightStatic(X, Y, Weights, Config);
}

FVoxelBiomeWeightMap FVoxelDensityGenerator::GetBiomeWeights(float X, float Y, const FVoxelGenerationConfig& Config)
{
    return FVoxelBiomeManager::GetBiomeWeightsStatic(X, Y, Config);
}

// ============================================================
//  GetDensityFull -- the full 3-layer composition
//
//  Layer order:
//    1. Surface  -- height-field signed distance + optional overhangs
//    2. Caves    -- worm tunnels + crystal caverns (carved from solid only)
//    3. Bedrock  -- hard floor always forced solid
//    4. Skylands -- floating islands (skipped when Z is below island band)
//
//  Final density = max(SkyD, SurfD) so skylands always override air.
// ============================================================
float FVoxelDensityGenerator::GetDensityFull(
    const FVector&              WorldPos,
    const FVoxelBiomeWeightMap& Weights,
    float                       SurfaceHeight,
    float                       NeutralSurfaceHeight,
    const FVoxelGenerationConfig& Config,
    int32 StepSize,
    const FSkylandColumnCache* SkylandCache)
{
    const float X = WorldPos.X, Y = WorldPos.Y, Z = WorldPos.Z;
    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;

    // Compute the world-space noise offset once and reuse throughout this call.
    // GetSeedOffset() runs a small integer hash -- cheap, but previously called
    // 2-3 times per voxel across overhangs / cave bedrock / SampleCaveNoise.
    const FVector SeedOff = Config.GetSeedOffset();

    // ============================================================
    //  LAYER 1: SURFACE
    //  Signed-distance ramp centred on SurfaceHeight.
    //  Positive = solid, negative = air.
    // ============================================================
    float SurfD = FVoxelBiomeManager::GetBaseSurfaceDensity(Z, SurfaceHeight, Config);
    // Clamp removed to preserve smooth interpolation gradient.


    // Optional overhangs: protrusions on steep cliff/peak faces near the surface.
    // Gated on SteepnessWeight > 0.05 so flat plains never pay the noise cost.
    if (Config.Performance.bEnableOverhangs)
    {
        const FOverhangConfig& OC           = Config.Overhangs;
        const float DistFromSurface         = FMath::Abs(Z - SurfaceHeight);
        const float SteepnessWeight         = Weights.Cliffs + Weights.Peaks;

        if (Z > Config.SeaLevel
            && DistFromSurface < OC.MaxDistFromSurface
            && SteepnessWeight > 0.05f)
        {
            const float NearSurface = FMath::Clamp(
                1.f - DistFromSurface / OC.MaxDistFromSurface, 0.f, 1.f);

            const float Overhang = FMath::PerlinNoise3D(FVector(
                (X + SeedOff.X) * OC.NoiseFrequency,
                (Y + SeedOff.Y) * OC.NoiseFrequency,
                (Z + SeedOff.Z) * OC.NoiseFrequency * 1.8f));

            // Scale by steepness: overhangs only appear on rough terrain.
            SurfD += Overhang * NearSurface * OC.Amplitude * SteepnessWeight;
        }
    }

    // ============================================================
    //  LAYER 2: CAVES (only carve where we are already solid)
    // ============================================================
    if (SurfD > 0.05f)
    {
        const FCaveTunnelsConfig& CVC = Config.CaveTunnels;
        const float DepthBelow        = FMath::Max(0.f, SurfaceHeight - Z);

        // Worm tunnels: fade in below MinDepthBelowSurface, fade out toward bedrock.
        // Ensure minimum robust padding below landscape so no caves break the surface crust
        const float EffectiveMinDepth = FMath::Max(CVC.MinDepthBelowSurface, 600.f); 
        if (DepthBelow > EffectiveMinDepth)
        {
            const float SurfFade = FMath::Clamp(
                (DepthBelow - CVC.MinDepthBelowSurface) / CVC.SurfaceFadeDepth, 0.f, 1.f);

            // Jagged bedrock floor variation (XY only -- no Z variation needed).
            const float BedrockJag = FMath::PerlinNoise3D(FVector(
                X * CVC.BedrockJagFrequency,
                Y * CVC.BedrockJagFrequency,
                0.f)) * CVC.BedrockJagAmplitude;

            const float EffBedrock  = CVC.BedrockDepth + BedrockJag;
            const float BedrockFade = FMath::Clamp((Z - EffBedrock) / 1000.f, 0.f, 1.f);
            const float CaveFade    = SurfFade * BedrockFade;

            if (CaveFade > 0.f)
            {
                // Pass SeedOff in so SampleCaveNoise does not need to recompute it.
                const float TunnelCarve = SampleCaveNoise(WorldPos, SeedOff, Config) * CaveFade;
                // Limit tunnel carving so one uniform tunnel layer cannot hollow an entire chunk.
                const float MaxTunnelCarve = 0.85f;
                SurfD -= FMath::Min(TunnelCarve, MaxTunnelCarve);
            }
        }

        // Crystal caverns: large carved chambers deep underground.
        const float CavernDelta = FVoxelBiomeGenerators::GetCrystalCavernDelta(
            X, Y, Z, NeutralSurfaceHeight, Config);
        // Limit cavern carve so chambers cannot turn a whole chunk into one void.
        const float MaxCavernCarve = 0.6f;
        SurfD += FMath::Clamp(CavernDelta, -MaxCavernCarve, MaxCavernCarve);
    }

    // ============================================================
    //  LAYER 3: BEDROCK FLOOR -- always solid below this line
    // ============================================================
    if (Z < Config.CaveTunnels.BedrockDepth)
        SurfD = 2.f;

    // ============================================================
    //  LAYER 4: SKYLANDS
    //
    //  Early-out: skip GetSkylandDensity() when Z is clearly below
    //  the lowest edge of any possible island band.
    //
    //  Conservative lower bound calculation:
    //    - Island centre altitude = SurfaceHeight + MinAltitudeAboveTerrain
    //    - Island bottom edge     = centre - (BaseIslandSize * ThicknessRatio)
    //    - Safety margin of 400 cm ensures we never skip a real island voxel
    //
    //  For a ground-level chunk (Z ~= SurfaceHeight), this fires for every
    //  voxel, eliminating the most expensive noise evaluation entirely.
    //  For a mid-air skyland chunk the condition is false and the full
    //  evaluation runs normally.
    // ============================================================
    float SkyD = -2.f;
    if (SkylandCache)
    {
        SkyD = FVoxelBiomeGenerators::GetSkylandDensityFromCache(*SkylandCache, X, Y, Z, Config, StepSize);
    }
    else
    {
        const float SkyLowerBound = SurfaceHeight
            + SC.MinAltitudeAboveTerrain
            - (SC.BaseIslandSize * SC.ThicknessRatio)
            - 400.f;   // 400 cm safety margin

        if (Z >= SkyLowerBound)
        {
            SkyD = FVoxelBiomeGenerators::GetSkylandDensity(
                X, Y, Z, SurfaceHeight, Weights, Config, StepSize);
        }
    }

    // Final composition: Terrain takes precedence, skylands override air above.
    // Water is NOT part of the density field:
    //   - Ocean surface is a flat UStaticMeshComponent driven by VoxelWaterComponent.
    //   - Voxel pools and flow are simulated by FVoxelWaterSimulator post-generation.
    // Adding water density here caused two bugs:
    //   (a) WaterD=1.5 below SeaLevel re-solidified carved caves -> filled entire chunks.
    //   (b) Skyland depression sampling ran 8x GetBiomeWeightsStatic per skyland voxel -> massive perf hit.

    // ---- ☁️ CLEARANCE GATE: Force absolute air gap above local terrain ----
    // This solves cell-center averaging overlaps by enforcing SC.MinAltitudeAboveTerrain
    // continuously against the local height coordinate.
    const float HeightCutoff = SurfaceHeight + SC.MinAltitudeAboveTerrain;
    const float FadeDist = 400.f; // 4 meters smooth fade
    if (Z < HeightCutoff)
    {
        const float t = FMath::Clamp((HeightCutoff - Z) / FadeDist, 0.f, 1.f);
        const float SmoothT = FMath::SmoothStep(0.f, 1.f, t);
        SkyD = FMath::Lerp(SkyD, -2.f, SmoothT);
    }

    // Final composition: Terrain takes precedence, skylands override air above.
    float FinalDensity = FMath::Max(SkyD, SurfD);

    return FinalDensity;

}

// ============================================================
//  SampleCaveNoise
//  Two offset Perlin tunnel fields merged for natural branching.
//  Accepts SeedOff pre-computed by the caller to avoid a redundant
//  GetSeedOffset() call for every below-surface solid voxel.
// ============================================================
float FVoxelDensityGenerator::SampleCaveNoise(
    const FVector&              WorldPos,
    const FVector&              SeedOff,
    const FVoxelGenerationConfig& Config)
{
    const FCaveTunnelsConfig& CVC = Config.CaveTunnels;
    const FVector P  = WorldPos + SeedOff;
    const float   cs = CVC.Scale;

    // Two offset tunnel noise fields merged -- creates natural branching.
    const float Cave1   = FMath::Abs(FMath::PerlinNoise3D(FVector(P.X * cs,        P.Y * cs,        P.Z * cs)));
    const float Cave2   = FMath::Abs(FMath::PerlinNoise3D(FVector(P.X * cs * 0.7f, P.Y * cs * 0.7f, P.Z * cs * 1.3f + 5.f)));
    const float CaveVal = Cave1 + Cave2;

    // Wobble threshold: varies tunnel diameter organically along its length.
    const float Wobble = FMath::PerlinNoise3D(FVector(
        P.X * CVC.WobbleFrequency,
        P.Y * CVC.WobbleFrequency,
        P.Z * CVC.WobbleFrequency)) * CVC.WobbleAmplitude;

    const float EffThreshold = CVC.Threshold + Wobble;
    if (CaveVal < EffThreshold)
    {
        // Smooth ramp: carve strength is strongest at the tunnel axis.
        const float t = 1.f - (CaveVal / EffThreshold);
        return t * CVC.Strength;
    }
    return 0.f;
}

// =============================================================================
//  Concrete Pipeline Stages Implementation
// =============================================================================

// ---- FVoxelSurfacePass ----

void FVoxelSurfacePass::PrepareColumn(float WorldX, float WorldY, const FVoxelGenerationConfig& Config, FColumnContext& OutContext) const
{
	OutContext.BedrockHeight = Config.CaveTunnels.BedrockDepth;
}

float FVoxelSurfacePass::EvaluateVoxel(const FVector& WorldPos, const FColumnContext& Context, const FVoxelGenerationConfig& Config, float CurrentDensity) const
{
	const float X = WorldPos.X, Y = WorldPos.Y, Z = WorldPos.Z;
	float SurfD = FVoxelBiomeManager::GetBaseSurfaceDensity(Z, Context.SurfaceHeight, Config);

	if (Config.Performance.bEnableOverhangs)
	{
		const FOverhangConfig& OC           = Config.Overhangs;
		const float DistFromSurface         = FMath::Abs(Z - Context.SurfaceHeight);
		const float SteepnessWeight         = Context.BiomeWeights.Cliffs + Context.BiomeWeights.Peaks;

		if (Z > Config.SeaLevel && DistFromSurface < OC.MaxDistFromSurface && SteepnessWeight > 0.05f)
		{
			const float NearSurface = FMath::Clamp(1.f - DistFromSurface / OC.MaxDistFromSurface, 0.f, 1.f);
			// FIX: Cache GetSeedOffset() once instead of calling 3x per voxel.
			// GetSeedOffset() runs 3 LCG hashes — previously called 3 times = 9 hashes per overhang voxel.
			// Now cached = 3 hashes total.
			const FVector SeedOff = Config.GetSeedOffset();
			const float Overhang = FMath::PerlinNoise3D(FVector(
				(X + SeedOff.X) * OC.NoiseFrequency,
				(Y + SeedOff.Y) * OC.NoiseFrequency,
				(Z + SeedOff.Z) * OC.NoiseFrequency * 1.8f));
			SurfD += Overhang * NearSurface * OC.Amplitude * SteepnessWeight;
		}
	}
	return SurfD;
}

// ---- FVoxelCavePass ----

void FVoxelCavePass::PrepareColumn(float WorldX, float WorldY, const FVoxelGenerationConfig& Config, FColumnContext& OutContext) const
{
	FVoxelBiomeWeightMap CavernWeights = OutContext.BiomeWeights;
	CavernWeights.SetWeight(EVoxelBiome::Craters, 0.f);
	CavernWeights.Normalize();
	OutContext.NeutralSurfaceHeight = FVoxelBiomeManager::GetSurfaceHeightStatic(WorldX, WorldY, CavernWeights, Config);
}

float FVoxelCavePass::EvaluateVoxel(const FVector& WorldPos, const FColumnContext& Context, const FVoxelGenerationConfig& Config, float CurrentDensity) const
{
	const float X = WorldPos.X, Y = WorldPos.Y, Z = WorldPos.Z;
	float D = CurrentDensity;

	if (D > 0.05f)
	{
		const FCaveTunnelsConfig& CVC = Config.CaveTunnels;
		const float DepthBelow        = FMath::Max(0.f, Context.SurfaceHeight - Z);
		const float EffectiveMinDepth = FMath::Max(CVC.MinDepthBelowSurface, 600.f); 

		if (DepthBelow > EffectiveMinDepth)
		{
			const float SurfFade = FMath::Clamp((DepthBelow - CVC.MinDepthBelowSurface) / CVC.SurfaceFadeDepth, 0.f, 1.f);
			const float BedrockJag = FMath::PerlinNoise3D(FVector(X * CVC.BedrockJagFrequency, Y * CVC.BedrockJagFrequency, 0.f)) * CVC.BedrockJagAmplitude;
			const float EffBedrock  = CVC.BedrockDepth + BedrockJag;
			const float BedrockFade = FMath::Clamp((Z - EffBedrock) / 1000.f, 0.f, 1.f);
			const float CaveFade    = SurfFade * BedrockFade;

			if (CaveFade > 0.f)
			{
				const float TunnelCarve = FVoxelDensityGenerator::SampleCaveNoise(WorldPos, Config.GetSeedOffset(), Config) * CaveFade;
				D -= FMath::Min(TunnelCarve, 0.85f);
			}
		}

		const float CavernDelta = FVoxelBiomeGenerators::GetCrystalCavernDelta(X, Y, Z, Context.NeutralSurfaceHeight, Config);
		D += FMath::Clamp(CavernDelta, -0.6f, 0.6f);
	}

	if (Z < Context.BedrockHeight) D = 2.f;

	return D;
}

// ---- FVoxelSkylandPass ----

void FVoxelSkylandPass::PrepareColumn(float WorldX, float WorldY, const FVoxelGenerationConfig& Config, FColumnContext& OutContext) const
{
	const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
	
	const float SkyLowerBound = OutContext.SurfaceHeight 
		+ SC.MinAltitudeAboveTerrain 
		- (SC.BaseIslandSize * SC.ThicknessRatio) 
		- 1000.f; // safety margin

	if (OutContext.MaxWorldZ < SkyLowerBound)
	{
		// Column is safely below the skyland belt; skip heavy neighbor cells list sampling
		OutContext.SkylandCache.bHasSkyland = false;
		return;
	}

	OutContext.SkylandCache = FVoxelBiomeGenerators::GetSkylandColumnCache(WorldX, WorldY, OutContext.SurfaceHeight, OutContext.BiomeWeights, Config);
}

float FVoxelSkylandPass::EvaluateVoxel(const FVector& WorldPos, const FColumnContext& Context, const FVoxelGenerationConfig& Config, float CurrentDensity) const
{
	const float X = WorldPos.X, Y = WorldPos.Y, Z = WorldPos.Z;
	const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
	float SkyD = -2.f;


	if (Context.SkylandCache.bHasSkyland)

	{

		// FIX: Recover original coordinates (without seed offset) from the cache's base coordinates.
		// The cache's WX_base/WY_base already include the seed offset. Passing X,Y (which are

		// effective-cell corners) directly causes double offset, breaking skyland placement.

		const FVector Off = Config.GetSeedOffset();

		const float X_orig = Context.SkylandCache.WX_base - Off.X;

		const float Y_orig = Context.SkylandCache.WY_base - Off.Y;
		SkyD = FVoxelBiomeGenerators::GetSkylandDensityFromCache(Context.SkylandCache, X_orig, Y_orig, Z, Config, 1);

	}

	else
	{
		const float SkyLowerBound = Context.SurfaceHeight + SC.MinAltitudeAboveTerrain - (SC.BaseIslandSize * SC.ThicknessRatio) - 400.f;
		if (Z >= SkyLowerBound)
		{
			SkyD = FVoxelBiomeGenerators::GetSkylandDensity(X, Y, Z, Context.SurfaceHeight, Context.BiomeWeights, Config, 1);
		}
	}

	const float HeightCutoff = Context.SurfaceHeight + SC.MinAltitudeAboveTerrain;
	const float FadeDist = 400.f; // 4 meters smooth fade
	if (Z < HeightCutoff)
	{
		const float t = FMath::Clamp((HeightCutoff - Z) / FadeDist, 0.f, 1.f);
		const float SmoothT = FMath::SmoothStep(0.f, 1.f, t);
		SkyD = FMath::Lerp(SkyD, -2.f, SmoothT);
	}

	return FMath::Max(SkyD, CurrentDensity);
}
