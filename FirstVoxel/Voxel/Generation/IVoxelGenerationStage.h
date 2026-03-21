// =============================================================================
// IVoxelGenerationStage.h
//
// FIX #13 — FColumnContext::Blackboard (TMap<FName,float>) removed.
//           Was constructed/destructed for every XY column but never used.
//
// FIX #14 (COMPLETE) — Added FVector CachedSeedOffset to FColumnContext.
//           PrepareColumn now caches Config.GetSeedOffset() once per column.
//           EvaluateVoxel reads it from Context instead of calling GetSeedOffset()
//           per voxel (saves 3 LCG multiplications × N³ voxels per chunk).
// =============================================================================
#pragma once

#include "CoreMinimal.h"
#include "Voxel/Biomes/VoxelBiome.h"
#include "Voxel/Biomes/VoxelBiomeGenerators.h"

// ---------------------------------------------------------------------------
// FColumnContext — per-column payload shared between PrepareColumn / EvaluateVoxel
// ---------------------------------------------------------------------------
struct FColumnContext
{
    float SurfaceHeight        = 0.f;
    float NeutralSurfaceHeight = 0.f;   // Craters weight zeroed out (for caverns)
    float BedrockHeight        = -20000.f;
    float MaxWorldZ            = 0.f;

    FVoxelBiomeWeightMap BiomeWeights;
    FSkylandColumnCache  SkylandCache;

    // FIX #14: SeedOffset cached once per PrepareColumn call.
    // Previously EvaluateVoxel called Config.GetSeedOffset() per voxel,
    // which runs 3 LCG multiplications each time.
    // PrepareColumn (called once per XY column) caches the result here;
    // all EvaluateVoxel calls for that column reuse it at zero extra cost.
    FVector CachedSeedOffset = FVector::ZeroVector;
};

// ---------------------------------------------------------------------------
// IVoxelGenerationStage — modular generation pipeline node
// ---------------------------------------------------------------------------
class IVoxelGenerationStage
{
public:
    virtual ~IVoxelGenerationStage() = default;

    /** O(N²) — called once per XY column. Populate OutContext including CachedSeedOffset. */
    virtual void PrepareColumn(float WorldX, float WorldY,
                               const struct FVoxelGenerationConfig& Config,
                               FColumnContext& OutContext) const = 0;

    /** O(N³) — called per voxel. Read SeedOffset from Context.CachedSeedOffset. */
    virtual float EvaluateVoxel(const FVector& WorldPos,
                                const FColumnContext& Context,
                                const struct FVoxelGenerationConfig& Config,
                                float CurrentDensity) const = 0;
};
