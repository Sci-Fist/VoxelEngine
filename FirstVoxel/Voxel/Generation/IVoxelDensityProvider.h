// =============================================================================
// IVoxelDensityProvider.h
// =============================================================================
//
// Abstract interface that decouples FVoxelGeneratorTask from any specific
// density implementation. The only production implementation is
// FVoxelDensityGenerator (Generation/VoxelDensityGenerator.h), but tests or
// editor tools can supply custom implementations without recompiling the task.
//
// ── CALLING CONTRACT ─────────────────────────────────────────────────────────
//  All methods are called from a background thread inside
//  FVoxelGeneratorTask::BuildDensityField(). Implementations MUST be
//  thread-safe (stateless or internally synchronized).
//
// ── DENSITY CONVENTION ───────────────────────────────────────────────────────
//  Positive  → solid terrain  (stone, dirt, rock)
//  Zero      → surface isosurface
//  Negative  → air / empty space
//
// ── PERFORMANCE NOTES ────────────────────────────────────────────────────────
//  GetDensityFull() is the hot path — called O(n³) times per chunk.
//  GetBiomeWeights() and GetSurfaceHeight() are called O(n²) times (once per
//  XY column) and cached in ColumnWeights[] for the foliage pass.
//  Prefer GetDensityFull() over GetDensity() when biome weights and surface
//  height are already available to avoid redundant Perlin evaluations.
// =============================================================================

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
                                float NeutralSurfaceHeight,
                                const FVoxelGenerationConfig& Config,
                                int32 StepSize = 1,
                                const struct FSkylandColumnCache* SkylandCache = nullptr) = 0;
};