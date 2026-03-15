// VoxelBiomeManager.cpp
// Drives the 2D surface biome distribution using two orthogonal noise fields:
//   Temperature (X axis of biome space) — warm vs cold
//   Erosion     (Y axis of biome space) — flat vs rough
//
// Biome weights are always normalized to sum to 1.0. The resulting weight map
// is used to blend surface heights and for skyland roughness estimation.

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
    const float Temp    = GetTemperature(X, Y, Config); // [0, 1]
    const float Erosion = GetErosion    (X, Y, Config); // [0, 1]

    const FBiomeBlendConfig& B   = Config.BiomeBlend;
    // Compute seed offset once here for the crater noise below.
    // Temperature and Erosion compute it internally via their helper functions.
    const FVector Off = Config.GetSeedOffset();

    FVoxelBiomeWeightMap Map;

    // Forest: thrives in low-erosion (flat) areas with temperate temperatures.
    const float ForestW = FMath::Clamp(1.0f - Erosion * 0.40f, 0.f, 1.f)
                       * FMath::Clamp(1.5f - Temp, 0.f, 1.f); // taper down at high temp

    // Desert: thrives in low-erosion + hot temperature.
    const float DesertW = FMath::Clamp(1.0f - Erosion * 0.40f, 0.f, 1.f)
                       * FMath::Clamp((Temp - 0.70f) * 4.0f, 0.f, 1.f);

    // Peaks: trigger at high erosion (rarer) and steeper rise
    const float PeaksW = FMath::Clamp((Erosion - 0.68f) * B.PeaksStrength * 2.5f, 0.f, 1.f)
                       * FMath::Clamp(1.2f - Temp, 0.f, 1.f);

    // Cliffs: rough + warm. Ridged terrain in drier, warmer zones.
    const float CliffsW = FMath::Clamp((Erosion - 0.50f) * B.CliffsStrength * 1.6f, 0.f, 1.f)
                        * FMath::Clamp(Temp * 1.3f - 0.15f, 0.f, 1.f);

    // Mesa: hot + moderate erosion. The sharp temperature cutoff gives Mesa a distinctive zone.
    const float MesaW = FMath::Clamp((Temp - 0.62f) * 3.5f * B.MesaStrength, 0.f, 1.f)
                      * FMath::Clamp(1.f - FMath::Abs(Erosion - 0.4f) * 3.5f, 0.f, 1.f);

    // Craters: rare, driven by a separate low-frequency noise not related to Temp/Erosion.
    // Uses the Z=200 slice as a pseudo-2D crater placement field.
    const float CraterNoise = FMath::PerlinNoise3D(FVector(
        (X + Off.X) * Config.Craters.Frequency,
        (Y + Off.Y) * Config.Craters.Frequency,
        200.f));
    // Weight caps at 0.45 so other biomes (Forest, Desert…) remain active inside craters.
    // Without this cap, CratersW normalized to ~1.0 at the core, zeroing out all other
    // biome weights and making crater interiors a featureless flat plain.
    const float CratersW = FMath::SmoothStep(
        Config.Craters.ImpactThreshold + 0.1f,
        Config.Craters.ImpactThreshold,
        CraterNoise) * 0.45f;

    Map.SetWeight(EVoxelBiome::Forest,  ForestW);
    Map.SetWeight(EVoxelBiome::Peaks,   PeaksW);
    Map.SetWeight(EVoxelBiome::Cliffs,  CliffsW);
    Map.SetWeight(EVoxelBiome::Mesa,    MesaW);
    Map.SetWeight(EVoxelBiome::Craters, CratersW);
    Map.SetWeight(EVoxelBiome::Desert,  DesertW);

    Map.Normalize();
    return Map;
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

    const float wForest  = Weights.GetWeight(EVoxelBiome::Forest);
    const float wPeaks   = Weights.GetWeight(EVoxelBiome::Peaks);
    const float wCliffs  = Weights.GetWeight(EVoxelBiome::Cliffs);
    const float wMesa    = Weights.GetWeight(EVoxelBiome::Mesa);
    const float wCraters = Weights.GetWeight(EVoxelBiome::Craters);
    const float wDesert  = Weights.GetWeight(EVoxelBiome::Desert);

    if (wForest  > 0.f) Height += FVoxelBiomeGenerators::GetForestHeight (X, Y, Config) * wForest;
    if (wPeaks   > 0.f) Height += FVoxelBiomeGenerators::GetPeaksHeight  (X, Y, Config) * wPeaks;
    if (wCliffs  > 0.f) Height += FVoxelBiomeGenerators::GetCliffsHeight (X, Y, Config) * wCliffs;
    if (wMesa    > 0.f) Height += FVoxelBiomeGenerators::GetMesaHeight   (X, Y, Config) * wMesa;
    if (wCraters > 0.f) Height += FVoxelBiomeGenerators::GetCraterHeight (X, Y, Config) * wCraters;
    if (wDesert  > 0.f) Height += FVoxelBiomeGenerators::GetDesertHeight (X, Y, Config) * wDesert;

    return Height;
}

// ============================================================
//  GetBaseSurfaceDensity
//  Simple signed-distance ramp relative to the surface.
//  +1 at surface, decreasing above and increasing below.
// ============================================================
float FVoxelBiomeManager::GetBaseSurfaceDensity(float Z, float SurfaceHeight, const FVoxelGenerationConfig& Config)
{
    return (SurfaceHeight - Z) / FMath::Max(1.f, Config.SurfaceGradientScale);
}

// ============================================================
//  PRIVATE — noise fields
// ============================================================
// GetSeedOffset() is deliberately not called here — the caller already computed it.
// These helpers accept a pre-computed offset so the LCG hash runs only once per
// GetBiomeWeightsStatic call instead of three times.
float FVoxelBiomeManager::GetTemperature(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FVector Off = Config.GetSeedOffset();
    // Remap Perlin [-1, 1] to Temperature [0, 1].
    return FMath::PerlinNoise2D(FVector2D(
        (X + Off.X) * Config.BiomeBlend.TemperatureFrequency,
        (Y + Off.Y) * Config.BiomeBlend.TemperatureFrequency))
        * 0.5f + 0.5f;
}

float FVoxelBiomeManager::GetErosion(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FVector Off = Config.GetSeedOffset();
    // +100 offset separates Erosion from Temperature in noise space so they are
    // visually independent even when both frequencies are equal.
    return FMath::PerlinNoise2D(FVector2D(
        (X + Off.X) * Config.BiomeBlend.ErosionFrequency + 100.f,
        (Y + Off.Y) * Config.BiomeBlend.ErosionFrequency + 100.f))
        * 0.5f + 0.5f;
}
