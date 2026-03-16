// VoxelWorld_Modification.cpp
// Implementation of voxel modification and utility functions for AVoxelWorld.
// This file contains modification-related function implementations to reduce
// the size of VoxelWorld.cpp and improve maintainability.

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


// ============================================================
//  Voxel Modification Implementation
// ============================================================

void AVoxelWorld::SetVoxelSphere(FVector WorldPosition, float Radius, float DensityValue, bool bRebuildChunks)
{
	if (!GetWorld() || bShutdown) return;

	UE_LOG(LogVoxelWorld, Verbose, TEXT("VoxelWorld: SetVoxelSphere at %s, Radius=%.2f, Density=%.2f"),
		*WorldPosition.ToString(), Radius, DensityValue);
	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("SetVoxelSphere: pos=%s radius=%.2f density=%.2f"),
		*WorldPosition.ToString(), Radius, DensityValue));

	DataMap.SetSphere(WorldPosition, Radius, DensityValue, VoxelSize);


	// If rebuild is requested, mark affected chunks as dirty
	if (bRebuildChunks)
	{
		const FVector Anchor = GetActorLocation();

		// Find all chunks that intersect with the modified region
		const FIntVector MinChunkCoord = FIntVector(
			FMath::FloorToInt((WorldPosition.X - Radius - Anchor.X) / (ChunkSize * VoxelSize)),
			FMath::FloorToInt((WorldPosition.Y - Radius - Anchor.Y) / (ChunkSize * VoxelSize)),
			FMath::FloorToInt((WorldPosition.Z - Radius - Anchor.Z) / (ChunkSize * VoxelSize))
		);
		const FIntVector MaxChunkCoord = FIntVector(
			FMath::CeilToInt((WorldPosition.X + Radius - Anchor.X) / (ChunkSize * VoxelSize)),
			FMath::CeilToInt((WorldPosition.Y + Radius - Anchor.Y) / (ChunkSize * VoxelSize)),
			FMath::CeilToInt((WorldPosition.Z + Radius - Anchor.Z) / (ChunkSize * VoxelSize))
		);

		// Mark each affected chunk as dirty
		for (int32 z = MinChunkCoord.Z; z <= MaxChunkCoord.Z; ++z)
		{
			for (int32 y = MinChunkCoord.Y; y <= MaxChunkCoord.Y; ++y)
			{
				for (int32 x = MinChunkCoord.X; x <= MaxChunkCoord.X; ++x)
				{
					const FIntVector ChunkCoord(x, y, z);
					if (AVoxelChunk** ChunkPtr = LoadedChunks.Find(ChunkCoord))
					{
						if (AVoxelChunk* Chunk = *ChunkPtr)
						{
							Chunk->bMeshDirty = true;
						}
					}
				}
			}
		}
	}
}

void AVoxelWorld::ClearModifications()
{
	DataMap.Clear();
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: All modifications cleared"));
}


void AVoxelWorld::SaveToFile(const FString& SlotName)
{
	if (!GetWorld()) return;

	const FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VoxelSaves"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (!PlatformFile.DirectoryExists(*SaveDir))
	{
		PlatformFile.CreateDirectoryTree(*SaveDir);
	}

	const FString FilePath = FPaths::Combine(SaveDir, FString::Printf(TEXT("%s_%s.sav"), *GetName(), *SlotName));

	// -- ⚡ HIGH SPEED BINARY SAVING --
	FBufferArchive ToBuffer;
	
	float Version = 1.0f;
	int32 Seed = GetEffectiveConfig().Seed;
	ToBuffer << Version;
	ToBuffer << Seed;

	// Serialize sparse data map
	DataMap.Serialize(ToBuffer);

	if (FFileHelper::SaveArrayToFile(ToBuffer, *FilePath))
	{
		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Successfully saved modifications to slot '%s'"), *SlotName);
	}
	else
	{
		UE_LOG(LogVoxelWorld, Error, TEXT("VoxelWorld: Failed to save modifications to slot '%s'"), *SlotName);
	}
}

void AVoxelWorld::LoadFromFile(const FString& SlotName)
{
	if (!GetWorld()) return;

	const FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VoxelSaves"));
	const FString FilePath = FPaths::Combine(SaveDir, FString::Printf(TEXT("%s_%s.sav"), *GetName(), *SlotName));

	if (!FPaths::FileExists(FilePath)) return;

	TArray<uint8> FromBuffer;
	if (FFileHelper::LoadFileToArray(FromBuffer, *FilePath))
	{
		FMemoryReader FromBufferReader(FromBuffer);

		// Read Header/Version
		float Version = 0.0f;
		int32 Seed = 0;
		FromBufferReader << Version;
		FromBufferReader << Seed;

		// Serialize sparse data map
		DataMap.Clear();
		DataMap.Serialize(FromBufferReader);

		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Loaded modifications from slot '%s'"), *SlotName);
	}
}

void AVoxelWorld::SaveDefaultSlot()
{
	SaveToFile(TEXT("DefaultSlot"));
}

void AVoxelWorld::LoadDefaultSlot()
{
	LoadFromFile(TEXT("DefaultSlot"));
}

// ============================================================
//  Utility Functions Implementation
// ============================================================

float AVoxelWorld::GetTerrainHeight(float X, float Y) const
{
	const FVoxelBiomeWeightMap Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(X, Y, GetEffectiveConfig());
	return FVoxelBiomeManager::GetSurfaceHeightStatic(X, Y, Weights, GetEffectiveConfig());
}

float AVoxelWorld::GetSurfaceZ(float X, float Y) const
{
	return GetTerrainHeight(X, Y);
}

FVector AVoxelWorld::SnapToVoxelGrid(const FVector& WorldPos) const
{
	const float SnappedX = FMath::RoundToFloat(WorldPos.X / VoxelSize) * VoxelSize;
	const float SnappedY = FMath::RoundToFloat(WorldPos.Y / VoxelSize) * VoxelSize;
	const float SnappedZ = FMath::RoundToFloat(WorldPos.Z / VoxelSize) * VoxelSize;

	return FVector(SnappedX, SnappedY, SnappedZ);
}

FVector AVoxelWorld::FindCraterSpawnLocation(const FVector& StartPos, const FVoxelGenerationConfig& Config) const
{
	if (!GetWorld()) return StartPos;

	const float SearchRadius = CraterSpawnSearchRadius;
	const float Step = CraterSpawnSearchStep;
	const float MinWeight = CraterSpawnMinWeight;


	FVector BestPos = StartPos;
	float BestWeight = -1.0f;

	// Search in a grid pattern around the start position
	for (float y = -SearchRadius; y <= SearchRadius; y += Step)
	{
		for (float x = -SearchRadius; x <= SearchRadius; x += Step)
		{
			FVector Candidate = FVector(StartPos.X + x, StartPos.Y + y, StartPos.Z);

			// Get biome weights at this position
			FVoxelBiomeWeightMap Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(Candidate.X, Candidate.Y, Config);
			float CraterWeight = Weights.GetWeight(EVoxelBiome::Craters);

			if (CraterWeight > BestWeight && CraterWeight >= MinWeight)
			{
				BestWeight = CraterWeight;
				BestPos = Candidate;
			}
		}
	}

	return BestPos;
}

float AVoxelWorld::GetSafeSpawnHeightOffset() const
{
	// This can be made configurable if needed
	return 350.0f;
}

// ============================================================
//  Testing and Debug Implementation
// ============================================================

void AVoxelWorld::RunVoxelTests()
{
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Running voxel tests..."));

	// Test 1: Check if density generator is working
	{
		static FVoxelDensityGenerator TestGen;
		const FVoxelGenerationConfig& Config = GetEffectiveConfig();

		float TestX = 1000.f, TestY = 2000.f, TestZ = 500.f;
		float Density = TestGen.GetDensity(TestX, TestY, TestZ, Config);

		UE_LOG(LogVoxelWorld, Log, TEXT("Test 1: Density at (%.0f, %.0f, %.0f) = %.3f"),
			TestX, TestY, TestZ, Density);
	}

	// Test 2: Check biome weights
	{
		const FVoxelBiomeWeightMap Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(0.f, 0.f, GetEffectiveConfig());
		UE_LOG(LogVoxelWorld, Log, TEXT("Test 2: Biome weights at origin - Forest:%.3f Peaks:%.3f Cliffs:%.3f Mesa:%.3f Craters:%.3f Desert:%.3f"),
			Weights.Forest, Weights.Peaks, Weights.Cliffs, Weights.Mesa, Weights.Craters, Weights.Desert);
	}

	// Test 3: Check surface height
	{
		float SurfaceH = GetTerrainHeight(0.f, 0.f);
		UE_LOG(LogVoxelWorld, Log, TEXT("Test 3: Surface height at origin = %.2f cm"), SurfaceH);
	}

	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Tests completed."));
}

void AVoxelWorld::TestSmoothLODTransitions()
{
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Testing smooth LOD transitions..."));

	// This test would create a test chunk and verify LOD transitions
	// For now, just log the configuration
	UE_LOG(LogVoxelWorld, Log, TEXT("LOD1Distance: %.2f, LOD2Distance: %.2f"), LOD1Distance, LOD2Distance);

	// Test each loaded chunk's LOD transition capability
	for (auto& It : LoadedChunks)
	{
		if (AVoxelChunk* Chunk = It.Value)
		{
			if (IsValid(Chunk))
			{
				UE_LOG(LogVoxelWorld, Verbose, TEXT("Chunk (%d,%d,%d) current LOD: %d"),
					Chunk->ChunkCoord.X, Chunk->ChunkCoord.Y, Chunk->ChunkCoord.Z, Chunk->LOD);
			}
		}
	}

	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: LOD transition test completed."));
}
