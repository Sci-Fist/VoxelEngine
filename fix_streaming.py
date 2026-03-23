import os

filepath = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Core\World\Streaming\VoxelStreamingComponent.cpp"

with open(filepath, 'r', encoding='utf-8') as f:
    lines = f.readlines()

start_idx = 64
end_idx = 331

replacement = """    // 1. Sky Altitude
    CalculateSkyAltitude(PlayerPos, Config);

    // 2. Column Heights
    TArray<FVoxelBiomeManager::FWeightsAndHeight> CachedColumns;
    GatherColumnHeights(PlayerCoord, ChunkWorldSize, Config, CachedColumns);

    // 3. Island Thickness Math
    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
    const float IslandSize    = FMath::Max(SC.BaseIslandSize, SC.BaseIslandSize + CachedCurvedH * SC.HeightSizeBonus + CachedCurvedR * SC.RoughnessSizeBonus);
    const float HalfThickCm   = FMath::Max(200.f, IslandSize * SC.ThicknessRatio);

    int32 SkyZMin = 0;
    int32 SkyZMax = 0;

    // 4. Build Desired Set
    TSet<FIntVector> Desired;
    BuildDesiredChunkSet(PlayerCoord, CachedColumns, ChunkWorldSize, CachedSkyAltWorld, HalfThickCm, SkyZMin, SkyZMax, Desired);

    // 5. Unload Out-of-Range
    TArray<FIntVector> ToRemove;
    const TMap<FIntVector, AVoxelChunk*>* LoadedChunks = World->GetLoadedChunks();
    for (auto& It : *LoadedChunks)
    {
        if (!Desired.Contains(It.Key))
            ToRemove.Add(It.Key);
    }
    for (const FIntVector& C : ToRemove)
        World->DestroyChunk(C);

    // 6. Self-healing sky chunks
    World->ClearEmptyChunksInRange(SkyZMin, SkyZMax);

    // 7. Update LODs
    UpdateLODs(PlayerPos, PlayerCoord, SkyZMin, SkyZMax, Desired);

    // 8. Sort Queue
    RebuildGenerationQueue(PlayerPos, Player->GetActorForwardVector(), Desired);
"""

new_lines = lines[:start_idx] + [replacement] + lines[end_idx+1:]

with open(filepath, 'w', encoding='utf-8') as f:
    f.writelines(new_lines)

print("Replacement successful")
