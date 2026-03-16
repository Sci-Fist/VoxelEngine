// VoxelMapGenerator.cpp  [canonical location: Voxel/Map/]
#include "Voxel/Map/VoxelMapGenerator.h"
#include "Voxel/Biomes/VoxelBiomeManager.h"
#include "Voxel/Biomes/VoxelBiomeGenerators.h"
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

static_assert(FVoxelBiomeWeightMap::MaxBiomes == 6,
    "BiomeColors and BiomeNames must each have exactly MaxBiomes entries.");

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

    const float PixelWorldSize = (WorldRadius * 2.f) / (float)Resolution;
    const float SeaLevel       = Config.SeaLevel;
    const float MaxHeight      = Config.SkylandsLayer.MaxTerrainReference;
    const int32 DotRadius      = FMath::Max(2, Resolution / 64);
    const int32 HalfRes        = Resolution / 2;

    ParallelFor(Resolution, [&](int32 py)
    {
        const float WorldY = CenterY + (py - HalfRes) * PixelWorldSize;

        for (int32 px = 0; px < Resolution; ++px)
        {
            const float WorldX = CenterX + (px - HalfRes) * PixelWorldSize;

            FVoxelBiomeWeightMap Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(WorldX, WorldY, Config);
            const float SurfH = FVoxelBiomeManager::GetSurfaceHeightStatic(WorldX, WorldY, Weights, Config);

            FLinearColor C(0.f, 0.f, 0.f, 1.f);
            for (int32 b = 0; b < FVoxelBiomeWeightMap::MaxBiomes; ++b)
                C += BiomeColors[b] * Weights[b];

            const float Brightness = 0.20f + 0.80f * FMath::Pow(FMath::Clamp(SurfH / MaxHeight, 0.f, 1.f), 0.6f);
            C *= Brightness;
            C.A = 1.f;

            if (SurfH < SeaLevel)
            {
                const float WD = FMath::Clamp((SeaLevel - SurfH) / 2000.f, 0.f, 1.f);
                C = FMath::Lerp(C, FLinearColor(0.08f, 0.28f, 0.72f, 1.f), WD);
            }

            // Topographic contour lines
            if (SurfH > SeaLevel)
            {
                const float CI  = 2000.f;
                const float Mod = FMath::Fmod(FMath::Abs(SurfH - SeaLevel), CI);
                const float HW  = FMath::Max(PixelWorldSize * 0.7f, 60.f);
                if (Mod < HW || Mod > CI - HW)
                {
                    const float MajMod = FMath::Fmod(FMath::Abs(SurfH - SeaLevel), 10000.f);
                    const bool bMajor  = MajMod < HW * 2.f || MajMod > 10000.f - HW * 2.f;
                    const FLinearColor CC = bMajor
                        ? FLinearColor(0.9f, 0.8f, 0.3f, 1.f) : FLinearColor(1.f, 1.f, 1.f, 1.f);
                    C = FMath::Lerp(C, CC, bMajor ? 0.55f : 0.28f);
                }
            }

            // Loaded chunk highlight
            if (ChunkWorldSize > 0.f)
            {
                const FIntVector CC(FMath::FloorToInt(WorldX / ChunkWorldSize),
                                    FMath::FloorToInt(WorldY / ChunkWorldSize), 0);
                bool bLoaded = false;
                for (int32 cz = -2; cz <= 12 && !bLoaded; ++cz)
                    bLoaded = LoadedChunkCoords.Contains(FIntVector(CC.X, CC.Y, cz));
                if (bLoaded) C += FLinearColor(0.07f, 0.07f, 0.07f, 0.f);
            }

            // Chunk grid lines
            if (ChunkWorldSize > 0.f && Resolution >= 128)
            {
                const float ModX = FMath::Fmod(FMath::Abs(WorldX), ChunkWorldSize);
                const float ModY = FMath::Fmod(FMath::Abs(WorldY), ChunkWorldSize);
                if (ModX < PixelWorldSize || ModY < PixelWorldSize)
                    C = FMath::Lerp(C, FLinearColor(0.f, 0.f, 0.f, 1.f), 0.25f);
            }

            OutPixels[py * Resolution + px] = C.ToFColor(true);
        }
    });

    // Player dot (red with white border, always centered)
    for (int32 dy = -DotRadius; dy <= DotRadius; ++dy)
    for (int32 dx = -DotRadius; dx <= DotRadius; ++dx)
    {
        if (dx*dx + dy*dy <= DotRadius*DotRadius)
        {
            const int32 px = HalfRes + dx, py = HalfRes + dy;
            if (px >= 0 && px < Resolution && py >= 0 && py < Resolution)
            {
                const bool bBorder = (dx*dx + dy*dy > (DotRadius-1)*(DotRadius-1));
                OutPixels[py * Resolution + px] = bBorder ? FColor::White : FColor::Red;
            }
        }
    }

    // Yellow north marker
    const int32 CY = DotRadius + 2, CX = HalfRes;
    if (CX >= 0 && CX < Resolution && CY >= 0 && CY < Resolution)
        OutPixels[CY * Resolution + CX] = FColor(255, 255, 100, 255);
}
