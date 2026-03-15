// VoxelMapGenerator.cpp
#include "VoxelMapGenerator.h"
#include "Biomes/VoxelBiomeManager.h"
#include "Biomes/VoxelBiomeGenerators.h"
#include "Async/ParallelFor.h"

// ---------------------------------------------------------------
//  Biome colour palette
//  Order must match EVoxelBiome cast to uint8:
//    0=Forest  1=Peaks  2=Cliffs  3=Mesa  4=Craters  5=Desert
// ---------------------------------------------------------------
const FLinearColor FVoxelMapGenerator::BiomeColors[FVoxelBiomeWeightMap::MaxBiomes] =
{
    FLinearColor(0.07f, 0.60f, 0.07f, 1.f),   // Forest  — vivid green
    FLinearColor(0.80f, 0.84f, 0.98f, 1.f),   // Peaks   — icy blue-white
    FLinearColor(0.62f, 0.33f, 0.10f, 1.f),   // Cliffs  — warm brown
    FLinearColor(0.82f, 0.24f, 0.06f, 1.f),   // Mesa    — vivid red-earth
    FLinearColor(0.18f, 0.16f, 0.22f, 1.f),   // Craters — dark purple-grey
    FLinearColor(0.93f, 0.86f, 0.55f, 1.f),   // Desert  — warm sand
};

const TCHAR* FVoxelMapGenerator::BiomeNames[FVoxelBiomeWeightMap::MaxBiomes] =
{
    TEXT("Lush Forest"),
    TEXT("Jagged Peaks"),
    TEXT("Steep Cliffs"),
    TEXT("Mesa Plateaus"),
    TEXT("Impact Craters"),
    TEXT("Sand Dunes"),
};

// Compile-time guard: if MaxBiomes changes and these arrays are not updated,
// the build fails immediately with a clear message instead of a silent out-of-bounds read.
static_assert(
    FVoxelBiomeWeightMap::MaxBiomes == 6,
    "BiomeColors and BiomeNames must each have exactly MaxBiomes entries. "
    "Add the new biome colour and name before changing MaxBiomes.");

// ---------------------------------------------------------------
//  GeneratePixelBuffer
// ---------------------------------------------------------------
void FVoxelMapGenerator::GeneratePixelBuffer(
    float                         CenterX,
    float                         CenterY,
    float                         WorldRadius,
    int32                         Resolution,
    const FVoxelGenerationConfig& Config,
    const TSet<FIntVector>&       LoadedChunkCoords,
    float                         ChunkWorldSize,
    TArray<FColor>&               OutPixels)
{
    OutPixels.SetNumUninitialized(Resolution * Resolution);

    const float PixelWorldSize = (WorldRadius * 2.f) / (float)Resolution; // cm per pixel
    const float SeaLevel       = Config.SeaLevel;
    // Brightness reference: this matches FSkylandsLayerConfig::MaxTerrainReference (80-90k cm).
    // Pixels at or above this height are at full brightness; lower terrain is progressively darker.
    const float MaxHeight      = Config.SkylandsLayer.MaxTerrainReference;

    // Player dot radius (pixels)
    const int32 DotRadius = FMath::Max(2, Resolution / 64);

    // Centre pixel
    const int32 HalfRes = Resolution / 2;

    // ---- Sample every row in parallel ----
    ParallelFor(Resolution, [&](int32 py)
    {
        // In texture coordinates, py=0 is the top.
        // We want WorldY to increase downward (south) as py increases.
        const float WorldY = CenterY + (py - HalfRes) * PixelWorldSize;

        for (int32 px = 0; px < Resolution; ++px)
        {
            const float WorldX = CenterX + (px - HalfRes) * PixelWorldSize;

            // ---- 1. Biome blend ----
            FVoxelBiomeWeightMap Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(WorldX, WorldY, Config);
            const float SurfH           = FVoxelBiomeManager::GetSurfaceHeightStatic(WorldX, WorldY, Weights, Config);

            FLinearColor BlendedColor(0.f, 0.f, 0.f, 1.f);
            for (int32 b = 0; b < FVoxelBiomeWeightMap::MaxBiomes; ++b)
            {
                // operator[] maps index 0-5 to the named weight members (Forest, Peaks, Cliffs …)
                // in the same order as EVoxelBiome cast to uint8.
                BlendedColor += BiomeColors[b] * Weights[b];
            }

            // ---- 2. Height-based brightness ----
            // More contrast: low terrain is darker, peaks are brighter (pow 0.6 gives perceptual curve)
            const float HeightNorm = FMath::Clamp(SurfH / MaxHeight, 0.f, 1.f);
            const float Brightness = 0.20f + 0.80f * FMath::Pow(HeightNorm, 0.6f);
            BlendedColor *= Brightness;
            BlendedColor.A = 1.f;

            // ---- 3. Below sea-level → fade to deep blue ----
            if (SurfH < SeaLevel)
            {
                const float WaterDepth = FMath::Clamp((SeaLevel - SurfH) / 2000.f, 0.f, 1.f);
                const FLinearColor WaterColor(0.08f, 0.28f, 0.72f, 1.f);
                BlendedColor = FMath::Lerp(BlendedColor, WaterColor, WaterDepth);
            }

            // ---- 3b. Height contour lines every 2000 world units (20m) ----
            // Thin bright lines mark elevation bands like a topographic map.
            if (SurfH > SeaLevel)
            {
                const float ContourInterval = 2000.f;
                const float ContourMod = FMath::Fmod(FMath::Abs(SurfH - SeaLevel), ContourInterval);
                const float ContourHalfWidth = FMath::Max(PixelWorldSize * 0.7f, 60.f);
                if (ContourMod < ContourHalfWidth || ContourMod > ContourInterval - ContourHalfWidth)
                {
                    // Major contour every 10000 units: bright gold; minor: subtle white
                    const float MajorMod = FMath::Fmod(FMath::Abs(SurfH - SeaLevel), 10000.f);
                    const bool bMajor = MajorMod < ContourHalfWidth * 2.f || MajorMod > 10000.f - ContourHalfWidth * 2.f;
                    const FLinearColor ContourCol = bMajor
                        ? FLinearColor(0.9f, 0.8f, 0.3f, 1.f)   // gold for major
                        : FLinearColor(1.0f, 1.0f, 1.0f, 1.f);   // white for minor
                    BlendedColor = FMath::Lerp(BlendedColor, ContourCol, bMajor ? 0.55f : 0.28f);
                }
            }

            // ---- 4. Loaded chunk highlight ----
            // Work out which chunk coord this pixel falls in.
            // Loaded chunks get +0.15 brightness boost so the loaded region is visible.
            if (ChunkWorldSize > 0.f)
            {
                const FIntVector ChunkCoord(
                    FMath::FloorToInt(WorldX / ChunkWorldSize),
                    FMath::FloorToInt(WorldY / ChunkWorldSize),
                    0);  // We use Z=0 as a 2D lookup key; any chunk in the column counts.

                // Check a small vertical slice (typical for surface chunks)
                bool bLoaded = false;
                for (int32 cz = -2; cz <= 12 && !bLoaded; ++cz)
                {
                    bLoaded = LoadedChunkCoords.Contains(FIntVector(ChunkCoord.X, ChunkCoord.Y, cz));
                }

                if (bLoaded)
                {
                    // Subtle inner glow for loaded area
                    BlendedColor += FLinearColor(0.07f, 0.07f, 0.07f, 0.f);
                }
            }

            // ---- 5. Chunk grid lines ----
            // Draw a faint 1-pixel grid every ChunkWorldSize to visualise chunk boundaries.
            if (ChunkWorldSize > 0.f && Resolution >= 128)
            {
                const float ModX = FMath::Fmod(FMath::Abs(WorldX), ChunkWorldSize);
                const float ModY = FMath::Fmod(FMath::Abs(WorldY), ChunkWorldSize);
                if (ModX < PixelWorldSize || ModY < PixelWorldSize)
                {
                    BlendedColor = FMath::Lerp(BlendedColor, FLinearColor(0.f, 0.f, 0.f, 1.f), 0.25f);
                }
            }

            OutPixels[py * Resolution + px] = BlendedColor.ToFColor(true);
        }
    });

    // ---- 6. Player dot (painted after parallel loop to avoid races) ----
    // The player is always at the centre of a player-centred map.
    for (int32 dy = -DotRadius; dy <= DotRadius; ++dy)
    {
        for (int32 dx = -DotRadius; dx <= DotRadius; ++dx)
        {
            if (dx * dx + dy * dy <= DotRadius * DotRadius)
            {
                const int32 px = HalfRes + dx;
                const int32 py = HalfRes + dy;
                if (px >= 0 && px < Resolution && py >= 0 && py < Resolution)
                {
                    // White border ring, red fill
                    const bool bBorder = (dx * dx + dy * dy > (DotRadius - 1) * (DotRadius - 1));
                    OutPixels[py * Resolution + px] = bBorder ? FColor::White : FColor::Red;
                }
            }
        }
    }

    // ---- 7. Compass rose dot at North (top-centre) ----
    {
        const int32 CompassY = DotRadius + 2;
        const int32 CompassX = HalfRes;
        if (CompassX >= 0 && CompassX < Resolution && CompassY >= 0 && CompassY < Resolution)
            OutPixels[CompassY * Resolution + CompassX] = FColor(255, 255, 100, 255); // yellow N marker
    }
}
