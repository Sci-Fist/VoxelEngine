// VoxelBiomeManager.h
// 
// Core biome distribution system for 2D surface terrain generation.
// 
// ARCHITECTURE OVERVIEW:
// This class manages the procedural distribution of surface biomes across the
// 2D world plane using orthogonal noise fields. It computes biome weights and
// blended terrain heights for the surface layer only.
// 
// DESIGN PHILOSOPHY:
// - Surface-only focus: Skylands and Crystal Caverns are handled separately
//   in FVoxelDensityGenerator to maintain clear layer separation
// - Noise-driven distribution: Uses temperature and erosion noise fields
//   for natural biome transitions
// - Weight-based blending: Biome weights sum to 1.0 for smooth transitions
// - Performance optimized: Caches seed offsets and avoids redundant calculations
// 
// BIOME SYSTEM:
// The system uses two primary noise dimensions to create natural biome
// distributions:
// - Temperature axis: Controls climate-based biomes (cold to hot)
// - Erosion axis: Controls terrain roughness (flat to rugged)
// 
// INTEGRATION:
// - Works with FVoxelDensityGenerator for complete terrain generation
// - Provides biome weights for material assignment and foliage placement
// - Supports per-biome configuration overrides and water settings
// - Integrates with spawn system for biome-specific player placement
#pragma once

#include "CoreMinimal.h"
#include "Voxel/Biomes/VoxelBiome.h"
#include "Voxel/Config/VoxelGenerationConfig.h"

// LogVoxelBiome is declared in VoxelLogger.h â€” do NOT re-declare here.
// Including it twice in the same TU causes "struct redefinition" C2011.

class FIRSTVOXEL_API FVoxelBiomeManager
{
public:
    FVoxelBiomeManager() = default;

    // ----------------------------------------------------------------
    // Surface biome weights at a 2D (X, Y) coordinate.
    // Z is irrelevant for surface biomes â€” use FVoxelDensityGenerator
    // for the full 3D density pipeline including Skylands and Caves.
    // ----------------------------------------------------------------
    static FVoxelBiomeWeightMap GetBiomeWeightsStatic(float X, float Y, const FVoxelGenerationConfig& Config);

    // Blended terrain surface height from all weighted surface biomes.
    static float GetSurfaceHeightStatic(float X, float Y, const FVoxelBiomeWeightMap& Weights, const FVoxelGenerationConfig& Config);

    // Single call for weights + surface height (avoids duplicate GetSeedOffset / weight computation).
    struct FWeightsAndHeight { FVoxelBiomeWeightMap Weights; float SurfaceHeight = 0.f; };
    static FWeightsAndHeight GetWeightsAndSurfaceHeightStatic(float X, float Y, const FVoxelGenerationConfig& Config);

    // Simple surface-layer density: positive below surface, negative above.
    // Does NOT include caves, skylands, or overhangs.
    static float GetBaseSurfaceDensity(float Z, float SurfaceHeight, const FVoxelGenerationConfig& Config);

private:
    // Two orthogonal 2D noise fields that drive biome distribution.
    // WithSeed variants avoid redundant GetSeedOffset() when caller already has it.
    static float GetTemperature(float X, float Y, const FVoxelGenerationConfig& Config);
    static float GetErosion    (float X, float Y, const FVoxelGenerationConfig& Config);
    static float GetTemperatureWithSeed(float X, float Y, const FVoxelGenerationConfig& Config, const FVector& SeedOff);
    static float GetErosionWithSeed    (float X, float Y, const FVoxelGenerationConfig& Config, const FVector& SeedOff);
};
