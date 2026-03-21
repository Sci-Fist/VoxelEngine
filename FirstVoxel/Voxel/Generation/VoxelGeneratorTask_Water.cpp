// VoxelGeneratorTask_Water.cpp
//
// Implements PlaceWaterSources() for FVoxelGeneratorTask.
//
// This file was previously missing, which caused:
//   1. GetWaterSources() always returning an empty TArray
//   2. OnChunkWaterReady never receiving any sources
//   3. FVoxelWaterSimulator having nothing to simulate
//   → No voxel water appeared anywhere in the world
//
// WATER SOURCE PLACEMENT LOGIC:
//   For each interior air voxel at or below SeaLevel with solid terrain below,
//   in an ocean-weight or below-surface-height context → emit WATER_SOURCE.

#include "Generation/VoxelGeneratorTask.h"
#include "Biomes/VoxelBiomeManager.h"
#include "Config/VoxelGenerationConfig.h"
#include "VoxelLogger.h"   // UVoxelLogger::LogVoxelEvent

void FVoxelGeneratorTask::PlaceWaterSources()
{
    if (bCancelled) return;

    WaterSources.Empty();

    const bool bWaterOn = Config.Water.bEnableOcean || Config.Water.bUseVoxelOcean;
    if (!bWaterOn) return;

    const float SeaLevel  = Config.SeaLevel;
    const int32 EffCS     = ChunkSize / StepSize;
    const int32 S         = EffCS + 3;
    const float EffVoxSz  = VoxelSize * (float)StepSize;

    auto D = [&](int32 ix, int32 iy, int32 iz) -> float
    {
        if (ix<0||ix>=S||iy<0||iy>=S||iz<0||iz>=S) return -1.f;
        return Densities[ix + iy*S + iz*S*S];
    };

    for (int32 iz = 1; iz <= EffCS; ++iz)
    for (int32 iy = 1; iy <= EffCS; ++iy)
    for (int32 ix = 1; ix <= EffCS; ++ix)
    {
        if (D(ix, iy, iz) > 0.f) continue;                       // solid — skip

        const float WZ = WorldOrigin.Z + (iz - 1.f) * EffVoxSz;
        if (WZ > SeaLevel) continue;                              // above sea — skip

        if (D(ix, iy, iz-1) <= 0.f) continue;                    // no solid floor — skip

        const float WX = WorldOrigin.X + (ix - 1.f) * EffVoxSz;
        const float WY = WorldOrigin.Y + (iy - 1.f) * EffVoxSz;

        // Biome / height check using pre-built column cache where available
        float OceanW = 0.f;
        float SurfH  = 0.f;
        const int32 LX = ix-1, LY2 = iy-1;
        if (LX>=0&&LX<EffCS&&LY2>=0&&LY2<EffCS&&ColumnWeights.Num()==EffCS*EffCS)
        {
            OceanW = ColumnWeights [LX + LY2*EffCS].GetWeight(EVoxelBiome::Ocean);
            SurfH  = ColumnSurfaceH[LX + LY2*EffCS];
        }
        else
        {
            const FVoxelBiomeWeightMap W = FVoxelBiomeManager::GetBiomeWeightsStatic(WX, WY, Config);
            OceanW = W.GetWeight(EVoxelBiome::Ocean);
            SurfH  = FVoxelBiomeManager::GetSurfaceHeightStatic(WX, WY, W, Config);
        }

        // Fill if ocean biome OR terrain surface is below sea level (crater lakes, valleys)
        if (OceanW < 0.3f && SurfH >= SeaLevel - 200.f) continue;

        // World-voxel coord (anchor-relative integer voxel index)
        WaterSources.Add(FIntVector(
            ChunkCoord.X * ChunkSize + (ix - 1),
            ChunkCoord.Y * ChunkSize + (iy - 1),
            ChunkCoord.Z * ChunkSize + (iz - 1)));
    }

    if (WaterSources.Num() > 0)
        UVoxelLogger::LogVoxelEvent(FString::Printf(
            TEXT("VoxelWater: [%d,%d,%d] Placed %d water sources (SeaLevel=%.0f)"),
            ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z,
            WaterSources.Num(), SeaLevel));
}
