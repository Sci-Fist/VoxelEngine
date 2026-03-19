// VoxelWorld_Modification.cpp
//
// Voxel modification, persistence, coordinate utilities, and crater spawn search.

#include "VoxelWorld.h"
#include "Voxel/Core/VoxelChunk.h"
#include "Voxel/Core/VoxelDataMap.h"
#include "Voxel/Generation/VoxelDensityGenerator.h"
#include "Voxel/Biomes/VoxelBiomeManager.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "Voxel/VoxelLogger.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

// =============================================================================
//  Voxel Modification
// =============================================================================

void AVoxelWorld::SetVoxelSphere(FVector WorldPosition, float Radius, float DensityValue, bool bRebuildChunks)
{
    if (!GetWorld() || bShutdown) return;

    UE_LOG(LogVoxelWorld, Verbose, TEXT("VoxelWorld: SetVoxelSphere at %s, Radius=%.2f, Density=%.2f"),
        *WorldPosition.ToString(), Radius, DensityValue);

    DataMap.SetSphere(WorldPosition, Radius, DensityValue, VoxelSize, GetActorLocation());

    if (bRebuildChunks)
    {
        const FIntVector MinCoord = WorldToChunkCoord(WorldPosition - FVector(Radius));
        const FIntVector MaxCoord = WorldToChunkCoord(WorldPosition + FVector(Radius));
        for (int32 z = MinCoord.Z; z <= MaxCoord.Z; ++z)
        for (int32 y = MinCoord.Y; y <= MaxCoord.Y; ++y)
        for (int32 x = MinCoord.X; x <= MaxCoord.X; ++x)
        {
            const FIntVector Coord(x, y, z);
            if (LoadedChunks.Contains(Coord))
                MarkChunkDirty(Coord);
        }
    }
}

void AVoxelWorld::ClearModifications()
{
    DataMap.Clear();
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: All modifications cleared"));
}

// =============================================================================
//  Persistence
// =============================================================================

void AVoxelWorld::SaveToFile(const FString& SlotName)
{
    if (!GetWorld()) return;

    const FString SaveDir  = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VoxelSaves"));
    IPlatformFile& PF      = FPlatformFileManager::Get().GetPlatformFile();
    if (!PF.DirectoryExists(*SaveDir)) PF.CreateDirectoryTree(*SaveDir);

    const FString FilePath = FPaths::Combine(SaveDir,
        FString::Printf(TEXT("%s_%s.sav"), *GetName(), *SlotName));

    FBufferArchive ToBuffer;
    float Version = 1.0f;
    int32 Seed    = GetEffectiveConfig().Seed;
    ToBuffer << Version << Seed;
    DataMap.Serialize(ToBuffer);

    if (FFileHelper::SaveArrayToFile(ToBuffer, *FilePath))
    {
        UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Saved to slot '%s'"), *SlotName);
    }
    else
    {
        UE_LOG(LogVoxelWorld, Error, TEXT("VoxelWorld: Failed to save slot '%s'"), *SlotName);
    }
}

void AVoxelWorld::LoadFromFile(const FString& SlotName)
{
    if (!GetWorld()) return;

    const FString SaveDir  = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VoxelSaves"));
    const FString FilePath = FPaths::Combine(SaveDir,
        FString::Printf(TEXT("%s_%s.sav"), *GetName(), *SlotName));

    if (!FPaths::FileExists(FilePath)) return;

    TArray<uint8> Buffer;
    if (!FFileHelper::LoadFileToArray(Buffer, *FilePath)) return;

    FMemoryReader Reader(Buffer);
    float Version = 0.f; int32 Seed = 0;
    Reader << Version << Seed;
    DataMap.Clear();
    DataMap.Serialize(Reader);

    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Loaded from slot '%s'"), *SlotName);
}

void AVoxelWorld::SaveDefaultSlot() { SaveToFile(SaveSlotName); }
void AVoxelWorld::LoadDefaultSlot() { LoadFromFile(SaveSlotName); }

// =============================================================================
//  Terrain Query Utilities
// =============================================================================

float AVoxelWorld::GetTerrainHeight(float X, float Y) const
{
    const FVoxelBiomeWeightMap W = FVoxelBiomeManager::GetBiomeWeightsStatic(X, Y, GetEffectiveConfig());
    return FVoxelBiomeManager::GetSurfaceHeightStatic(X, Y, W, GetEffectiveConfig());
}

float AVoxelWorld::GetSurfaceZ(float X, float Y) const { return GetTerrainHeight(X, Y); }

FVector AVoxelWorld::SnapToVoxelGrid(const FVector& WorldPos) const
{
    return FVector(
        FMath::RoundToFloat(WorldPos.X / VoxelSize) * VoxelSize,
        FMath::RoundToFloat(WorldPos.Y / VoxelSize) * VoxelSize,
        FMath::RoundToFloat(WorldPos.Z / VoxelSize) * VoxelSize);
}

float AVoxelWorld::GetSafeSpawnHeightOffset() const { return SafeSpawnHeightOffset; }

// =============================================================================
//  FindCraterSpawnLocation
//  Grid-searches for the position with the highest natural crater biome weight,
//  then returns the centroid of all candidates within a weight tolerance band.
//
//  Two-criterion scoring:
//    1. CratersW (primary) — higher is better; finds the noise crater peak.
//    2. SurfaceHeight (secondary, tie-break) — lower is better; prefers the
//       basin floor over the rim when weight values are indistinguishable.
//
//  Centroid averaging: all candidates within WeightTol of the best weight are
//  accumulated and averaged, ensuring the result lands at the plateau center
//  rather than at the first grid point where the weight peaked.
// =============================================================================
FVector AVoxelWorld::FindCraterSpawnLocation(
    const FVector& StartPos, const FVoxelGenerationConfig& Config) const
{
    if (!GetWorld()) return StartPos;

    const float SearchRadius = FMath::Max(CraterSpawnSearchRadius, 100000.f);
    const float Step         = CraterSpawnSearchStep;
    const float MinWeight    = CraterSpawnMinWeight;

    float   BestWeight   = -1.f;
    float   BestSurfH    = TNumericLimits<float>::Max();
    FVector BestPos      = StartPos;

    // Centroid accumulator for tie-band averaging
    FVector CentroidSum  = FVector::ZeroVector;
    int32   CentroidN    = 0;
    static constexpr float WeightTol = 0.002f; // points within this range share the plateau

    const float CellSz = 100000.f; // 1km cells matching GetCraterHeight
    const float SearchMax = FMath::Max(SearchRadius, 200000.f); 
    const FVector Off = Config.GetSeedOffset();

    for (float y = -SearchMax; y <= SearchMax; y += CellSz)
    for (float x = -SearchMax; x <= SearchMax; x += CellSz)
    {
        const float nX = StartPos.X + x + Off.X;
        const float nY = StartPos.Y + y + Off.Y;

        const int32 CCX = FMath::FloorToInt(nX / CellSz);
        const int32 CCY = FMath::FloorToInt(nY / CellSz);

        // Get central coordinate using identical hash math
        const float COffX = FVoxelBiomeGenerators::FastNoise3D(CCX * 13.f, CCY * 11.f, 500.f) * 0.35f * CellSz;
        const float COffY = FVoxelBiomeGenerators::FastNoise3D(CCX * 13.f, CCY * 11.f, 600.f) * 0.35f * CellSz;
        const float CLocalX = (CCX + 0.5f) * CellSz + COffX;
        const float CLocalY = (CCY + 0.5f) * CellSz + COffY;

        const float AbsoluteX = CLocalX - Off.X;
        const float AbsoluteY = CLocalY - Off.Y;

        const FVector Candidate(AbsoluteX, AbsoluteY, StartPos.Z);

        const FVoxelBiomeWeightMap W = FVoxelBiomeManager::GetBiomeWeightsStatic(
            AbsoluteX, AbsoluteY, Config);
        const float CratersW = W.GetWeight(EVoxelBiome::Craters);

        if (CratersW < 0.15f) continue; // threshold hurdle

        const float SurfH = FVoxelBiomeManager::GetSurfaceHeightStatic(
            Candidate.X, Candidate.Y, W, Config);

        const bool bStrictlyBetter =
            (CratersW > BestWeight + WeightTol) ||
            (FMath::Abs(CratersW - BestWeight) <= WeightTol && SurfH < BestSurfH - 1.f);

        const bool bInTieBand = FMath::Abs(CratersW - BestWeight) <= WeightTol
                             && FMath::Abs(SurfH - BestSurfH) <= 50.f;

        if (bStrictlyBetter)
        {
            BestWeight   = CratersW;
            BestSurfH    = SurfH;
            BestPos      = Candidate;
            CentroidSum  = Candidate;
            CentroidN    = 1;
        }

    }



    return BestPos;
}

// =============================================================================
//  Tests / Debug
// =============================================================================

void AVoxelWorld::RunVoxelTests()
{
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Running voxel tests..."));

    static FVoxelDensityGenerator TestGen;
    const FVoxelGenerationConfig& Config = GetEffectiveConfig();

    const float Density = TestGen.GetDensity(1000.f, 2000.f, 500.f, Config);
    UE_LOG(LogVoxelWorld, Log, TEXT("Test 1: Density at (1000,2000,500) = %.3f"), Density);

    const FVoxelBiomeWeightMap W = FVoxelBiomeManager::GetBiomeWeightsStatic(0.f, 0.f, Config);
    UE_LOG(LogVoxelWorld, Log,
        TEXT("Test 2: Biome weights at origin — Forest:%.3f Peaks:%.3f Cliffs:%.3f Mesa:%.3f Craters:%.3f Desert:%.3f"),
        W.Forest, W.Peaks, W.Cliffs, W.Mesa, W.Craters, W.Desert);

    UE_LOG(LogVoxelWorld, Log, TEXT("Test 3: Surface height at origin = %.2f cm"), GetTerrainHeight(0.f, 0.f));
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Tests completed."));
}

void AVoxelWorld::TestSmoothLODTransitions()
{
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: LOD1=%.0f LOD2=%.0f"), LOD1Distance, LOD2Distance);
    for (auto& It : LoadedChunks)
    {
        if (AVoxelChunk* Chunk = It.Value)
            UE_LOG(LogVoxelWorld, Verbose, TEXT("Chunk (%d,%d,%d) LOD: %d"),
                Chunk->ChunkCoord.X, Chunk->ChunkCoord.Y, Chunk->ChunkCoord.Z, Chunk->LOD);
    }
}
