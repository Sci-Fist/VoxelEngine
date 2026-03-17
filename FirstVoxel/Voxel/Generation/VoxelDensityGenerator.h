// =============================================================================
// VoxelDensityGenerator.h
// =============================================================================
//
// Main entry point for per-voxel density evaluation.
// Implements IVoxelDensityProvider so it plugs directly into FVoxelGeneratorTask.
//
// -- THREE-LAYER COMPOSITION --------------------------------------------------
//
//  GetDensityFull() composes four layers in priority order:
//
//    1. SURFACE  (Z around SurfaceHeight)
//       Signed-distance ramp:  D = (SurfH - Z) / SurfaceGradientScale
//       + optional overhang noise on steep terrain.
//
//    2. CAVES    (Z << SurfaceHeight, solid only)
//       Worm tunnels carved by SampleCaveNoise().
//       Crystal cavern chambers from FVoxelBiomeGenerators.
//       Only modifies already-solid voxels (D > 0.05).
//
//    3. BEDROCK  (Z < BedrockDepth)
//       Forced to D = 2.0 -- always solid, immune to carving.
//
//    4. SKYLANDS (Z > SkyLowerBound)
//       GetSkylandDensity() returns D > 0 inside floating islands.
//       final = max(SkyD, SurfD) so islands always override air.
//
// -- KEY OPTIMISATIONS --------------------------------------------------------
//  - GetSeedOffset() is called ONCE per GetDensityFull() call and reused
//    by every sub-system, eliminating ~3 redundant LCG hashes per voxel.
//  - GetSkylandDensity() has an early-out when Z is clearly below the lowest
//    possible island band, saving the most expensive noise call for ground-
//    level chunks where no skylands exist.
//  - FSkylandColumnCache is built once per XY column and reused for all Z,
//    amortising the 9-cell neighbourhood query across the full column.
//  - SampleCaveNoise() accepts a pre-computed SeedOff to skip another LCG.
//
// -- THREAD SAFETY ------------------------------------------------------------
//  Fully stateless -- safe to call from multiple threads simultaneously.
//  The static FVoxelDensityGenerator fallback instance in GeneratorTask is
//  shared across threads; this is safe because the struct has no mutable
//  state between calls.
// =============================================================================
#pragma once

#include "CoreMinimal.h"
#include "Generation/IVoxelDensityProvider.h"
#include "Voxel/Biomes/VoxelBiome.h"
#include "Voxel/Config/VoxelGenerationConfig.h"

struct FIRSTVOXEL_API FVoxelDensityGenerator : public IVoxelDensityProvider
{
    // IVoxelDensityProvider
    virtual float              GetDensity    (float X, float Y, float Z, const FVoxelGenerationConfig& Config) override;
    virtual float              GetSurfaceHeight(float X, float Y, const FVoxelGenerationConfig& Config) override;
    virtual FVoxelBiomeWeightMap GetBiomeWeights(float X, float Y, const FVoxelGenerationConfig& Config) override;

    /**
     * Full 3-layer density evaluation with pre-supplied biome weights and surface height.
     * Call this overload (not GetDensity) whenever weights and SurfH are already known
     * for the column -- it avoids re-running the two O(n^2) Perlin calls per voxel.
     *
     * @param NeutralSurfaceHeight  Surface height computed with Craters weight zeroed out.
     *                              Used by crystal cavern placement to prevent chambers
     *                              from breaking into the crater floor.
     * @param SkylandCache          Per-column cache built by GetSkylandColumnCache().
     *                              Pass nullptr to compute on-the-fly (slower).
     */
    virtual float GetDensityFull(const FVector& WorldPos,
                                 const FVoxelBiomeWeightMap& Weights,
                                 float SurfaceHeight,
                                 float NeutralSurfaceHeight,
                                 const FVoxelGenerationConfig& Config,
                                 int32 StepSize = 1,
                                 const struct FSkylandColumnCache* SkylandCache = nullptr) override;

    // Z=0 is sea level and the world origin for chunk coordinate math.
    static constexpr float TerrainMidZ = 0.f;

private:
    /**
     * Two-tunnel worm noise evaluation.
     * Accepts a pre-computed SeedOff (Config.GetSeedOffset()) to avoid a redundant
     * LCG hash for every below-surface solid voxel.
     * Returns a carve strength in [0, CVC.Strength]; 0 = no carving.
     */
    static float SampleCaveNoise(
        const FVector&                WorldPos,
        const FVector&                SeedOff,
        const FVoxelGenerationConfig& Config);
};
