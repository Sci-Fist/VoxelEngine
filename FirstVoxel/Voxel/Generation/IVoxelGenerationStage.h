// =============================================================================
// IVoxelGenerationStage.h
//
// FIX #13 — Removed FColumnContext::Blackboard (TMap<FName,float>).
//           It was constructed and destructed for every XY column
//           (~1225/chunk) but nothing in the codebase ever wrote to or
//           read from it.  Removing it saves ~128B heap overhead per column.
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
    // Blackboard removed (#13) — was never used, added ~128B heap per column
};

// ---------------------------------------------------------------------------
// IVoxelGenerationStage — modular generation pipeline node
// ---------------------------------------------------------------------------
class IVoxelGenerationStage
{
public:
    virtual ~IVoxelGenerationStage() = default;

    /** O(N²) — called once per XY column. Populate OutContext. */
    virtual void PrepareColumn(float WorldX, float WorldY,
                               const struct FVoxelGenerationConfig& Config,
                               FColumnContext& OutContext) const = 0;

    /** O(N³) — called per voxel. Returns updated density. */
    virtual float EvaluateVoxel(const FVector& WorldPos,
                                const FColumnContext& Context,
                                const struct FVoxelGenerationConfig& Config,
                                float CurrentDensity) const = 0;
};
