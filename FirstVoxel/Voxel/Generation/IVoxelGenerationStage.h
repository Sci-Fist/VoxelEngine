#pragma once

#include "CoreMinimal.h"
#include "Voxel/Biomes/VoxelBiome.h"
#include "Voxel/Biomes/VoxelBiomeGenerators.h" // For FSkylandColumnCache

/**
 * FColumnContext
 * Payload containing shared data computed once per column (O(N²))
 * and passed down into per-voxel evaluation loops (O(N³)).
 */
struct FColumnContext
{
	float                SurfaceHeight = 0.f;
	float                NeutralSurfaceHeight = 0.f;
	float                BedrockHeight = -20000.f;
	float                MaxWorldZ = 0.f;

	FVoxelBiomeWeightMap BiomeWeights;
	FSkylandColumnCache  SkylandCache;

	// Generic blackboard storage for custom pass communication
	TMap<FName, float>   Blackboard;
};

/**
 * IVoxelGenerationStage
 * Interface for decoupled processing nodes in the voxel generation pipeline.
 */
class IVoxelGenerationStage
{
public:
	virtual ~IVoxelGenerationStage() = default;

	/**
	 * PrepareColumn (O(N²))
	 * Pre-calculate heights and caches once per vertical column.
	 * 
	 * @param WorldX        Column X coordinate (World Space)
	 * @param WorldY        Column Y coordinate (World Space)
	 * @param Config        Voxel configuration
	 * @param OutContext    Output payload to share with voxel evaluations
	 */
	virtual void PrepareColumn(
		float WorldX, float WorldY, 
		const struct FVoxelGenerationConfig& Config, 
		FColumnContext& OutContext) const = 0;

	/**
	 * EvaluateVoxel (O(N³))
	 * Compute the density for a single grid voxel item.
	 * 
	 * @param WorldPos      Voxel position (World Space)
	 * @param Context       Per-column calculated context payload
	 * @param Config        Voxel configuration
	 * @param CurrentDensity The density provided by previous stages in the chain
	 * @return              The updated density
	 */
	virtual float EvaluateVoxel(
		const FVector& WorldPos, 
		const FColumnContext& Context, 
		const struct FVoxelGenerationConfig& Config, 
		float CurrentDensity) const = 0;
};
