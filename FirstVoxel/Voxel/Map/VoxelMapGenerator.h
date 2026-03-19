// VoxelMapGenerator.h  [canonical location: Voxel/Map/]
// CPU-based top-down world map generator for voxel terrain visualization.
//
// Thread-safe implementation that produces a flat BGRA pixel buffer suitable
// for UTexture2D upload on the game thread. Generates topographic maps with
// biome-based coloring, elevation visualization, and debugging overlays.
//
// @thread-safety Thread-safe. All public methods can be called from background
//                threads without external synchronization.
// @performance   O(N²) for N×N pixel resolution using ParallelFor processing.
//                Optimized for large map generation with minimal memory overhead.
//
// MAP COORDINATE SYSTEM:
// Map pixel (px, py) maps to world coordinate:
//   WorldX = CenterX + (px - Resolution*0.5) * PixelWorldSize
//   WorldY = CenterY + (py - Resolution*0.5) * PixelWorldSize
//
// COLOR GENERATION PIPELINE:
//   1. Biome blending: Mix biome colors by weight for base terrain color
//   2. Height shading: Apply brightness based on elevation (higher = brighter)
//   3. Water depth: Blend to deep blue for areas below sea level
//   4. Topographic contours: Add elevation lines every 2000 world units
//   5. Debug overlays: Highlight loaded chunks and chunk grid boundaries
//   6. Player indicator: Red dot at map center for player position
//
// PERFORMANCE CHARACTERISTICS:
// - Parallel processing using ParallelFor for optimal multi-core utilization
// - Static sampling functions avoid object instantiation overhead
// - Minimal memory allocation (single pixel buffer)
// - Optimized color calculations with precomputed palettes
// - Resolution-independent processing suitable for large maps
//
// INTEGRATION NOTES:
// - Designed for use with VoxelMapWidget UI component
// - Output format compatible with UTexture2D::UpdateTextureRegions
// - Supports dynamic resolution scaling for performance/quality balance
// - Includes debugging features for chunk loading visualization
#pragma once

#include "CoreMinimal.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "Voxel/Biomes/VoxelBiome.h"

struct FIRSTVOXEL_API FVoxelMapGenerator
{
    /**
     * Fills OutPixels with Resolution×Resolution BGRA colors.
     *
     * @param CenterX/Y        World XY the map is centered on
     * @param WorldRadius      Half-extent of the map in world cm
     * @param Resolution       Pixel dimensions (square, power-of-2 recommended)
     * @param Config           Generation config for world sampling
     * @param LoadedChunkCoords  Currently loaded chunk grid coordinates
     * @param ChunkWorldSize   One chunk side in world cm (ChunkSize × VoxelSize)
     * @param OutPixels        Output — sized to Resolution×Resolution on return
     */
    static void GeneratePixelBuffer(
        float                         CenterX,
        float                         CenterY,
        float                         WorldRadius,
        int32                         Resolution,
        const FVoxelGenerationConfig& Config,
        const TSet<FIntVector>&       LoadedChunkCoords,
        float                         ChunkWorldSize,
        TArray<FColor>&               OutPixels);

    /** Biome colour palette. Index matches EVoxelBiome cast to uint8. */
    static const FLinearColor BiomeColors[FVoxelBiomeWeightMap::MaxBiomes];

    /** Human-readable biome display names matching EVoxelBiome. */
    static const TCHAR* BiomeNames[FVoxelBiomeWeightMap::MaxBiomes];
};
