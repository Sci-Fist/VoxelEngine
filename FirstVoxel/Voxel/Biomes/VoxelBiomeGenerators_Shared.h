// =============================================================================
// VoxelBiomeGenerators_Shared.h
// =============================================================================
// Internal shared noise utilities used across all VoxelBiomeGenerator sub-files.
// Include ONLY in the .cpp files — do not expose to external callers.
// External API lives in VoxelBiomeGenerators.h.
#pragma once
#include "CoreMinimal.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "VoxelBiome.h"
#include "VoxelBiomeGenerators.h"

// Convenience alias so split files can call FBM without the class prefix
FORCEINLINE float BG_FBM(float X, float Y, float Z, int32 Oct, float Lac, float Gain, int32 MaxOct)
{
    return FVoxelBiomeGenerators::FBM(X,Y,Z,Oct,Lac,Gain,MaxOct);
}
FORCEINLINE float BG_Noise(float X, float Y, float Z)
{
    return FVoxelBiomeGenerators::FastNoise3D(X,Y,Z);
}
