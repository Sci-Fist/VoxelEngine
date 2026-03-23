// VoxelBiomeManager.h
//
// BUG-CRUSH SESSION — all flaw analysis + fixes:
//
// ROOT BUG: GetSurfaceHeightStatic() ignored its weight parameter.
//   The function recomputed weights internally from temp/erosion, then
//   ALWAYS called GetCraterHeight() at the end regardless of passed weights.
//   Every caller that zeroed crater weights to get "neutral" terrain was
//   silently receiving the crater-modified value. This had cascading effects:
//
// BUG A (Skyland/Crater collision) — GetSkylandColumnCache used actual SH
//   (with crater rim at +72900 cm) for ColHN/GridSize. At rim columns
//   ColHN=1.0→GridSize=16000; adjacent normal columns ColHN=0.4→GridSize~5000.
//   Same physical island cell detected or missed depending on column → tears.
//   AND island SkyAlt was computed from "neutral" CH which was actually
//   crater-modified → islands inside crater spawned at floor altitude (251 cm)
//   where surrounding terrain is solid → FMath::Max absorbed island into terrain.
//
// BUG B (SkyLB false early-out) — FVoxelSkylandPass::PrepareColumn used
//   actual SurfaceHeight for SkyLB. At rim (SH=83000): SkyLB=91400 > MaxWorldZ
//   → bHasSkyland=false. Islands killed for all rim-adjacent chunks.
//
// BUG C (Rim multiply-stack) — ComputeCentralCraterHeight had three chained
//   multipliers stacking to 72900 cm:
//     MinRimH = abs(18000) * 1.5 = 27000  overrides CraterRimHeight=20000
//     BaseRimH = 27000 * 1.5 = 40500
//     Meteor: * 1.2 = 48600
//     RimPeak = BasePlains + 48600 * 1.5 = BasePlains + 72900 cm
//   Result: 720m tall crater rim = giant spires in the viewport.
//
// BUG D (Spawn underwater) — With crater floor at BasePlains-18000 < SeaLevel=0,
//   player spawned underground/underwater (TargetZ = -7751 + 200 = -7551 cm).
//
// FIXES:
//   - Added GetNeutralSurfaceHeightStatic() — computes base terrain WITHOUT
//     the GetCraterHeight overlay. This is the true neutral/pre-crater height.
//   - All skyland callers now use neutral height for altitude + SkyLB.
//   - Island SkyAlt floored by Max(NeutralAlt, ActualTerrainMax + MinAlt).
//   - Crater rim multiplier chain simplified (see VoxelBiomeGenerators_Craters.cpp).
//   - Spawn clamped above SeaLevel + 200 (see VoxelWorldGeneration.cpp).

#pragma once

#include "CoreMinimal.h"
#include "Voxel/Biomes/VoxelBiome.h"
#include "Voxel/Config/VoxelGenerationConfig.h"

class FIRSTVOXEL_API FVoxelBiomeManager
{
public:
    FVoxelBiomeManager() = default;

    static FVoxelBiomeWeightMap GetBiomeWeightsStatic(
        float X, float Y, const FVoxelGenerationConfig& Config,
        float* OutTemp = nullptr, float* OutErosion = nullptr);

    /** Returns surface height INCLUDING crater overlay. */
    static float GetSurfaceHeightStatic(
        float X, float Y,
        const FVoxelBiomeWeightMap& Weights,
        const FVoxelGenerationConfig& Config,
        float InTemp = -999.f, float InErosion = -999.f);

    /**
     * FIX ROOT BUG: Returns BASE terrain height WITHOUT crater overlay.
     * Use this wherever "neutral" terrain is needed:
     *   - Skyland altitude computation (so islands don't track crater rim height)
     *   - Cave pass neutral height (for cavern depth reference)
     *   - Skyland SkyLB threshold (so rim doesn't suppress nearby islands)
     */
    static float GetNeutralSurfaceHeightStatic(
        float X, float Y, const FVoxelGenerationConfig& Config,
        float InTemp = -999.f, float InErosion = -999.f);

    struct FWeightsAndHeight { FVoxelBiomeWeightMap Weights; float SurfaceHeight = 0.f; float NeutralHeight = 0.f; };
    static FWeightsAndHeight GetWeightsAndSurfaceHeightStatic(
        float X, float Y, const FVoxelGenerationConfig& Config);

    static float GetBaseSurfaceDensity(
        float Z, float SurfaceHeight, const FVoxelGenerationConfig& Config);

    static float ComputeSkylandSpawnProbability(
        float HeightNorm, float RoughnessNorm, const FSkylandsLayerConfig& SC);

private:
    static float GetTemperatureWithSeed(float X, float Y,
        const FVoxelGenerationConfig& Config, const FVector& SeedOff);
    static float GetErosionWithSeed(float X, float Y,
        const FVoxelGenerationConfig& Config, const FVector& SeedOff);
};
