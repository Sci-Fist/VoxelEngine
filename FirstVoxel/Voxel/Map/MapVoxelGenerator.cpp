// MapVoxelGenerator.cpp
//
// FIX #36 — Replaced two separate GetBiomeWeightsStatic + GetSurfaceHeightStatic
//            calls per pixel with one GetWeightsAndSurfaceHeightStatic call.
//            At 256×256 = 65536 pixels this halves the biome noise evaluations.
//
// FIX #42 — Loaded chunk XY check now uses a pre-built TSet<TPair<int32,int32>>
//            (one entry per unique XY pair) so each pixel does a single O(1)
//            lookup instead of 14 Z-level TSet::Contains probes.
//
// FIX #44 — North indicator is now a filled triangle (7 pixels) instead of a
//            single invisible pixel.

#include "Voxel/Map/VoxelMapGenerator.h"
#include "Voxel/Biomes/VoxelBiomeManager.h"
#include "Voxel/Biomes/VoxelBiomeGenerators.h"
#include "Async/ParallelFor.h"

const FLinearColor FVoxelMapGenerator::BiomeColors[FVoxelBiomeWeightMap::MaxBiomes] =
{
    FLinearColor(0.07f, 0.60f, 0.07f, 1.f),
    FLinearColor(0.80f, 0.84f, 0.98f, 1.f),
    FLinearColor(0.62f, 0.33f, 0.10f, 1.f),
    FLinearColor(0.82f, 0.24f, 0.06f, 1.f),
    FLinearColor(0.18f, 0.16f, 0.22f, 1.f),
    FLinearColor(0.93f, 0.86f, 0.55f, 1.f),
    FLinearColor(0.08f, 0.28f, 0.72f, 1.f),
};

const TCHAR* FVoxelMapGenerator::BiomeNames[FVoxelBiomeWeightMap::MaxBiomes] =
{
    TEXT("Lush Forest"),   TEXT("Jagged Peaks"), TEXT("Steep Cliffs"),
    TEXT("Mesa Plateaus"), TEXT("Impact Craters"),TEXT("Sand Dunes"),
    TEXT("Open Ocean"),
};

static_assert(FVoxelBiomeWeightMap::MaxBiomes == 7,
    "BiomeColors and BiomeNames must have exactly MaxBiomes entries.");

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
    UE_LOG(LogTemp, Log, TEXT("VoxelMapGenerator: GeneratePixelBuffer start. Center=[%.0f, %.0f] Res=%d Radius=%.0f LoadedChunks=%d"), 
        CenterX, CenterY, Resolution, WorldRadius, LoadedChunkCoords.Num());
    const float SeaLevel       = Config.SeaLevel;
    const float MaxHeight      = Config.SkylandsLayer.MaxTerrainReference;
    const int32 DotRadius      = FMath::Max(2, Resolution / 64);
    const int32 HalfRes        = Resolution / 2;

    // FIX #42: pre-build a flat XY set — one lookup per pixel instead of 14
    // A uint64 packs (X & 0xFFFFFFFF) | ((uint64)Y << 32) for a cheap key
    TSet<uint64> LoadedXY;
    LoadedXY.Reserve(LoadedChunkCoords.Num());
    for (const FIntVector& CC : LoadedChunkCoords)
    {
        const uint64 Key = ((uint64)(uint32)CC.X) | ((uint64)(uint32)CC.Y << 32);
        LoadedXY.Add(Key);
    }

    ParallelFor(Resolution, [&](int32 py)
    {
        const float WorldY = CenterY + (py - HalfRes) * PixelWorldSize;

        for (int32 px = 0; px < Resolution; ++px)
        {
            const float WorldX = CenterX + (px - HalfRes) * PixelWorldSize;

            // FIX #36: single batched call instead of two separate evaluations
            const auto Wh = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(WorldX, WorldY, Config);
            const FVoxelBiomeWeightMap& W = Wh.Weights;
            const float SurfH             = Wh.SurfaceHeight;

            FLinearColor C(0.f, 0.f, 0.f, 1.f);
            for (int32 b = 0; b < FVoxelBiomeWeightMap::MaxBiomes; ++b)
                C += BiomeColors[b] * W[b];

            const float Brightness = 0.20f + 0.80f * FMath::Pow(
                FMath::Clamp(SurfH / MaxHeight, 0.f, 1.f), 0.6f);
            C *= Brightness;
            C.A = 1.f;

            if (SurfH < SeaLevel)
            {
                const float WD = FMath::Clamp((SeaLevel - SurfH) / 2000.f, 0.f, 1.f);
                C = FMath::Lerp(C, FLinearColor(0.08f, 0.28f, 0.72f, 1.f), WD);
            }

            if (SurfH > SeaLevel)
            {
                const float CI  = 2000.f;
                const float Mod = FMath::Fmod(FMath::Abs(SurfH - SeaLevel), CI);
                const float HW  = FMath::Max(PixelWorldSize * 0.7f, 60.f);
                if (Mod < HW || Mod > CI - HW)
                {
                    const float MajMod = FMath::Fmod(FMath::Abs(SurfH - SeaLevel), 10000.f);
                    const bool  bMajor = MajMod < HW * 2.f || MajMod > 10000.f - HW * 2.f;
                    const FLinearColor CC = bMajor
                        ? FLinearColor(0.9f, 0.8f, 0.3f, 1.f)
                        : FLinearColor(1.f,  1.f,  1.f,  1.f);
                    C = FMath::Lerp(C, CC, bMajor ? 0.55f : 0.28f);
                }
            }

            // FIX #42: single O(1) lookup per pixel
            if (ChunkWorldSize > 0.f)
            {
                const int32  CX  = FMath::FloorToInt(WorldX / ChunkWorldSize);
                const int32  CY2 = FMath::FloorToInt(WorldY / ChunkWorldSize);
                const uint64 Key = ((uint64)(uint32)CX) | ((uint64)(uint32)CY2 << 32);
                if (LoadedXY.Contains(Key)) C += FLinearColor(0.07f, 0.07f, 0.07f, 0.f);
            }

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

    // Player dot
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

    // FIX #44: north indicator — filled 3×5 arrow instead of single invisible pixel
    // Draws a small upward-pointing triangle above the player dot
    {
        const int32 TipY   = HalfRes - DotRadius - 3;
        const int32 TipX   = HalfRes;
        const FColor ArrowColor(255, 255, 100, 255);

        auto SafeSet = [&](int32 px, int32 py) {
            if (px >= 0 && px < Resolution && py >= 0 && py < Resolution)
                OutPixels[py * Resolution + px] = ArrowColor;
        };

        // tip row
        SafeSet(TipX, TipY);
        // row +1
        SafeSet(TipX-1, TipY+1); SafeSet(TipX, TipY+1); SafeSet(TipX+1, TipY+1);
        // row +2
        for (int32 dx = -2; dx <= 2; ++dx) SafeSet(TipX+dx, TipY+2);
        // row +3 (base)
        for (int32 dx = -2; dx <= 2; ++dx) SafeSet(TipX+dx, TipY+3);
    }
}
