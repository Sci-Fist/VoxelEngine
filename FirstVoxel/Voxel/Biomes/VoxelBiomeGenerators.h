// VoxelBiomeGenerators.h
// Pure mathematical shape functions for each surface biome + skyland islands.
// All implementations live in VoxelBiomeGenerators.cpp â€” no inline bloat here
// except the small utility helpers used everywhere (FBM, FastNoise3D).
#pragma once

#include "CoreMinimal.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "VoxelBiome.h"

struct FIRSTVOXEL_API FVoxelBiomeGenerators
{
    // ----------------------------------------------------------------
    // NOISE UTILITIES
    // ----------------------------------------------------------------

    /** Single-octave 3D Perlin noise. Range approximately [-1, 1]. */
    static FORCEINLINE float FastNoise3D(float X, float Y, float Z)
    {
        return FMath::PerlinNoise3D(FVector(X, Y, Z));
    }

    /**
     * Fractional Brownian Motion â€” multi-octave layered noise.
     * Output range approximately [-1, 1] (exact range depends on octave count).
     * @param MaxOctaves  Hard cap from the performance config.
     */
    static float FBM(float X, float Y, float Z, int32 Octaves, float Lacunarity, float Gain, int32 MaxOctaves = 8);

    // ----------------------------------------------------------------
    // SURFACE BIOME HEIGHT FUNCTIONS
    // Each function returns an absolute world-Z surface height in cm.
    // ----------------------------------------------------------------

    /** Rolling plains and gentle forested hills. */
    static float GetForestHeight(float X, float Y, const FVoxelGenerationConfig& Config);

    /** Rolling sand dunes and desert ripples. */
    static float GetDesertHeight(float X, float Y, const FVoxelGenerationConfig& Config);

    /** Dramatic alpine peaks and high mountain ranges. */
    static float GetPeaksHeight(float X, float Y, const FVoxelGenerationConfig& Config);

    /** Ridged, terraced cliff formations and dry canyon walls. */
    static float GetCliffsHeight(float X, float Y, const FVoxelGenerationConfig& Config);

    /** Flat-top mesa plateaus with sharp vertical edges. */
    static float GetMesaHeight(float X, float Y, const FVoxelGenerationConfig& Config);

    /** Impact crater depressions with raised rims. */
    static float GetCraterHeight(float X, float Y, const FVoxelGenerationConfig& Config);

    // ----------------------------------------------------------------
    // SKYLANDS LAYER
    // Returns density at (X, Y, Z) for floating island shapes.
    // Altitude, size, and probability are all driven by the terrain
    // directly below (SurfaceHeight) and the local roughness (Weights).
    //
    //   > 0  =  island solid
    //   < 0  =  sky air (far from any island)
    //  -10   =  fast-out (Z completely outside the island band)
    // ----------------------------------------------------------------
    static float GetSkylandDensity(
        float X, float Y, float Z,
        float SurfaceHeight,
        const FVoxelBiomeWeightMap& Weights,
        const FVoxelGenerationConfig& Config);

    // ----------------------------------------------------------------
    // CAVE LAYER â€” CRYSTAL CAVERNS
    // Returns a density DELTA (negative = carving, positive = crystal fill).
    // ----------------------------------------------------------------
    static float GetCrystalCavernDelta(
        float X, float Y, float Z,
        float SurfaceHeight,
        const FVoxelGenerationConfig& Config);
};
