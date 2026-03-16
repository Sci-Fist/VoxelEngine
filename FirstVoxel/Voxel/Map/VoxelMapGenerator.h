// VoxelMapGenerator.h  [canonical location: Voxel/Map/]
// CPU-based top-down world map sampler. Thread-safe — no UObjects touched.
// Produces a flat BGRA pixel buffer uploadable to UTexture2D on the game thread.
//
// Map pixel (px, py) maps to world coordinate:
//   WorldX = CenterX + (px - Resolution*0.5) * PixelWorldSize
//   WorldY = CenterY + (py - Resolution*0.5) * PixelWorldSize
//
// Color logic per pixel:
//   1. Blend biome colors by weight
//   2. Multiply by height-based brightness
//   3. Below SeaLevel → fade to deep blue
//   4. Contour lines every 2000 world units (topographic style)
//   5. Loaded chunks get a subtle brightness ring
//   6. Player dot at center (red 3×3 px)
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
