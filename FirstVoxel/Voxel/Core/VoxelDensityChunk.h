#pragma once

#include "CoreMinimal.h"

/**
 * DensityChunk (Data Layer Only)
 * Stores scalar density values for smooth Surface Nets interpolation.
 */
struct FVoxelDensityChunk
{
	/** Dense scalar density array, size = (GridSize + 3)^3 to include central gradients padding. */
	TArray<float> Densities;

	/** Dirty flag indicating if mesh rebuild is queued/required. */
	bool bIsDirty = true;

	void Init(int32 GridSize)
	{
		const int32 S = GridSize + 3;
		Densities.Empty(S * S * S);
		Densities.AddZeroed(S * S * S);
		bIsDirty = true;
	}
};
