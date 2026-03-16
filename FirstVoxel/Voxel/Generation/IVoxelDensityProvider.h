// IVoxelDensityProvider.h
// Abstract interface for voxel density evaluation.
// Implementations can be used directly in FVoxelGeneratorTask
// to drive mesh generation and foliage placement.

#pragma once

#include "CoreMinimal.h"
#include "Voxel/Biomes/VoxelBiome.h"
#include "Voxel/Config/VoxelGenerationConfig.h"

class FIRSTVOXEL_API IVoxelDensityProvider
{
public:
    virtual ~IVoxelDensityProvider() = default;

    // Evaluate density at a world position.
    // Positive values are solid, negative are air.
    virtual float GetDensity(float X, float Y, float Z, const FVoxelGenerationConfig& Config) = 0;

    // Compute the blended terrain surface height at (X, Y).
    // This is the height of the surface layer before carving.
    virtual float GetSurfaceHeight(float X, float Y, const FVoxelGenerationConfig& Config) = 0;

    // Compute per-biome weights at (X, Y).
    // Used for material blending and skyland roughness estimation.
    virtual FVoxelBiomeWeightMap GetBiomeWeights(float X, float Y, const FVoxelGenerationConfig& Config) = 0;

    // Convenience overload pre-supplying biome weights + surface height (avoids recomputation)
    virtual float GetDensityFull(const FVector& WorldPos,
                                const FVoxelBiomeWeightMap& Weights,
                                float SurfaceHeight,
                                const FVoxelGenerationConfig& Config,
                                int32 StepSize = 1,
                                const struct FSkylandColumnCache* SkylandCache = nullptr) = 0;
};