// =============================================================================
// VoxelBiomeManager.h
// =============================================================================
//
// Static utility class for 2D surface biome distribution.
// Evaluates temperature/erosion noise to produce FVoxelBiomeWeightMap values
// and blended surface heights. All methods are stateless and thread-safe.
//
// -- RESPONSIBILITY BOUNDARY --------------------------------------------------
//
//  FVoxelBiomeManager handles the SURFACE layer ONLY:
//    - Biome weight calculation from 2D noise
//    - Blended surface height from per-biome height functions
//    - Basic signed-distance surface density (GetBaseSurfaceDensity)
//
//  It does NOT handle:
//    - Skylands density  (FVoxelBiomeGenerators::GetSkylandDensity)
//    - Cave carving      (FVoxelDensityGenerator::SampleCaveNoise)
//    - 3D density composition (FVoxelDensityGenerator::GetDensityFull)
//
// -- CALL FREQUENCY -----------------------------------------------------------
//
//  GetBiomeWeightsStatic() is called O(n^2) times per chunk (once per XY
//  column) in FVoxelGeneratorTask::BuildDensityField. The results are cached
//  in ColumnWeights[] and reused for every Z in the column, and again for
//  the foliage pass. FVoxelBiomeGenerators::GetSkylandColumnCache() also
//  calls it for each of 9 neighbouring cells -- see notes there.
//
// -- NOISE STRUCTURE ----------------------------------------------------------
//
//  Two PerlinNoise2D calls per column (Temperature + Erosion), each seeded by
//  Config.GetSeedOffset() to ensure different worlds per seed. The WithSeed
//  variants accept a pre-computed offset to avoid calling GetSeedOffset()
//  twice per column.
// =============================================================================
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
    // Both accept a pre-computed SeedOff to avoid redundant GetSeedOffset() calls.
    static float GetTemperatureWithSeed(float X, float Y, const FVoxelGenerationConfig& Config, const FVector& SeedOff);
    static float GetErosionWithSeed    (float X, float Y, const FVoxelGenerationConfig& Config, const FVector& SeedOff);
};
