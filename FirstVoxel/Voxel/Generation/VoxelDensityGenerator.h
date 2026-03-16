// VoxelDensityGenerator.h
// The main entry point for per-voxel density calculation.
// Implements IVoxelDensityProvider so it can be used directly in FVoxelGeneratorTask.
//
// THREE-LAYER ARCHITECTURE:
//
//   SURFACE LAYER   (around Z â‰ˆ SurfaceHeight)
//     Blended height-field terrain driven by biome weights.
//     Optional overhangs near cliff faces.
//
//   SKYLANDS LAYER  (Z >> SurfaceHeight)
//     Floating islands whose altitude, size, and probability all
//     scale with the terrain height and roughness directly below.
//
//   CAVE LAYER      (Z << SurfaceHeight)
//     Universal worm tunnels + deep crystal cavern chambers.
//     Carved out of solid density only where D > 0.
//     Protected by a hard bedrock floor.
//
// The three layers are composed additively / by zone:
//   â€¢ If Z is in the sky zone â†’ skyland density replaces surface density
//   â€¢ Surface carving (overhangs, caves) only applies in the surface zone
//   â€¢ Bedrock is always forced solid regardless of other layers
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

    // Convenience overload pre-supplying biome weights + surface height (avoids recomputation).
    // Declared virtual + override so the compiler enforces the IVoxelDensityProvider contract.
    virtual float GetDensityFull(const FVector& WorldPos,
                                 const FVoxelBiomeWeightMap& Weights,
                                 float SurfaceHeight,
                                 const FVoxelGenerationConfig& Config,
                                 int32 StepSize = 1) override;

    // ---- World anchor constants ----
    // TerrainMidZ: Z=0 is sea level and the world origin for chunk coordinate math.
    static constexpr float TerrainMidZ = 0.f;

private:
    /**
     * Evaluates the two-tunnel worm noise at WorldPos.
     * SeedOff must be pre-computed by the caller via Config.GetSeedOffset() --
     * this avoids the redundant LCG hash that the old internal call caused
     * for every below-surface solid voxel.
     */
    static float SampleCaveNoise(
        const FVector&              WorldPos,
        const FVector&              SeedOff,
        const FVoxelGenerationConfig& Config);
};
