// VoxelBiomeManager.h
// FIX N10 — Added ComputeSkylandSpawnProbability() which applies the J-curve
//            probability fields (ProbMidDipCenter/Width/Depth, ProbHighAltThreshold).
//            Called from VoxelBiomeGenerators::GetSkylandColumnCache() replacing the
//            old inline `if (HP > Prob) continue` that ignored the J-curve config.

#pragma once

#include "CoreMinimal.h"
#include "Voxel/Biomes/VoxelBiome.h"
#include "Voxel/Config/VoxelGenerationConfig.h"

class FIRSTVOXEL_API FVoxelBiomeManager
{
public:
    FVoxelBiomeManager() = default;

    static FVoxelBiomeWeightMap GetBiomeWeightsStatic(
        float X, float Y, const FVoxelGenerationConfig& Config);

    static float GetSurfaceHeightStatic(
        float X, float Y,
        const FVoxelBiomeWeightMap& Weights,
        const FVoxelGenerationConfig& Config);

    struct FWeightsAndHeight { FVoxelBiomeWeightMap Weights; float SurfaceHeight = 0.f; };
    static FWeightsAndHeight GetWeightsAndSurfaceHeightStatic(
        float X, float Y, const FVoxelGenerationConfig& Config);

    static float GetBaseSurfaceDensity(
        float Z, float SurfaceHeight, const FVoxelGenerationConfig& Config);

    /**
     * FIX N10: Compute skyland spawn probability at (HeightNorm, RoughnessNorm)
     * applying the full J-curve from FSkylandsLayerConfig:
     *   - Gaussian suppression at mid-altitude (ProbMidDipCenter)
     *   - Smooth surge above ProbHighAltitudeThreshold
     * Returns a clamped probability in [0, 0.98].
     */
    static float ComputeSkylandSpawnProbability(
        float HeightNorm, float RoughnessNorm, const FSkylandsLayerConfig& SC);

private:
    static float GetTemperatureWithSeed(float X, float Y,
        const FVoxelGenerationConfig& Config, const FVector& SeedOff);
    static float GetErosionWithSeed(float X, float Y,
        const FVoxelGenerationConfig& Config, const FVector& SeedOff);
};
