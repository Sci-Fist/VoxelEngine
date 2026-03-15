// VoxelMapGenerator.h
// CPU-based top-down world map sampler.
// Runs entirely on a background thread â€” no UObjects touched.
// Produces a flat BGRA pixel buffer that can be uploaded to a UTexture2D on the game thread.
//
// Map pixel (px, py) maps to world coordinate:
//   WorldX = CenterX + (px - Resolution*0.5) * PixelWorldSize
//   WorldY = CenterY + (py - Resolution*0.5) * PixelWorldSize     (Y increases downward in texture)
//
// Color logic (per pixel):
//   1. Blend biome colors by weight
//   2. Multiply by height-based brightness (darker = lower, brighter = higher)
//   3. Below SeaLevel â†’ fade toward deep blue
//   4. Loaded chunks slightly brighter ring at border
//   5. Player position = red dot (3Ã—3 px)
#pragma once

#include "CoreMinimal.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "Voxel/Biomes/VoxelBiome.h"

struct FIRSTVOXEL_API FVoxelMapGenerator
{
    /**
     * Fills OutPixels with ResolutionÃ—Resolution BGRA colors representing the world surface
     * viewed from directly above.
     *
     * @param CenterX/Y        World XY the map is centered on (usually player position)
     * @param WorldRadius      Half-extent of the map in world cm (e.g. 50000 = 500m radius)
     * @param Resolution       Pixel dimensions of the output (square). Power-of-2 recommended.
     * @param Config           Generation config used to sample the world
     * @param LoadedChunkCoords  Set of currently loaded chunk grid coordinates (for chunk outline)
     * @param ChunkWorldSize   Size of one chunk side in world cm (ChunkSize Ã— VoxelSize)
     * @param OutPixels        Output â€” sized to ResolutionÃ—Resolution on return
     *
     * Thread-safe: calls only static/const density functions with no shared mutable state.
     */
    static void GeneratePixelBuffer(
        float                      CenterX,
        float                      CenterY,
        float                      WorldRadius,
        int32                      Resolution,
        const FVoxelGenerationConfig& Config,
        const TSet<FIntVector>&    LoadedChunkCoords,
        float                      ChunkWorldSize,
        TArray<FColor>&            OutPixels);

    /**
     * Biome colour palette. Index matches EVoxelBiome cast to uint8.
     * Must have exactly MaxBiomes entries — a static_assert in the .cpp enforces this.
     */
    static const FLinearColor BiomeColors[FVoxelBiomeWeightMap::MaxBiomes];

    /**
     * Human-readable biome display names matching EVoxelBiome.
     * Must have exactly MaxBiomes entries.
     */
    static const TCHAR* BiomeNames[FVoxelBiomeWeightMap::MaxBiomes];
};
