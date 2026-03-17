// VoxelWorld_Modification.cpp
// 
// Implementation of voxel modification and utility functions for AVoxelWorld.
// This file contains modification-related function implementations to reduce
// the size of VoxelWorld.cpp and improve maintainability.
//
// ARCHITECTURE OVERVIEW:
// This module handles all player-driven modifications to the voxel world,
// including terrain editing, data persistence, and utility functions for
// world interaction. It provides a clean separation between world management
// and modification logic.
//
// KEY FEATURES:
// - Spherical voxel editing with configurable density values
// - Persistent modification storage and loading
// - Coordinate transformation utilities
// - Biome-aware spawn location finding
// - Comprehensive testing and debugging tools
//
// PERFORMANCE CHARACTERISTICS:
// - Efficient sparse data storage for modifications
// - Binary serialization for fast save/load operations
// - Chunk dirty tracking for selective regeneration
// - Grid-based search algorithms for spawn location finding

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
	// Safety check: ensure world is valid and not shutting down
	if (!GetWorld() || bShutdown) return;

	// Log modification for debugging and performance tracking
	UE_LOG(LogVoxelWorld, Verbose, TEXT("VoxelWorld: SetVoxelSphere at %s, Radius=%.2f, Density=%.2f"),
		*WorldPosition.ToString(), Radius, DensityValue);
	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("SetVoxelSphere: pos=%s radius=%.2f density=%.2f"),
		*WorldPosition.ToString(), Radius, DensityValue));

	// Apply spherical modification to the data map
	// FIX: DataMap uses pure world-space coordinates (no actor offset).
	// The density generator also samples in world space, so they match.
	// Previously the coordinate spaces were consistent but the chunk dirty
	// calculation below was using WorldToChunkCoord which DOES account for
	// the actor anchor -- so we must use WorldToChunkCoord for dirty marking,
	// not manual arithmetic that assumed the actor is at (0,0,0).
	DataMap.SetSphere(WorldPosition, Radius, DensityValue, VoxelSize);

	// Mark affected chunks as dirty for regeneration if requested
	if (bRebuildChunks)
	{
		// Calculate chunk coordinates that contain the modified sphere
		// FIX: Use WorldToChunkCoord which correctly accounts for GetActorLocation()
		// as the anchor. The old manual arithmetic subtracted Anchor from WorldPosition
		// but DataMap.SetSphere wrote voxels WITHOUT subtracting Anchor, so the dirty
		// chunk range was offset from the actual modified chunks.
		const FIntVector MinChunkCoord = WorldToChunkCoord(WorldPosition - FVector(Radius));
		const FIntVector MaxChunkCoord = WorldToChunkCoord(WorldPosition + FVector(Radius));

		// Mark all chunks within the bounding box as dirty
		for (int32 z = MinChunkCoord.Z; z <= MaxChunkCoord.Z; ++z)
		for (int32 y = MinChunkCoord.Y; y <= MaxChunkCoord.Y; ++y)
		for (int32 x = MinChunkCoord.X; x <= MaxChunkCoord.X; ++x)
		{
			const FIntVector Coord(x, y, z);
			if (AVoxelChunk** ChunkPtr = LoadedChunks.Find(Coord))
			{
				if (AVoxelChunk* Chunk = *ChunkPtr)
					Chunk->bMeshDirty = true;
			}
		}
	}
}

void AVoxelWorld::ClearModifications()
{
	// Clear all player modifications from the data map
	// This resets the world to its generated state
	DataMap.Clear();
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: All modifications cleared"));
}


void AVoxelWorld::SaveToFile(const FString& SlotName)
{
	// Safety check: ensure world is valid
	if (!GetWorld()) return;

	// Create save directory if it doesn't exist
	const FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VoxelSaves"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (!PlatformFile.DirectoryExists(*SaveDir))
	{
		PlatformFile.CreateDirectoryTree(*SaveDir);
	}

	// Generate file path with world name and slot name
	const FString FilePath = FPaths::Combine(SaveDir, FString::Printf(TEXT("%s_%s.sav"), *GetName(), *SlotName));

	// -- ⚡ HIGH SPEED BINARY SAVING --
	// Use binary serialization for optimal performance and smaller file sizes
	FBufferArchive ToBuffer;
	
	// Write version header for future compatibility
	float Version = 1.0f;
	int32 Seed = GetEffectiveConfig().Seed;
	ToBuffer << Version;
	ToBuffer << Seed;

	// Serialize the sparse data map containing all modifications
	DataMap.Serialize(ToBuffer);

	// Save binary data to file
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
	// Safety check: ensure world is valid
	if (!GetWorld()) return;

	// Construct file path
	const FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VoxelSaves"));
	const FString FilePath = FPaths::Combine(SaveDir, FString::Printf(TEXT("%s_%s.sav"), *GetName(), *SlotName));

	// Check if file exists
	if (!FPaths::FileExists(FilePath)) return;

	// Load binary data from file
	TArray<uint8> FromBuffer;
	if (FFileHelper::LoadFileToArray(FromBuffer, *FilePath))
	{
		// Create memory reader for deserialization
		FMemoryReader FromBufferReader(FromBuffer);

		// Read version header for compatibility checking
		float Version = 0.0f;
		int32 Seed = 0;
		FromBufferReader << Version;
		FromBufferReader << Seed;

		// Clear existing modifications and load new ones
		DataMap.Clear();
		DataMap.Serialize(FromBufferReader);

		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Loaded modifications from slot '%s'"), *SlotName);
	}
}

void AVoxelWorld::SaveDefaultSlot()
{
	// Save modifications to the currently configured slot
	SaveToFile(SaveSlotName);
}

void AVoxelWorld::LoadDefaultSlot()
{
	// Load modifications from the currently configured slot
	LoadFromFile(SaveSlotName);
}

// ============================================================
//  Utility Functions Implementation
// ============================================================

float AVoxelWorld::GetTerrainHeight(float X, float Y) const
{
	// Get biome weights at the specified coordinates
	const FVoxelBiomeWeightMap Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(X, Y, GetEffectiveConfig());
	
	// Calculate surface height based on biome weights and configuration
	return FVoxelBiomeManager::GetSurfaceHeightStatic(X, Y, Weights, GetEffectiveConfig());
}

float AVoxelWorld::GetSurfaceZ(float X, float Y) const
{
	// Convenience function that delegates to GetTerrainHeight
	return GetTerrainHeight(X, Y);
}

FVector AVoxelWorld::SnapToVoxelGrid(const FVector& WorldPos) const
{
	// Snap world position to the nearest voxel boundary
	// This ensures consistent positioning relative to the voxel grid
	const float SnappedX = FMath::RoundToFloat(WorldPos.X / VoxelSize) * VoxelSize;
	const float SnappedY = FMath::RoundToFloat(WorldPos.Y / VoxelSize) * VoxelSize;
	const float SnappedZ = FMath::RoundToFloat(WorldPos.Z / VoxelSize) * VoxelSize;

	return FVector(SnappedX, SnappedY, SnappedZ);
}

FVector AVoxelWorld::FindCraterSpawnLocation(const FVector& StartPos, const FVoxelGenerationConfig& Config) const
{
	// Safety check: ensure world is valid
	if (!GetWorld()) return StartPos;

	// Configuration parameters for crater search
	const float SearchRadius = CraterSpawnSearchRadius;
	const float Step = CraterSpawnSearchStep;
	const float MinWeight = CraterSpawnMinWeight;

	// Initialize search with starting position
	FVector BestPos = StartPos;
	float BestWeight = -1.0f;

	// Search in a grid pattern around the start position
	// This provides comprehensive coverage while maintaining performance
	for (float y = -SearchRadius; y <= SearchRadius; y += Step)
	{
		for (float x = -SearchRadius; x <= SearchRadius; x += Step)
		{
			// Generate candidate position
			FVector Candidate = FVector(StartPos.X + x, StartPos.Y + y, StartPos.Z);

			// Get biome weights at this position to determine crater likelihood
			FVoxelBiomeWeightMap Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(Candidate.X, Candidate.Y, Config);
			float CraterWeight = Weights.GetWeight(EVoxelBiome::Craters);

			// Update best position if this candidate has higher crater weight
			// and meets the minimum weight threshold
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
	// FIX: Use the configurable SafeSpawnHeightOffset property (default 1500cm).
	// The old hardcoded 350cm placed the player's feet below the terrain surface
	// because the surface height from noise is the top of solid voxels, and
	// 350cm is less than one voxel height (100cm * capsule half-height 96cm = ~196cm
	// minimum needed just to stand). 1500cm gives comfortable clearance.
	return SafeSpawnHeightOffset;
}

// ============================================================
//  Testing and Debug Implementation
// ============================================================

void AVoxelWorld::RunVoxelTests()
{
	// Execute comprehensive voxel engine tests for debugging and validation
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Running voxel tests..."));

	// Test 1: Check if density generator is working correctly
	{
		static FVoxelDensityGenerator TestGen;
		const FVoxelGenerationConfig& Config = GetEffectiveConfig();

		// Test density calculation at a specific world position
		float TestX = 1000.f, TestY = 2000.f, TestZ = 500.f;
		float Density = TestGen.GetDensity(TestX, TestY, TestZ, Config);

		UE_LOG(LogVoxelWorld, Log, TEXT("Test 1: Density at (%.0f, %.0f, %.0f) = %.3f"),
			TestX, TestY, TestZ, Density);
	}

	// Test 2: Check biome weight distribution
	{
		// Test biome weight calculation at world origin
		const FVoxelBiomeWeightMap Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(0.f, 0.f, GetEffectiveConfig());
		UE_LOG(LogVoxelWorld, Log, TEXT("Test 2: Biome weights at origin - Forest:%.3f Peaks:%.3f Cliffs:%.3f Mesa:%.3f Craters:%.3f Desert:%.3f"),
			Weights.Forest, Weights.Peaks, Weights.Cliffs, Weights.Mesa, Weights.Craters, Weights.Desert);
	}

	// Test 3: Check surface height calculation
	{
		// Test surface height calculation at world origin
		float SurfaceH = GetTerrainHeight(0.f, 0.f);
		UE_LOG(LogVoxelWorld, Log, TEXT("Test 3: Surface height at origin = %.2f cm"), SurfaceH);
	}

	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Tests completed."));
}

void AVoxelWorld::TestSmoothLODTransitions()
{
	// Test smooth LOD transition functionality
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Testing smooth LOD transitions..."));

	// Log LOD configuration for debugging
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
