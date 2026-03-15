// VoxelBiomeDataAsset.h
// A UDataAsset that wraps FVoxelGenerationConfig, allowing designers to create
// multiple named biome presets in the Content Browser and swap them on any
// AVoxelWorld actor without touching C++ or recompiling.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Config/VoxelGenerationConfig.h"
#include "VoxelBiomeDataAsset.generated.h"

/**
 * A designer-friendly DataAsset that holds a complete world generation config.
 * Create instances via the Content Browser (Miscellaneous > Data Asset).
 * Assign to AVoxelWorld::BiomePreset to override the default inline config.
 */
UCLASS(BlueprintType)
class FIRSTVOXEL_API UVoxelBiomeDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** The full generation configuration stored in this preset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Generation", meta = (ShowOnlyInnerProperties))
	FVoxelGenerationConfig Config;
};
