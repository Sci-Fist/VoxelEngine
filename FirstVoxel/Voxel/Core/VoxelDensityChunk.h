#pragma once

#include "CoreMinimal.h"

/**
 * FVoxelDensityChunk
 *
 * Data Layer component that stores scalar density values for smooth Surface
 * Nets interpolation.
 *
 * This structure represents a single chunk's density field data, which is used
 * by the mesh generation system to create smooth terrain surfaces. The density
 * values represent a signed distance field where positive values indicate solid
 * material and negative values indicate air space.
 *
 * MEMORY LAYOUT:
 * - Densities array uses a 3D grid layout with padding for gradient
 * calculations
 * - Grid size is extended by 3 units on each side (GridSize + 3) to provide
 * boundary padding
 * - Total array size is (GridSize + 3)^3 elements
 * - Padding allows for central difference calculations at chunk boundaries
 *
 * DENSITY VALUES:
 * - Positive values (> 0.0): Solid terrain material
 * - Negative values (< 0.0): Air space
 * - Zero crossing: Surface boundary (isosurface)
 * - Magnitude: Distance from surface (approximate)
 *
 * THREAD SAFETY:
 * - This structure is not thread-safe for concurrent modification
 * - Multiple readers can safely access the data simultaneously
 * - Write operations should be synchronized by the managing system
 *
 * PERFORMANCE CHARACTERISTICS:
 * - Memory usage: ~4 bytes per voxel (float) + container overhead
 * - Initialization: O(n^3) where n is GridSize
 * - Access pattern: Cache-friendly sequential access recommended
 */
struct FVoxelDensityChunk {
  /** Dense scalar density array, size = (GridSize + 3)^3 to include central
   * gradients padding. */
  TArray<float> Densities;

  /** Dirty flag indicating if mesh rebuild is queued/required. */
  bool bIsDirty = true;

  /**
   * Initializes the density chunk with the specified grid size.
   *
   * This method allocates and initializes the internal density array with the
   * appropriate size including padding for gradient calculations. The array
   * is filled with zeros, representing an empty/air state.
   *
   * @param GridSize  The number of voxels per side in the core grid area
   *                  (excluding padding). Typical values are 16, 32, or 64.
   *
   * @note Memory: Allocates (GridSize + 3)^3 float elements
   * @note Performance: O(n^3) initialization time where n = GridSize
   * @note State: Sets bIsDirty to true, indicating mesh rebuild needed
   * @note Thread Safety: Not thread-safe - should be called from single thread
   */
  void Init(int32 GridSize) {
    const int32 S = GridSize + 3;
    Densities.Empty(S * S * S);
    Densities.AddZeroed(S * S * S);
    bIsDirty = true;
  }
};
