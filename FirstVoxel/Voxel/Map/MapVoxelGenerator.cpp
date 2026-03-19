// VoxelMapGenerator.cpp
// 
// CPU-based top-down world map generator for the voxel engine.
// Provides thread-safe map generation without touching UObjects, producing
// a flat BGRA pixel buffer suitable for UTexture2D upload on the game thread.
//
// ARCHITECTURE OVERVIEW:
// This generator creates a top-down representation of the voxel world by
// sampling terrain height and biome data at regular intervals across a
// specified world area. The implementation is designed for performance
// with parallel processing and minimal memory overhead.
//
// KEY FEATURES:
// - Thread-safe operation without UObject dependencies
// - Parallel processing for optimal performance
// - Biome-based color blending with height-based brightness
// - Topographic contour lines for elevation visualization
// - Loaded chunk highlighting for debugging
// - Player position indicator
// - Chunk grid visualization
//
// PERFORMANCE CHARACTERISTICS:
// - Uses ParallelFor for multi-threaded pixel processing
// - Minimal memory allocation (single pixel buffer)
// - Static sampling functions avoid object instantiation
// - Optimized color calculations with precomputed palettes

#include "Voxel/Map/VoxelMapGenerator.h"
#include "Voxel/Biomes/VoxelBiomeManager.h"
#include "Voxel/Biomes/VoxelBiomeGenerators.h"
#include "Async/ParallelFor.h"

// ---------------------------------------------------------------
//  Biome Colour Palette and Names
// ---------------------------------------------------------------
// Predefined color palette for biome visualization
// Colors are carefully chosen to provide good contrast and intuitive representation
// Order must match EVoxelBiome enum values when cast to uint8:
//   0=Forest  1=Peaks  2=Cliffs  3=Mesa  4=Craters  5=Desert
const FLinearColor FVoxelMapGenerator::BiomeColors[FVoxelBiomeWeightMap::MaxBiomes] =
{
    FLinearColor(0.07f, 0.60f, 0.07f, 1.f),   // Forest  — vivid green for vegetation
    FLinearColor(0.80f, 0.84f, 0.98f, 1.f),   // Peaks   — icy blue-white for snow/ice
    FLinearColor(0.62f, 0.33f, 0.10f, 1.f),   // Cliffs  — warm brown for rock formations
    FLinearColor(0.82f, 0.24f, 0.06f, 1.f),   // Mesa    — vivid red-earth for desert plateaus
    FLinearColor(0.18f, 0.16f, 0.22f, 1.f),   // Craters — dark purple-grey for impact zones
    FLinearColor(0.93f, 0.86f, 0.55f, 1.f),   // Desert  — warm sand color for arid regions
    FLinearColor(0.08f, 0.28f, 0.72f, 1.f),   // Ocean   — deep blue
};

// Human-readable biome display names for UI and debugging
const TCHAR* FVoxelMapGenerator::BiomeNames[FVoxelBiomeWeightMap::MaxBiomes] =
{
    TEXT("Lush Forest"),
    TEXT("Jagged Peaks"),
    TEXT("Steep Cliffs"),
    TEXT("Mesa Plateaus"),
    TEXT("Impact Craters"),
    TEXT("Sand Dunes"),
    TEXT("Open Ocean"),
};

// Compile-time assertion to ensure palette and names arrays match MaxBiomes count
static_assert(FVoxelBiomeWeightMap::MaxBiomes == 7,
    "BiomeColors and BiomeNames must each have exactly MaxBiomes entries.");


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

    // Debug: Log map generation parameters
    UE_LOG(LogTemp, Verbose, TEXT("VoxelMapGenerator::GeneratePixelBuffer - Center=(%.1f,%.1f) Radius=%.1f Res=%d ChunkSize=%.1f LoadedChunks=%d"),
        CenterX, CenterY, WorldRadius, Resolution, ChunkWorldSize, LoadedChunkCoords.Num());

    
    // Initialize output pixel buffer with required size

    // Resolution × Resolution for square map output

    OutPixels.SetNumUninitialized(Resolution * Resolution);


    // Calculate world-to-pixel conversion factors
    const float PixelWorldSize = (WorldRadius * 2.f) / (float)Resolution;
    const float SeaLevel       = Config.SeaLevel;
    const float MaxHeight      = Config.SkylandsLayer.MaxTerrainReference;
    
    // Calculate player dot size based on resolution
    const int32 DotRadius      = FMath::Max(2, Resolution / 64);
    const int32 HalfRes        = Resolution / 2;

    // Parallel processing of map pixels for optimal performance
    // Each thread processes one row of pixels (py coordinate)
    ParallelFor(Resolution, [&](int32 py)
    {
        // Calculate world Y coordinate for this pixel row
        const float WorldY = CenterY + (py - HalfRes) * PixelWorldSize;

        // Process each pixel in the current row
        for (int32 px = 0; px < Resolution; ++px)
        {
            // Calculate world X coordinate for this pixel
            const float WorldX = CenterX + (px - HalfRes) * PixelWorldSize;

            // Sample biome weights and surface height at world coordinates
            // Uses static methods to avoid object instantiation and ensure thread safety
            FVoxelBiomeWeightMap Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(WorldX, WorldY, Config);
            const float SurfH = FVoxelBiomeManager::GetSurfaceHeightStatic(WorldX, WorldY, Weights, Config);

            // Calculate base color by blending biome colors according to weights
            FLinearColor C(0.f, 0.f, 0.f, 1.f);
            for (int32 b = 0; b < FVoxelBiomeWeightMap::MaxBiomes; ++b)
                C += BiomeColors[b] * Weights[b];

            // Apply height-based brightness adjustment
            // Higher elevations are brighter, lower elevations are darker
            // Uses power curve for non-linear brightness distribution
            const float Brightness = 0.20f + 0.80f * FMath::Pow(FMath::Clamp(SurfH / MaxHeight, 0.f, 1.f), 0.6f);
            C *= Brightness;
            C.A = 1.f;

            // Water depth coloring for areas below sea level
            // Deep water transitions from terrain color to deep blue
            if (SurfH < SeaLevel)
            {
                const float WD = FMath::Clamp((SeaLevel - SurfH) / 2000.f, 0.f, 1.f);
                C = FMath::Lerp(C, FLinearColor(0.08f, 0.28f, 0.72f, 1.f), WD);
            }

            // Topographic contour lines for elevation visualization
            // Major lines every 10,000 units, minor lines every 2,000 units
            if (SurfH > SeaLevel)
            {
                const float CI  = 2000.f;  // Contour interval
                const float Mod = FMath::Fmod(FMath::Abs(SurfH - SeaLevel), CI);
                const float HW  = FMath::Max(PixelWorldSize * 0.7f, 60.f);  // Line width in world units
                
                if (Mod < HW || Mod > CI - HW)
                {
                    // Determine if this is a major contour line (every 5th line)
                    const float MajMod = FMath::Fmod(FMath::Abs(SurfH - SeaLevel), 10000.f);
                    const bool bMajor  = MajMod < HW * 2.f || MajMod > 10000.f - HW * 2.f;
                    
                    // Major lines are yellow, minor lines are white
                    const FLinearColor CC = bMajor
                        ? FLinearColor(0.9f, 0.8f, 0.3f, 1.f) : FLinearColor(1.f, 1.f, 1.f, 1.f);
                    C = FMath::Lerp(C, CC, bMajor ? 0.55f : 0.28f);
                }
            }

            // Highlight loaded chunks for debugging purposes
            // Adds subtle brightness to chunks currently in memory
            if (ChunkWorldSize > 0.f)
            {
                const FIntVector CC(FMath::FloorToInt(WorldX / ChunkWorldSize),
                                    FMath::FloorToInt(WorldY / ChunkWorldSize), 0);
                bool bLoaded = false;
                // Check multiple Z levels to account for chunk height
                for (int32 cz = -2; cz <= 12 && !bLoaded; ++cz)
                    bLoaded = LoadedChunkCoords.Contains(FIntVector(CC.X, CC.Y, cz));
                if (bLoaded) C += FLinearColor(0.07f, 0.07f, 0.07f, 0.f);
            }

            // Draw chunk grid lines for debugging
            // Only shown at high resolutions to avoid visual clutter
            if (ChunkWorldSize > 0.f && Resolution >= 128)
            {
                const float ModX = FMath::Fmod(FMath::Abs(WorldX), ChunkWorldSize);
                const float ModY = FMath::Fmod(FMath::Abs(WorldY), ChunkWorldSize);
                if (ModX < PixelWorldSize || ModY < PixelWorldSize)
                    C = FMath::Lerp(C, FLinearColor(0.f, 0.f, 0.f, 1.f), 0.25f);
            }

            // Convert linear color to final pixel format
            OutPixels[py * Resolution + px] = C.ToFColor(true);
        }
    });

    // Draw player position indicator (red dot with white border)
    // Always centered on the map regardless of actual player position
    for (int32 dy = -DotRadius; dy <= DotRadius; ++dy)
    for (int32 dx = -DotRadius; dx <= DotRadius; ++dx)
    {
        if (dx*dx + dy*dy <= DotRadius*DotRadius)
        {
            const int32 px = HalfRes + dx, py = HalfRes + dy;
            if (px >= 0 && px < Resolution && py >= 0 && py < Resolution)
            {
                // Create white border around red center
                const bool bBorder = (dx*dx + dy*dy > (DotRadius-1)*(DotRadius-1));
                OutPixels[py * Resolution + px] = bBorder ? FColor::White : FColor::Red;
            }
        }
    }

    // Draw north indicator (yellow marker above player dot)
    const int32 CY = DotRadius + 2, CX = HalfRes;
    if (CX >= 0 && CX < Resolution && CY >= 0 && CY < Resolution)
        OutPixels[CY * Resolution + CX] = FColor(255, 255, 100, 255);
}
