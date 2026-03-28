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
        if (D(ix, iy, iz-1) <= 0.f) continue;                    // no solid floor — skip

        const float WZ = WorldOrigin.Z + (iz - 1.f) * EffVoxSz;
        const float WX = WorldOrigin.X + (ix - 1.f) * EffVoxSz;
        const float WY = WorldOrigin.Y + (iy - 1.f) * EffVoxSz;

        // Fetch biome weight context
        float OceanW = 0.f;
        float SurfH  = 0.f;
        FVoxelBiomeWeightMap W;
        const int32 EIdx = ix + iy * S;
        if (ColScratch.PrecalcColumns.IsValidIndex(EIdx))
        {
            const FColumnCacheItem& Item = ColScratch.PrecalcColumns[EIdx];
            W      = Item.Weights;
            OceanW = W.GetWeight(EVoxelBiome::Ocean);
            SurfH  = Item.SurfH;
        }
        else
        {
            W      = FVoxelBiomeManager::GetBiomeWeightsStatic(WX, WY, Config);
            OceanW = W.GetWeight(EVoxelBiome::Ocean);
            SurfH  = FVoxelBiomeManager::GetSurfaceHeightStatic(WX, WY, W, Config);
        }

        // --- 1. Below SeaLevel fill mode (Original setup) ---
        bool bPlaceSource = false;
        if (WZ <= SeaLevel)
        {
            if (OceanW >= 0.3f || SurfH < SeaLevel - 200.f)
            {
                bPlaceSource = true;
            }
        }

        // --- 2. Above SeaLevel hollow/lake detection (New Mode) ---
        if (!bPlaceSource && WZ > SeaLevel)
        {
            // Determine dominant biome
            EVoxelBiome DomBiome = EVoxelBiome::Forest;
            float MaxW = 0.f;
            // Iterate known biomes: Forest=0, Peaks=1, Cliffs=2, Mesa=3, Craters=4, Desert=5, Ocean=6
            for (int32 b = 0; b <= 6; ++b)
            {
                float w = W.GetWeight((EVoxelBiome)b);
                if (w > MaxW) { MaxW = w; DomBiome = (EVoxelBiome)b; }
            }

            // Lookup biome config
            bool bEnableLakes = false;
            float LakeProb    = 0.f;
            switch (DomBiome)
            {
                case EVoxelBiome::Forest:  bEnableLakes=Config.ForestWater.bEnableLakes; LakeProb=Config.ForestWater.LakeSpawnProbability; break;
                case EVoxelBiome::Peaks:   bEnableLakes=Config.PeaksWater.bEnableLakes;  LakeProb=Config.PeaksWater.LakeSpawnProbability;  break;
                case EVoxelBiome::Cliffs:  bEnableLakes=Config.CliffsWater.bEnableLakes; LakeProb=Config.CliffsWater.LakeSpawnProbability; break;
                case EVoxelBiome::Mesa:    bEnableLakes=Config.MesaWater.bEnableLakes;   LakeProb=Config.MesaWater.LakeSpawnProbability;   break;
                case EVoxelBiome::Craters: bEnableLakes=Config.CratersWater.bEnableLakes;LakeProb=Config.CratersWater.LakeSpawnProbability;break;
                case EVoxelBiome::Desert:  bEnableLakes=Config.DesertWater.bEnableLakes; LakeProb=Config.DesertWater.LakeSpawnProbability; break;
                default: break;
            }

            if (bEnableLakes && LakeProb > 0.01f)
            {
                int32 SolidNeighbors = 0;
                if (D(ix + 1, iy, iz) > 0.f) SolidNeighbors++;
                if (D(ix - 1, iy, iz) > 0.f) SolidNeighbors++;
                if (D(ix, iy + 1, iz) > 0.f) SolidNeighbors++;
                if (D(ix, iy - 1, iz) > 0.f) SolidNeighbors++;

                // If surrounded by 3+ solid walls (a hollow/basin pocket)
                if (SolidNeighbors >= 3)
                {
                    // Deterministic coordinate lookup hash
                    const uint32 H = ((uint32)FMath::Abs(WX) * 2654435761u) ^ 
                                     ((uint32)FMath::Abs(WY) * 2246822519u) ^ 
                                     ((uint32)FMath::Abs(WZ) * 3266489917u);
                    const float RandVal = (float)(H % 1000) / 1000.f;
                    if (RandVal < LakeProb)
                    {
                        bPlaceSource = true;
                    }
                }
            }
        }

        if (bPlaceSource)
        {
            WaterSources.Add(FIntVector(
                ChunkCoord.X * ChunkSize + (ix - 1),
                ChunkCoord.Y * ChunkSize + (iy - 1),
                ChunkCoord.Z * ChunkSize + (iz - 1)));
        }
    }

    if (WaterSources.Num() > 0)
        UVoxelLogger::LogVoxelEvent(FString::Printf(
            TEXT("VoxelWater: [%d,%d,%d] Placed %d water sources (SeaLevel=%.0f)"),
            ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z,
            WaterSources.Num(), SeaLevel));
}
