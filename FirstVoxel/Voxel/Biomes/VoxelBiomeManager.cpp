// VoxelBiomeManager.cpp
// 
// Core biome distribution system that manages 2D surface biome blending.
// Uses orthogonal noise fields to create natural biome transitions across the world.
//
// BIOME DISTRIBUTION SYSTEM:
// The system uses two primary noise dimensions to determine biome placement:
// 
// 1. Temperature Axis (X): Controls climate-based biomes
//    - Low values (0.0-0.5): Cold/temperate regions → Forest, Peaks
//    - High values (0.5-1.0): Hot/dry regions → Desert, Mesa
//
// 2. Erosion Axis (Y): Controls terrain roughness
//    - Low values (0.0-0.5): Flat/eroded terrain → Forest, Desert
//    - High values (0.5-1.0): Rough/eroded terrain → Peaks, Cliffs
//
// BIOME MAPPING:
// - Forest: Low erosion + any temperature (temperate plains)
// - Desert: Low erosion + high temperature (arid flatlands)
// - Peaks: High erosion + low temperature (alpine mountains)
// - Cliffs: High erosion + high temperature (canyons/rocky shores)
// - Mesa: Moderate erosion + high temperature (plateau regions)
// - Craters: Noise-driven sparse impact sites (overrides other biomes locally)
//
// The resulting weight map is normalized to sum to 1.0 and used for:
// - Surface height blending
// - Material assignment
// - Skyland roughness estimation
// - Foliage distribution

#include "VoxelBiomeManager.h"
#include "FirstVoxel.h"
#include "../VoxelLogger.h"
#include "VoxelBiomeGenerators.h"

DEFINE_LOG_CATEGORY(LogVoxelBiome);

// ============================================================
//  GetBiomeWeightsStatic
//  Evaluates the temperature and erosion fields at (X, Y) and maps
//  them to per-biome weights using smooth threshold functions.
//
//  Biome mapping:
//    Forest  — low erosion (flat), any temperature
//    Peaks   — high erosion (rough) + cool temperature
//    Cliffs  — high erosion (rough) + warm temperature
//    Mesa    — moderate erosion + hot temperature
//    Craters — noise-driven sparse impact sites, any condition
// ============================================================
FVoxelBiomeWeightMap FVoxelBiomeManager::GetBiomeWeightsStatic(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FVector Off = Config.GetSeedOffset();
    const float Temp    = GetTemperatureWithSeed(X, Y, Config, Off);
    const float Erosion = GetErosionWithSeed    (X, Y, Config, Off);

    const FBiomeBlendConfig& B   = Config.BiomeBlend;

    FVoxelBiomeWeightMap Map;

    // Forest: thrives in low-erosion (flat) areas with temperate temperatures.
    // Smooth transitions to prevent hard biome boundaries
    float ForestW = FMath::SmoothStep(0.0f, 0.6f, 1.0f - Erosion) 
                       * FMath::SmoothStep(0.0f, 1.0f, 1.5f - Temp); // taper down at high temp

    // Desert: thrives in low-erosion + hot temperature.
    // Smooth transitions to prevent hard boundaries with Forest
    float DesertW = FMath::SmoothStep(0.0f, 0.6f, 1.0f - Erosion)
                       * FMath::SmoothStep(0.0f, 0.25f, Temp - 0.70f);

    // Peaks: trigger at high erosion (rarer) and steeper rise
    // Smooth transitions to prevent hard boundaries with Cliffs
    float PeaksW = FMath::SmoothStep(0.0f, 0.2f, Erosion - 0.68f) * B.PeaksStrength
                       * FMath::SmoothStep(0.0f, 0.8f, 1.2f - Temp);

    // Cliffs: rough + warm. Ridged terrain in drier, warmer zones.
    // Smooth transitions to prevent hard boundaries with Peaks
    float CliffsW = FMath::SmoothStep(0.0f, 0.2f, Erosion - 0.50f) * B.CliffsStrength
                        * FMath::SmoothStep(0.0f, 0.6f, Temp * 1.3f - 0.15f);

    // Mesa: hot + moderate erosion. The sharp temperature cutoff gives Mesa a distinctive zone.
    // Smooth transitions to prevent hard boundaries with Desert
    float MesaW = FMath::SmoothStep(0.0f, 0.2f, Temp - 0.62f) * B.MesaStrength
                      * FMath::SmoothStep(0.0f, 0.2f, 1.f - FMath::Abs(Erosion - 0.4f));

    // Craters: rare, driven by a separate low-frequency noise not related to Temp/Erosion.
    // Uses the Z=200 slice as a pseudo-2D crater placement field to avoid
    // 2D Perlin noise aliasing artifacts at certain coordinate ranges.
    // Frequency is halved (*0.5f) to produce larger, smoother crater zones.
    const float CraterNoise = FMath::PerlinNoise3D(FVector(
        (X + Off.X) * (Config.Craters.Frequency * 0.5f),
        (Y + Off.Y) * (Config.Craters.Frequency * 0.5f),
        200.f));

    // SmoothStep remaps crater noise to a 0-1 weight.
    // The +0.1f creates a dead-zone near the threshold to prevent
    // near-zero crater weights from bleeding into other biomes.
    float CratersW = FMath::SmoothStep(
        Config.Craters.ImpactThreshold + 0.1f,
        Config.Craters.ImpactThreshold,
        CraterNoise);

    // NOTE: Forced crater system removed. 
    // FindCraterSpawnLocation() in VoxelWorldGeneration.cpp always finds a natural crater
    // and centers the world on it, so the player always spawns in a natural crater.

    // NOTE: The old hard-coded origin crater boost was removed.
    // AVoxelWorld::GenerateWorldDeferred() uses FindCraterSpawnLocation() to
    // search the noise field for a real crater and centres the entire world
    // generation on that XY — so the player always spawns inside a genuine
    // crater without any biome map corruption at world origin.

    // --- Ocean: low altitude below sea-level ---
    // FIX: Only calculate ForestHeight when needed. Previously it was always computed
    // (5 noise calls) just for Ocean detection, even when Forest was disabled.
    float OceanW = 0.f;
    if (Config.Performance.bEnableForest)
    {
        const float ForestHeight = FVoxelBiomeGenerators::GetForestHeight(X, Y, Config);
        OceanW = FMath::SmoothStep(Config.SeaLevel - 200.f, Config.SeaLevel - 800.f, ForestHeight);
    }

    // --- Crater Override Mask ---
    // Make craters blend smoothly with other biomes.
    const float SafeCraterFactor = 1.0f - CratersW;
    ForestW *= SafeCraterFactor; DesertW *= SafeCraterFactor;
    PeaksW  *= SafeCraterFactor; CliffsW *= SafeCraterFactor;
    MesaW   *= SafeCraterFactor; OceanW  *= SafeCraterFactor;

    // Ocean Override: Suppress others if Ocean is dominant to solidify biome type
    if (OceanW > 0.6f)
    {
        const float Suppress = 1.0f - OceanW;
        ForestW *= Suppress; DesertW *= Suppress; MesaW *= Suppress;
    }

    // --- Toggle Enforcement ---
    if (!Config.Performance.bEnableForest)  ForestW  = 0.f;
    if (!Config.Performance.bEnableDesert)  DesertW  = 0.f;
    if (!Config.Performance.bEnablePeaks)   PeaksW   = 0.f;
    if (!Config.Performance.bEnableCliffs)  CliffsW  = 0.f;
    if (!Config.Performance.bEnableMesa)    MesaW    = 0.f;
    if (!Config.Performance.bEnableCraters) CratersW = 0.f;

    Map.SetWeight(EVoxelBiome::Forest,  ForestW);
    Map.SetWeight(EVoxelBiome::Peaks,   PeaksW);
    Map.SetWeight(EVoxelBiome::Cliffs,  CliffsW);
    Map.SetWeight(EVoxelBiome::Mesa,    MesaW);
    Map.SetWeight(EVoxelBiome::Craters, CratersW);
    Map.SetWeight(EVoxelBiome::Desert,  DesertW);
    Map.SetWeight(EVoxelBiome::Ocean,   OceanW);

    Map.Normalize();
    return Map;
}

/** Convenience wrapper that computes biome weights and surface height in one call.
 *  Avoids duplicate GetSeedOffset() and weight computation when both are needed.
 *  @param X, Y   World position in cm.
 *  @param Config  Generation configuration.
 *  @return Struct containing both the weight map and blended surface height. */
FVoxelBiomeManager::FWeightsAndHeight FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(float X, float Y, const FVoxelGenerationConfig& Config)
{
    FWeightsAndHeight Out;
    Out.Weights = GetBiomeWeightsStatic(X, Y, Config);
    Out.SurfaceHeight = GetSurfaceHeightStatic(X, Y, Out.Weights, Config);
    return Out;
}

// ============================================================
//  GetSurfaceHeightStatic
//  Additive weighted blend of all surface biome height functions.
//  Each biome only contributes when its weight is significant,
//  saving CPU on pure single-biome regions.
// ============================================================
float FVoxelBiomeManager::GetSurfaceHeightStatic(float X, float Y, const FVoxelBiomeWeightMap& Weights, const FVoxelGenerationConfig& Config)
{
    float Height = 0.f;
    float BaseWeightSum = 0.f;

    if (Weights.GetWeight(EVoxelBiome::Forest) > 0.01f)
    {
        Height += FVoxelBiomeGenerators::GetForestHeight(X, Y, Config) * Weights.GetWeight(EVoxelBiome::Forest);
        BaseWeightSum += Weights.GetWeight(EVoxelBiome::Forest);
    }

    if (Weights.GetWeight(EVoxelBiome::Desert) > 0.01f)
    {
        Height += FVoxelBiomeGenerators::GetDesertHeight(X, Y, Config) * Weights.GetWeight(EVoxelBiome::Desert);
        BaseWeightSum += Weights.GetWeight(EVoxelBiome::Desert);
    }

    if (Weights.GetWeight(EVoxelBiome::Peaks) > 0.01f)
    {
        Height += FVoxelBiomeGenerators::GetPeaksHeight(X, Y, Config) * Weights.GetWeight(EVoxelBiome::Peaks);
        BaseWeightSum += Weights.GetWeight(EVoxelBiome::Peaks);
    }

    if (Weights.GetWeight(EVoxelBiome::Cliffs) > 0.01f)
    {
        Height += FVoxelBiomeGenerators::GetCliffsHeight(X, Y, Config) * Weights.GetWeight(EVoxelBiome::Cliffs);
        BaseWeightSum += Weights.GetWeight(EVoxelBiome::Cliffs);
    }

    if (Weights.GetWeight(EVoxelBiome::Mesa) > 0.01f)
    {
        Height += FVoxelBiomeGenerators::GetMesaHeight(X, Y, Config) * Weights.GetWeight(EVoxelBiome::Mesa);
        BaseWeightSum += Weights.GetWeight(EVoxelBiome::Mesa);
    }

    // Normalize base height
    Height /= (BaseWeightSum + 0.0001f);

    // Apply craters as an overlay modifier layer
    Height = FVoxelBiomeGenerators::GetCraterHeight(X, Y, Config, Height);

    return Height;
}

/** Sample the temperature noise field at (X, Y).
 *  Returns a normalized value in [0, 1] where 0 = cold, 1 = hot.
 *  Uses 2D Perlin noise with the seed offset applied for reproducibility.
 *  The *0.5f + 0.5f remaps [-1, 1] noise output to [0, 1] range. */
float FVoxelBiomeManager::GetTemperatureWithSeed(float X, float Y, const FVoxelGenerationConfig& Config, const FVector& SeedOff)
{
    return FMath::PerlinNoise2D(FVector2D(
        (X + SeedOff.X) * Config.BiomeBlend.TemperatureFrequency,
        (Y + SeedOff.Y) * Config.BiomeBlend.TemperatureFrequency))
        * 0.5f + 0.5f;
}

/** Sample the erosion noise field at (X, Y).
 *  Returns a normalized value in [0, 1] where 0 = flat, 1 = rugged.
 *  The +100.f spatial offset decorrelates erosion from temperature noise
 *  so the two fields produce independent biome distributions.
 *  The *0.5f + 0.5f remaps [-1, 1] noise output to [0, 1] range. */
float FVoxelBiomeManager::GetErosionWithSeed(float X, float Y, const FVoxelGenerationConfig& Config, const FVector& SeedOff)
{
    return FMath::PerlinNoise2D(FVector2D(
        (X + SeedOff.X) * Config.BiomeBlend.ErosionFrequency + 100.f,
        (Y + SeedOff.Y) * Config.BiomeBlend.ErosionFrequency + 100.f))
        * 0.5f + 0.5f;
}

// ============================================================
//  GetBaseSurfaceDensity
//  Simple surface-layer density: positive below surface, negative above.
//  Does NOT include caves, skylands, or overhangs.
//  This is used by FVoxelDensityGenerator for the base terrain layer.
//  Returns (SurfaceHeight - Z) / SurfaceGradientScale as per analysis document.
// ============================================================
float FVoxelBiomeManager::GetBaseSurfaceDensity(float Z, float SurfaceHeight, const FVoxelGenerationConfig& Config)
{
    // 1. AIR COLUMN OPTIMIZATION
    // Early exit for air columns to avoid unnecessary density calculations.
    // This optimization improves performance by skipping density calculations
    // for voxels that are clearly above the surface.
    if (SurfaceHeight < Z - 1000.f) {
        return -1.0f; // Air column
    }

    // Calculate density based on distance from surface
    // Positive values indicate solid terrain (below surface)
    // Negative values indicate air (above surface)
    const float DistanceFromSurface = SurfaceHeight - Z;
    
    // Use the gradient scale from config for proper scaling
    const float GradientScale = Config.SurfaceGradientScale;
    
    return DistanceFromSurface / GradientScale;
}
