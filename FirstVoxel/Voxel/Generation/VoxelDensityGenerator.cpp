// VoxelDensityGenerator.cpp
// Composes the full 3-layer density field for any world-space coordinate.
// This is the ONLY place where all three layers meet -- it keeps Surface,
// Skylands, and Caves in clearly separated code blocks.
//
// PERFORMANCE IMPROVEMENTS over the previous version:
//
//   1. Config.GetSeedOffset() is now computed ONCE at the top of GetDensityFull()
//      and the result is forwarded to every sub-system. Previously it ran the
//      LCG hash 2-3 times per voxel (overhangs path + cave bedrock path +
//      SampleCaveNoise internal call = up to 3 redundant hashes per voxel).
//
//   2. GetSkylandDensity() is skipped entirely for voxels that lie below the
//      lowest possible edge of any island band. That function is the most
//      expensive noise call in the pipeline (multiple FBM passes + domain
//      warping). For a typical ground-level chunk the early-out fires for
//      every voxel, eliminating the call completely for that chunk.
//
//   3. SampleCaveNoise() now accepts the pre-computed SeedOff vector rather
//      than calling GetSeedOffset() internally, saving a 4th LCG hash for
//      every below-surface solid voxel that has an active cave region.

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
    return GetDensityFull(FVector(X, Y, Z), Weights, SurfH, Config);
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
    const FVoxelGenerationConfig& Config)
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
        if (DepthBelow > CVC.MinDepthBelowSurface)
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
                // Limit tunnel carving to prevent entire chunks from being hollowed out
                const float MaxTunnelCarve = 1.2f;
                SurfD -= FMath::Min(TunnelCarve, MaxTunnelCarve);
            }
        }

        // Crystal caverns: large carved chambers deep underground.
        const float CavernDelta = FVoxelBiomeGenerators::GetCrystalCavernDelta(
            X, Y, Z, SurfaceHeight, Config);
        
        // Limit cavern carving to prevent chunk-filling voids
        const float MaxCavernCarve = 1.0f;
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
    const float SkyLowerBound = SurfaceHeight
        + SC.MinAltitudeAboveTerrain
        - (SC.BaseIslandSize * SC.ThicknessRatio)
        - 400.f;   // 400 cm safety margin

    float SkyD = -2.f;
    if (Z >= SkyLowerBound)
    {
        SkyD = FVoxelBiomeGenerators::GetSkylandDensity(
            X, Y, Z, SurfaceHeight, Weights, Config);
    }

    // Combine: max() ensures skylands always override empty air,
    // while surface terrain fills in where no island exists.
    return FMath::Clamp(FMath::Max(SkyD, SurfD), -2.f, 2.f);
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
