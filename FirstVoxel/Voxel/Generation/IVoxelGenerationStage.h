#pragma once

#include "CoreMinimal.h"
#include "Voxel/Biomes/VoxelBiome.h"
#include "Voxel/Biomes/VoxelBiomeGenerators.h" // For FSkylandColumnCache

/**
 * FColumnContext
 *
 * Payload containing shared data computed once per column (O(N²)) and passed
 * down into per-voxel evaluation loops (O(N³)).
 *
 * This structure serves as a communication channel between the column-level
 * preparation phase and the voxel-level evaluation phase, allowing expensive
 * calculations to be performed once per column rather than once per voxel.
 *
 * PERFORMANCE OPTIMIZATION:
 * - SurfaceHeight: Pre-computed terrain height for the column
 * - BiomeWeights: Blended biome weights for the column location
 * - SkylandCache: Pre-computed skyland neighbor data for efficient lookup
 * - Blackboard: Generic storage for custom pass communication
 *
 * MEMORY USAGE:
 * - Fixed-size members: ~32 bytes
 * - Dynamic containers: BiomeWeights (~24 bytes), SkylandCache (~variable)
 * - Blackboard: Optional, grows as needed for custom passes
 *
 * THREAD SAFETY:
 * - All members are read-only during voxel evaluation
 * - Safe to share across multiple voxel evaluation threads
 * - Blackboard modifications should be done during PrepareColumn only
 */
struct FColumnContext {
  /** Surface height at this column location (world Z coordinate). */
  float SurfaceHeight = 0.f;

  /** Surface height with Craters weight zeroed out (for cavern placement). */
  float NeutralSurfaceHeight = 0.f;

  /** Bedrock depth limit for this column. */
  float BedrockHeight = -20000.f;

  /** Maximum world Z coordinate in this column. */
  float MaxWorldZ = 0.f;

  /** Blended biome weights for this column location. */
  FVoxelBiomeWeightMap BiomeWeights;

  /** Pre-computed skyland neighbor cache for efficient lookup. */
  FSkylandColumnCache SkylandCache;

  /**
   * Generic blackboard storage for custom pass communication.
   *
   * Custom generation stages can use this to pass arbitrary float values
   * between PrepareColumn and EvaluateVoxel phases. This is useful for
   * caching expensive calculations or sharing state between different
   * generation passes.
   *
   * @note Thread Safety: Only modify during PrepareColumn phase
   * @note Performance: Use FName keys for efficient lookup
   * @note Memory: Grows dynamically as needed
   */
  TMap<FName, float> Blackboard;
};

/**
 * IVoxelGenerationStage
 *
 * Interface for decoupled processing nodes in the voxel generation pipeline.
 *
 * This interface defines the contract for modular generation stages that can be
 * chained together to build complex terrain generation systems. Each stage
 * operates at two levels:
 *
 * 1. Column Level (O(N²)): Pre-compute expensive data once per vertical column
 * 2. Voxel Level (O(N³)): Use pre-computed data to evaluate individual voxels
 *
 * DESIGN PRINCIPLES:
 * - Decoupling: Each stage is independent and can be combined in any order
 * - Performance: Expensive calculations are done once per column, not per voxel
 * - Flexibility: Custom stages can be added without modifying core pipeline
 * - Thread Safety: All methods must be thread-safe for concurrent execution
 *
 * IMPLEMENTATION GUIDELINES:
 * - Keep PrepareColumn lightweight - it runs O(N²) times
 * - Optimize EvaluateVoxel for speed - it runs O(N³) times
 * - Use FColumnContext for sharing expensive calculations
 * - Return deterministic results for consistent terrain generation
 * - Handle edge cases gracefully (out-of-bounds, invalid parameters)
 */
class IVoxelGenerationStage {
public:
  virtual ~IVoxelGenerationStage() = default;

  /**
   * PrepareColumn (O(N²))
   *
   * Pre-calculate heights and caches once per vertical column.
   *
   * This method is called once for each XY column in the generation area.
   * It should perform all expensive calculations that can be shared across
   * all Z voxels in that column. The results are stored in OutContext and
   * passed to EvaluateVoxel for each voxel in the column.
   *
   * @param WorldX        Column X coordinate in world space
   * @param WorldY        Column Y coordinate in world space
   * @param Config        Voxel generation configuration
   * @param OutContext    Output payload to share with voxel evaluations
   *
   * @note Performance: This method runs O(N²) times, keep it lightweight
   * @note Thread Safety: Must be thread-safe - can be called from multiple
   * threads
   * @note Determinism: Should produce identical results for same inputs
   * @note Memory: OutContext will be reused for all voxels in this column
   */
  virtual void PrepareColumn(float WorldX, float WorldY,
                             const struct FVoxelGenerationConfig &Config,
                             FColumnContext &OutContext) const = 0;

  /**
   * EvaluateVoxel (O(N³))
   *
   * Compute the density for a single grid voxel item.
   *
   * This method is called once for every voxel in the generation area.
   * It uses the pre-computed context from PrepareColumn to efficiently
   * calculate the density value for the specified world position.
   *
   * @param WorldPos      Voxel position in world space coordinates
   * @param Context       Per-column calculated context payload from
   * PrepareColumn
   * @param Config        Voxel generation configuration
   * @param CurrentDensity The density provided by previous stages in the chain
   * @return              The updated density value for this voxel
   *
   * @note Performance: This method runs O(N³) times, optimize for speed
   * @note Thread Safety: Must be thread-safe - can be called from multiple
   * threads
   * @note Determinism: Should produce identical results for same inputs
   * @note Density Range: Return values > 0 indicate solid, < 0 indicate air
   * @note Chain Processing: CurrentDensity is the result from previous stages
   */
  virtual float EvaluateVoxel(const FVector &WorldPos,
                              const FColumnContext &Context,
                              const struct FVoxelGenerationConfig &Config,
                              float CurrentDensity) const = 0;
};
