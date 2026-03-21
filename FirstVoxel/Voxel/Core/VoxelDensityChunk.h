// =============================================================================
// VoxelDensityChunk.h
// FIX N10/N14 — Removed bIsDirty field.
//               It was set in Init() but never read anywhere in the codebase.
//               Dead state fields cause confusion about what drives mesh rebuild
//               (answer: AVoxelChunk::bMeshDirty + DirtyRebuildQueue in AVoxelWorld).
// =============================================================================
#pragma once

#include "CoreMinimal.h"

/**
 * FVoxelDensityChunk — dense float buffer for one chunk's density field.
 *
 * Owned by AVoxelChunk::DenseChunk (TSharedPtr) and shared with
 * FVoxelGeneratorTask for background generation.
 *
 * Array layout: (GridSize + 3)^3 elements, indexed X + Y*S + Z*S*S
 * where S = GridSize + 3. The +3 provides one-voxel padding on each
 * face for Surface Nets gradient computation at chunk boundaries.
 *
 * Thread safety: written on background thread during generation,
 * read on game thread in ApplyMesh(). TSharedPtr refcounting ensures
 * the buffer stays alive until both sides are done.
 */
struct FVoxelDensityChunk
{
    /** Density field. Positive = solid, negative = air, zero crossing = surface. */
    TArray<float> Densities;

    /**
     * Allocate and zero the density array for a chunk of GridSize voxels/side.
     * Total size = (GridSize + 3)^3 floats.
     */
    void Init(int32 GridSize)
    {
        const int32 S = GridSize + 3;
        Densities.Empty(S * S * S);
        Densities.AddZeroed(S * S * S);
    }
};
