// =============================================================================
// VoxelBiomeGenerators.h
// =============================================================================
//
// Pure-math shape functions for every surface biome + floating island system.
// All implementations are in VoxelBiomeGenerators.cpp. Only FBM() and
// FastNoise3D() are inlined here because they appear in every hot path.
//
// -- SURFACE HEIGHT FUNCTIONS -------------------------------------------------
//
//  Each Get*Height() function returns an absolute world-Z surface height (cm).
//  All heights are relative to Config.SeaLevel, not absolute world origin.
//  They are called once per XY column and blended by FVoxelBiomeManager.
//
// -- SKYLANDS SYSTEM ----------------------------------------------------------
//
//  GetSkylandColumnCache()     Build once per XY column; samples 9 neighbouring
//                              cellular grid cells and blends their properties.
//                              Expensive (9x GetBiomeWeightsStatic calls), so
//                              results are stored in FSkylandColumnCache and
//                              reused for every Z in the column.
//
//  GetSkylandDensityFromCache() Per-voxel evaluation using the prebuilt cache.
//                              Fast -- only evaluates shape noise + falloff.
//                              Returns D > 0 inside solid island volume.
//
//  GetSkylandDensity()          Convenience wrapper that builds the cache on
//                              the fly. Use only when no column cache exists.
//
//  SHARD vs ISLAND (Cache.ShardT):
//    ShardT=0  sky-shard (boulder):  EffThickness=0.65, spherical falloff,
//                                    high Z-noise freq (0.50x), low breakup
//                                    (0.10), size scales with altitude gap.
//    ShardT=1  skyland (platform):   EffThickness=ThicknessRatio, flat-top
//                                    falloff (35% plateau), low Z-noise (0.05x),
//                                    high breakup (up to 2.80 x HeightNorm).
//    All per-voxel properties are linearly blended by Cache.ShardT so the
//    transition from shard field to skyland is smooth and continuous.
//
//  WINDING / CLEARANCE NOTES:
//    The 200 cm clearance buffers (HalfThick clamp and post-selection SkyAlt
//    raise) are intentionally commented out.  They were double-offsetting
//    island altitude on top of MinAltitudeAboveTerrain and clamping HalfThick
//    too aggressively.  The HeightCutoff fade in GetSkylandDensityFromCache
//    (SkyD → -2 below SurfaceHeight + MinAltitudeAboveTerrain) is the
//    canonical terrain-intersection guard.
//
// -- CRYSTAL CAVERN DELTA -----------------------------------------------------
//
//  GetCrystalCavernDelta() returns a NEGATIVE density delta (carve) inside
//  deep chamber regions. Only applied where D > 0 (solid terrain). Crystals
//  are intentionally disabled (no positive delta) to prevent solid pillars
//  from re-sealing carved spaces.
//
// -- FBM NOTES ----------------------------------------------------------------
//
//  FBM(X,Y,Z, Octaves, Lacunarity, Gain, MaxOctaves)
//    MaxOctaves is capped by Performance.MaxNoiseOctaves from the world config.
//    Lacunarity=2.0, Gain=0.5 are the standard "pink noise" parameters.
//    Deviating from these changes the spectral balance visibly.
// =============================================================================
#pragma once

#include "CoreMinimal.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "VoxelBiome.h"

struct FSkylandIslandData
{
    float SkyAlt = 0.f;
    float HalfThick = 0.f;
    float Threshold = 0.f;
    float ShardT = 0.f;
    float Freq = 0.f;
    float HeightNorm = 0.f;
    float ShardFalloff = 0.f;
    float IslandSize = 0.f;
};

struct FSkylandColumnCache
{
    bool bHasSkyland = false;
    TArray<FSkylandIslandData, TInlineAllocator<4>> Islands;

    // Cache dimensions (global to column)
    float WX_base = 0.f;
    float WY_base = 0.f;
};

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
    static float GetCraterHeight(float X, float Y, const FVoxelGenerationConfig& Config, float BaseHeight);

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
        const FVoxelGenerationConfig& Config,
        int32 StepSize = 1);

    static FSkylandColumnCache GetSkylandColumnCache(
        float X, float Y, float SurfaceHeight,
        const FVoxelBiomeWeightMap& Weights,
        const FVoxelGenerationConfig& Config);

    static float GetSkylandDensityFromCache(
        const FSkylandColumnCache& Cache, float X, float Y, float Z,
        const FVoxelGenerationConfig& Config, int32 StepSize = 1);

    // ----------------------------------------------------------------
    // CAVE LAYER â€” CRYSTAL CAVERNS
    // Returns a density DELTA (negative = carving, positive = crystal fill).
    // ----------------------------------------------------------------
    static float GetCrystalCavernDelta(
        float X, float Y, float Z,
        float SurfaceHeight,
        const FVoxelGenerationConfig& Config);
};
