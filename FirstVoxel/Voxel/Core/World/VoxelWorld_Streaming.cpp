// VoxelWorld_Streaming.cpp
// 
// Implementation of chunk streaming and LOD management for AVoxelWorld.
// This file contains streaming-related function implementations to reduce
// the size of VoxelWorld.cpp and improve maintainability.
//
// ARCHITECTURE OVERVIEW:
// This module handles dynamic chunk loading and unloading based on player
// position, implementing a sophisticated streaming system that maintains
// performance while providing seamless world exploration.
//
// KEY FEATURES:
// - Volumetric 3D streaming with player position tracking
// - Separate ground and skylands streaming volumes
// - Dynamic LOD transitions based on distance
// - Performance optimizations for stationary players
// - Efficient chunk queue management with priority sorting
//
// PERFORMANCE CHARACTERISTICS:
// - Movement-based streaming to avoid unnecessary updates
// - Priority-based chunk loading (nearest first)
// - Separate render distances for ground vs skylands
// - Efficient set-based lookups to prevent O(n^2) operations
// - LOD transitions for distant chunks to reduce polygon count

#include "Voxel/Core/World/VoxelWorld.h"
#include "Voxel/Core/VoxelChunk.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "Voxel/Biomes/VoxelBiomeManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Voxel/VoxelLogger.h"

// ============================================================
//  Chunk Streaming Implementation
// ============================================================

void AVoxelWorld::UpdateChunkStreaming()
{
	// Safety checks: ensure player exists and this is a game world
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player || !GetWorld()->IsGameWorld()) 
	{
		return;
	}

	// Throttle streaming updates to prevent excessive CPU usage
	StreamingTimer += GetWorld()->GetDeltaSeconds();
	if (StreamingTimer < StreamingInterval) return;
	StreamingTimer = 0.f;

	// --- ⚡ Optimization: Skip building streaming volumes if player is stationary ---
	// This optimization prevents unnecessary streaming calculations when the player isn't moving
	const FVector CurrentPos = Player->GetActorLocation();
	const float MinStep = ChunkSize * VoxelSize * 0.4f; 
	if (FVector::DistSquared(CurrentPos, LastStreamedPos) < MinStep * MinStep)
	{
		return; // No movement, preserve CPU budget
	}
	LastStreamedPos = CurrentPos;

	// Get player position and convert to chunk coordinates
	FVector PlayerPos = Player->GetActorLocation();
	FIntVector PlayerCoord = WorldToChunkCoord(PlayerPos);

	// Get configuration settings for streaming calculations
	const FVoxelGenerationConfig& Config = GetEffectiveConfig();
	const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
	const float ChunkWorldSize = ChunkSize * VoxelSize;

	// Compute skyland altitude based on current terrain height
	// This ensures skylands are properly positioned above varying terrain elevations
	const FVoxelBiomeManager::FWeightsAndHeight Wh = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(
		PlayerPos.X, PlayerPos.Y, Config);
	const float PlayerSurfH = Wh.SurfaceHeight;

	// Calculate normalized terrain parameters for skyland positioning
	const float HeightNormSky    = FMath::Clamp(PlayerSurfH / SC.MaxTerrainReference, 0.f, 1.f);
	const float RoughnessNormSky = FMath::Clamp(Wh.Weights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);
	const float TerrainStrSky    = FMath::Clamp(HeightNormSky * 1.5f + RoughnessNormSky * 0.8f, 0.f, 1.f);
	const float ShardFalloff     = FMath::Pow(TerrainStrSky, 2.2f); // Match GetSkylandColumnCache()
	const float CurvedH          = FMath::Pow(HeightNormSky,    2.5f);
	const float CurvedR          = FMath::Pow(RoughnessNormSky, 2.0f);
	const float AltBase          = FMath::Lerp(SC.MinAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, TerrainStrSky);
	
	// Calculate final skyland altitude with terrain-based adjustments
	const float SkyAltWorld      = PlayerSurfH + AltBase
		                           + CurvedH * SC.HeightAltitudeBonus
		                           + CurvedR * SC.RoughnessAltitudeBonus
		                           + ShardFalloff * SC.LowTerrainAltitudeBoost;

	// Calculate skyland thickness in chunks with margin
	const float IslandSize    = FMath::Max(SC.BaseIslandSize,
		                          SC.BaseIslandSize + CurvedH * SC.HeightSizeBonus + CurvedR * SC.RoughnessSizeBonus);
	const float HalfThickCm   = FMath::Max(200.f, IslandSize * SC.ThicknessRatio);
	const int32 SkyThickness  = FMath::CeilToInt(HalfThickCm / ChunkWorldSize) + 2;
	const int32 SkyZCoordCenter = FMath::RoundToInt(SkyAltWorld / ChunkWorldSize);
	
	// Build desired chunk set for streaming
	TSet<FIntVector> Desired;

	// 1. Ground area: Standard 3D volume around player
	for (int32 z = -RenderDistanceZ; z <= RenderDistanceZ; ++z)
	for (int32 y = -RenderDistanceXY; y <= RenderDistanceXY; ++y)
	for (int32 x = -RenderDistanceXY; x <= RenderDistanceXY; ++x)
	{
		Desired.Add(PlayerCoord + FIntVector(x, y, z));
	}

	// 2. Skylands Pass: Separate volume for floating islands with different render distance
	// This uses a larger range to maintain distance visibility without overloading ground chunks
	for (int32 z = -SkyThickness; z <= SkyThickness; ++z)
	for (int32 y = -SkylandsRenderDistanceXY; y <= SkylandsRenderDistanceXY; ++y)
	for (int32 x = -SkylandsRenderDistanceXY; x <= SkylandsRenderDistanceXY; ++x)
	{
		Desired.Add(FIntVector(PlayerCoord.X + x, PlayerCoord.Y + y, SkyZCoordCenter + z));
	}

	// Remove chunks that are no longer in the desired set
	TArray<FIntVector> ToRemove;
	for (auto& It : LoadedChunks)
	{
		if (!Desired.Contains(It.Key)) ToRemove.Add(It.Key);
	}
	for (const FIntVector& C : ToRemove) DestroyChunk(C);

	// --- 3. DYNAMIC LOD MULTIPLIERS FOR EXISTING CHUNKS ---
	// Update LOD levels for existing chunks based on distance from player
	for (auto& It : LoadedChunks)
	{
		AVoxelChunk* Chunk = It.Value;
		if (IsValid(Chunk))
		{
			// Calculate distance to chunk center for LOD determination
			FVector ChunkPos = ChunkCoordToWorld(It.Key) + FVector(ChunkSize * VoxelSize * 0.5f);
			float DistSq = FVector::DistSquared(PlayerPos, ChunkPos);

			// Determine target LOD based on distance thresholds
			int32 TargetLOD = 0;
			if (DistSq > LOD2Distance * LOD2Distance) TargetLOD = 2;
			else if (DistSq > LOD1Distance * LOD1Distance) TargetLOD = 1;

			// Apply LOD transition if needed
			if (Chunk->LOD != TargetLOD)
			{
				// Use smooth LOD transition instead of instant rebuild
				Chunk->TransitionToLOD(TargetLOD);
			}
		}
	}

	// Add new desired chunks to generation queue, sorted by distance
	TArray<TPair<int32, FIntVector>> NewChunks;

	// OPTIMIZATION: convert queue to set for O(1) lookup to prevent main-thread freeze with large volumes
	TSet<FIntVector> QueueSet(GenerationQueue);

	// Find chunks that need to be generated
	for (const FIntVector& C : Desired)
	{
		if (!LoadedChunks.Contains(C) && !QueueSet.Contains(C))
		{
			FIntVector Local = C - PlayerCoord;
			int32 DistSq = Local.X*Local.X + Local.Y*Local.Y + Local.Z*Local.Z;
			NewChunks.Add(TPair<int32, FIntVector>(DistSq, C));
		}
	}
	
	// PRIORITY SORT: Ensure nearest chunks land at the HEAD of the queue
	// This ensures the most important chunks are generated first
	NewChunks.Sort([](const TPair<int32,FIntVector>& A, const TPair<int32,FIntVector>& B){ return A.Key < B.Key; });
	
	// Add sorted chunks to generation queue
	const int32 NumNew = NewChunks.Num();
	if (NumNew > 0)
	{
		GenerationQueue.Reserve(GenerationQueue.Num() + NumNew);
		for (int32 i = 0; i < NumNew; ++i)
		{
			GenerationQueue.Add(NewChunks[i].Value);
		}
		UE_LOG(LogVoxelWorld, Verbose, TEXT("VoxelWorld: Streaming added %d new chunks to queue"), NumNew);
	}
}
