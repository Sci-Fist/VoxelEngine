// VoxelWorldModification.cpp
// FIX N5 — FindCraterSpawnLocation centroid averaging was dead code.
//           CentroidSum/CentroidN were computed but the function returned
//           BestPos (single point) every time. Now returns the centroid
//           of all points within WeightTol of the peak, placing the player
//           at the centre of the crater plateau rather than at one edge.
//
// Other functions unchanged.

#include "VoxelWorld.h"
#include "Voxel/Core/VoxelChunk.h"
#include "Voxel/Core/VoxelDataMap.h"
#include "Voxel/Generation/VoxelDensityGenerator.h"
#include "Voxel/Biomes/VoxelBiomeManager.h"
#include "Voxel/Biomes/VoxelBiomeGenerators.h"
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
void AVoxelWorld::SetVoxelSphere(FVector WorldPosition, float Radius,
                                  float DensityValue, bool bRebuildChunks)
{
    if (!GetWorld() || bShutdown) return;
    DataMap.SetSphere(WorldPosition, Radius, DensityValue, VoxelSize, GetActorLocation());
    if (bRebuildChunks)
    {
        const FIntVector MinC = WorldToChunkCoord(WorldPosition - FVector(Radius));
        const FIntVector MaxC = WorldToChunkCoord(WorldPosition + FVector(Radius));
        for (int32 z=MinC.Z;z<=MaxC.Z;++z)
        for (int32 y=MinC.Y;y<=MaxC.Y;++y)
        for (int32 x=MinC.X;x<=MaxC.X;++x)
            if (LoadedChunks.Contains(FIntVector(x,y,z)))
                MarkChunkDirty(FIntVector(x,y,z));
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
    FBufferArchive Buf;
    float Version = 1.f; int32 Seed = GetEffectiveConfig().Seed; // temporary — OK
    Buf << Version << Seed;
    DataMap.Serialize(Buf);
    if (FFileHelper::SaveArrayToFile(Buf, *FilePath))
    {
        UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Saved to '%s'"), *SlotName);
    }
    else
    {
        UE_LOG(LogVoxelWorld, Error, TEXT("VoxelWorld: Failed to save '%s'"), *SlotName);
    }
}

void AVoxelWorld::LoadFromFile(const FString& SlotName)
{
    if (!GetWorld()) return;
    const FString SaveDir  = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VoxelSaves"));
    const FString FilePath = FPaths::Combine(SaveDir,
        FString::Printf(TEXT("%s_%s.sav"), *GetName(), *SlotName));
    if (!FPaths::FileExists(FilePath)) return;
    TArray<uint8> Buf;
    if (!FFileHelper::LoadFileToArray(Buf, *FilePath)) return;
    FMemoryReader Reader(Buf);
    float Version=0.f; int32 Seed=0; Reader << Version << Seed;
    DataMap.Clear(); DataMap.Serialize(Reader);
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Loaded from '%s'"), *SlotName);
}

void AVoxelWorld::SaveDefaultSlot() { SaveToFile(SaveSlotName); }
void AVoxelWorld::LoadDefaultSlot() { LoadFromFile(SaveSlotName); }

// =============================================================================
//  Terrain query helpers
// =============================================================================
float AVoxelWorld::GetTerrainHeight(float X, float Y) const
{
    const FVoxelGenerationConfig Cfg = GetEffectiveConfig(); // value copy — FIX #30
    const FVoxelBiomeWeightMap W = FVoxelBiomeManager::GetBiomeWeightsStatic(X, Y, Cfg);
    return FVoxelBiomeManager::GetSurfaceHeightStatic(X, Y, W, Cfg);
}
float AVoxelWorld::GetSurfaceZ(float X, float Y) const { return GetTerrainHeight(X, Y); }

FVector AVoxelWorld::SnapToVoxelGrid(const FVector& P) const
{
    return FVector(
        FMath::RoundToFloat(P.X/VoxelSize)*VoxelSize,
        FMath::RoundToFloat(P.Y/VoxelSize)*VoxelSize,
        FMath::RoundToFloat(P.Z/VoxelSize)*VoxelSize);
}
float AVoxelWorld::GetSafeSpawnHeightOffset() const { return SafeSpawnHeightOffset; }

// =============================================================================
//  FindCraterSpawnLocation
//  FIX N5: Centroid averaging now actually used in the return value.
//  Previously CentroidSum/CentroidN were accumulated but BestPos was always
//  returned, placing the player at the first-found peak pixel rather than
//  the geometric centre of the crater's biome-weight plateau.
//  Now: all candidates within WeightTol of the best are averaged → returns
//  the centroid of the high-weight region (i.e. the middle of the crater).
// =============================================================================
FVector AVoxelWorld::FindCraterSpawnLocation(
    const FVector& StartPos, const FVoxelGenerationConfig& Config) const
{
    if (!GetWorld()) return StartPos;

    TRACE_CPUPROFILER_EVENT_SCOPE(AVoxelWorld::FindCraterSpawnLocation);

    const float SearchMax  = FMath::Max(CraterSpawnSearchRadius, 10000.f);
    const float SearchStep = FMath::Max(CraterSpawnSearchStep, 2000.f); 
    const float MinWeight  = CraterSpawnMinWeight;
    const float EarlyOutW  = 0.85f; // "Excellent" crater found, stop here
    const FVector Off      = Config.GetSeedOffset();

    float   BestWeight  = -1.f;
    float   BestSurfH   = TNumericLimits<float>::Max();
    FVector BestPos     = StartPos;

    FVector CentroidSum = FVector::ZeroVector;
    int32   CentroidN   = 0;
    static constexpr float WeightTol = 0.005f;

    // ── Spiral Search ────────────────────────────────────────────────────────
    // Prioritizes craters closest to the player's intended start position.
    int32 x = 0, y = 0, dx = 0, dy = -1;
    const int32 NumSteps = FMath::CeilToInt(SearchMax / SearchStep);
    const int32 MaxSamples = 2000; // Physical safety limit (approx 45x45 grid)
    
    for (int32 i = 0; i < (NumSteps * 2 + 1) * (NumSteps * 2 + 1); ++i)
    {
        if (i >= MaxSamples) break;

        const float CandidateX = StartPos.X + (x * SearchStep);
        const float CandidateY = StartPos.Y + (y * SearchStep);
        
        // Random jitter within the cell to avoid aliasing artifacts in the noise lookup
        const int32 CX = FMath::FloorToInt((CandidateX + Off.X) / SearchStep);
        const int32 CY = FMath::FloorToInt((CandidateX + Off.Y) / SearchStep);
        const float JX = FVoxelBiomeGenerators::FastNoise3D(CX * 17.f, CY * 13.f, 500.f) * 0.4f * SearchStep;
        const float JY = FVoxelBiomeGenerators::FastNoise3D(CX * 17.f, CY * 13.f, 600.f) * 0.4f * SearchStep;

        const FVector Candidate(CandidateX + JX, CandidateY + JY, StartPos.Z);

        FVoxelGenerationConfig TempCfg = Config;
        TempCfg.Craters.ForcedCraterCenter = FVector2D(Candidate.X, Candidate.Y);

        const FVoxelBiomeWeightMap W = FVoxelBiomeManager::GetBiomeWeightsStatic(Candidate.X, Candidate.Y, TempCfg);
        const float CratersW = W.GetWeight(EVoxelBiome::Craters);

        if (CratersW > 0.15f)
        {
            const float SurfH = FVoxelBiomeManager::GetSurfaceHeightStatic(Candidate.X, Candidate.Y, W, TempCfg);

            // Avoid seleccionar craters that plunge below Sea Level (flooded spawn basins)
            if (SurfH >= Config.SeaLevel + 500.f) 
            {
                const bool bStrictlyBetter =
                    (CratersW > BestWeight + WeightTol) ||
                    (FMath::Abs(CratersW - BestWeight) <= WeightTol && SurfH < BestSurfH - 1.f);

                const bool bInTieBand =
                    FMath::Abs(CratersW - BestWeight) <= WeightTol &&
                    FMath::Abs(SurfH - BestSurfH) <= 100.f;

                if (bStrictlyBetter)
                {
                    BestWeight  = CratersW;
                    BestSurfH   = SurfH;
                    BestPos     = Candidate;
                    CentroidSum = Candidate;
                    CentroidN   = 1;

                    // ── Early Out ──────────────────────────────────────────
                    // If we found a localized peak that is highly likely to 
                    // be a great crater, stop searching further rings.
                    if (BestWeight >= EarlyOutW) break;
                }
                else if (bInTieBand)
                {
                    CentroidSum += Candidate;
                    CentroidN++;
                }
            }
        }

        // Spiral progression
        if (x == y || (x < 0 && x == -y) || (x > 0 && x == 1 - y))
        {
            int32 temp = dx; dx = -dy; dy = temp;
        }
        x += dx; y += dy;
    }

    if (BestWeight < MinWeight) return StartPos;

    // ── Centroid Averaging (FIX N5 - Finalized) ─────────────────────────────
    // If we have a cluster of samples near the crater peak, return their 
    // center of mass. This places the player in the middle of the basin 
    // plateau rather than on a potentially sharp edge of the detection grid.
    if (CentroidN > 1)
    {
        FVector Centroid = CentroidSum / (float)CentroidN;
        Centroid.Z = StartPos.Z; // Preserve Z for later terrain height lookup
        return Centroid;
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
    const FVoxelGenerationConfig Cfg = GetEffectiveConfig(); // FIX #30: value copy
    UE_LOG(LogVoxelWorld, Log, TEXT("Test 1: Density at (1000,2000,500) = %.3f"),
        TestGen.GetDensity(1000.f, 2000.f, 500.f, Cfg));
    const FVoxelBiomeWeightMap W = FVoxelBiomeManager::GetBiomeWeightsStatic(0.f, 0.f, Cfg);
    UE_LOG(LogVoxelWorld, Log,
        TEXT("Test 2: Biomes at origin — Forest:%.3f Peaks:%.3f Cliffs:%.3f Mesa:%.3f Craters:%.3f Desert:%.3f"),
        W.Forest, W.Peaks, W.Cliffs, W.Mesa, W.Craters, W.Desert);
    UE_LOG(LogVoxelWorld, Log, TEXT("Test 3: Surface height at origin = %.2f cm"), GetTerrainHeight(0.f, 0.f));
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Tests completed."));
}

void AVoxelWorld::TestSmoothLODTransitions()
{
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: LOD1=%.0f LOD2=%.0f"), LOD1Distance, LOD2Distance);
}
