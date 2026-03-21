// VoxelGeneratorTask_Water.cpp
// PlaceWaterSources — extracted from the VoxelGeneratorTask monolith.
// Scans the freshly-built density field for air-over-solid voxels that
// qualify as lake/pond sources, then seeds FVoxelWaterSimulator via
// the world registration path (OnChunkWaterReady callback in ApplyMesh).

#include "Generation/VoxelGeneratorTask.h"
#include "Biomes/VoxelBiomeManager.h"
#include "Voxel/Config/VoxelGenerationConfig.h"

void FVoxelGeneratorTask::PlaceWaterSources()
{
    WaterSources.Reset();
    if (StepSize > 1) return; // water only at full LOD

    const int32 CS   = ChunkSize;
    const int32 S    = CS + 3;
    const int32 EffS = CS;

    // Density accessors — identical bounds/padding logic to BuildDensityField
    auto Dens = [&](int32 lx, int32 ly, int32 lz) -> float {
        return Densities[
            FMath::Clamp(lx+1, 0, S-1) +
            FMath::Clamp(ly+1, 0, S-1) * S +
            FMath::Clamp(lz+1, 0, S-1) * S * S];
    };
    auto IsSolid = [&](int32 x, int32 y, int32 z) { return Dens(x,y,z) >  0.f; };
    auto IsAir   = [&](int32 x, int32 y, int32 z) { return Dens(x,y,z) <= 0.f; };

    // Integer hash — no FMath::FRand() calls here; this runs on a background thread
    // and FRand() uses thread-local state that can diverge per-task.
    auto RandH = [](int32 x, int32 y, int32 z, int32 seed) -> float {
        uint32 h = (uint32)(x*73856093 ^ y*19349663 ^ z*83492791 ^ seed);
        h = (h ^ (h >> 16)) * 0x45d9f3b;
        h ^= h >> 16;
        return (float)(h & 0xFFFFFF) / (float)0xFFFFFF;
    };

    const float MinSkyAlt = Config.SeaLevel + Config.SkylandsLayer.MinAltitudeAboveTerrain;

    for (int32 lz = 0; lz < CS; ++lz)
    for (int32 ly = 0; ly < CS; ++ly)
    for (int32 lx = 0; lx < CS; ++lx)
    {
        if (bCancelled) return;

        // Must be an air voxel with a solid floor
        if (!IsAir(lx,ly,lz) || !IsSolid(lx,ly,lz-1)) continue;

        const float WZ = WorldOrigin.Z + lz * VoxelSize;

        // Skip open-ocean surface voxels (handled by voxel ocean initialisation)
        if (!Config.Water.bUseVoxelOcean && Config.Water.bEnableOcean
            && WZ <= Config.SeaLevel + VoxelSize) continue;

        // Cave ceiling check — skip if open-air at sea level
        const bool bCave = IsSolid(lx, ly, lz+1);
        if (Config.Water.bEnableOcean && WZ <= Config.SeaLevel + VoxelSize && !bCave) continue;

        // Require at least 2 solid side-neighbours (depression, not cliff edge)
        int32 SN = 0;
        if (IsSolid(lx+1,ly,lz)) SN++;
        if (IsSolid(lx-1,ly,lz)) SN++;
        if (IsSolid(lx,ly+1,lz)) SN++;
        if (IsSolid(lx,ly-1,lz)) SN++;
        if (SN < 2) continue;

        // Classify as skylands vs surface for water config lookup
        const bool bSky = (WZ >= MinSkyAlt);
        float SpawnChance = 0.f;

        if (bSky)
        {
            const FVoxelBiomeWaterConfig& BWC = Config.SkylandsWater;
            if (!BWC.bEnableLakes || !IsSolid(lx, ly, lz-2)) continue;
            SpawnChance = FMath::Clamp(BWC.LakeSpawnProbability * 0.12f, 0.f, 1.f);
        }
        else
        {
            // Look up column biome weight for per-biome water config
            const int32 gX = FMath::Clamp(lx, 0, EffS-1);
            const int32 gY = FMath::Clamp(ly, 0, EffS-1);
            const int32 CI = gX + gY * EffS;
            FVoxelBiomeWeightMap W;
            if (ColumnWeights.IsValidIndex(CI)) W = ColumnWeights[CI];
            const FVoxelBiomeWaterConfig& BWC = Config.GetBiomeWater(W.GetDominantBiome());
            if (!BWC.bEnableLakes) continue;
            // More enclosed depressions (SN==4) get a higher spawn bonus
            const float EF = (SN==4) ? 1.5f : (SN==3) ? 1.1f : 0.7f;
            SpawnChance = FMath::Clamp(BWC.LakeSpawnProbability * EF * 0.15f, 0.f, 1.f);
        }

        if (RandH(lx, ly, lz, Config.Seed) < SpawnChance)
            WaterSources.Add(FIntVector(
                ChunkCoord.X * CS + lx,
                ChunkCoord.Y * CS + ly,
                ChunkCoord.Z * CS + lz));
    }
}
