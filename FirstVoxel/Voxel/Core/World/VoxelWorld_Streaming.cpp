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

	// FIX: GridSnap the height to 1000cm discrete steps so the SkyZCoordCenter
	// does not shift continuously on slight slope movements, avoiding lag spikes.
	const float RoundedSurfH     = FMath::GridSnap(PlayerSurfH, 1000.f);

	// Calculate normalized terrain parameters for skyland positioning
	const float HeightNormSky    = FMath::Clamp(PlayerSurfH / SC.MaxTerrainReference, 0.f, 1.f);
	const float RoughnessNormSky = FMath::Clamp(Wh.Weights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);
	const float TerrainStrSky    = FMath::Clamp(HeightNormSky * 1.5f + RoughnessNormSky * 0.8f, 0.f, 1.f);
	const float ShardFalloff     = FMath::Pow(TerrainStrSky, 2.2f); // Match GetSkylandColumnCache()
	const float CurvedH          = FMath::Pow(HeightNormSky,    2.5f);
	const float CurvedR          = FMath::Pow(RoughnessNormSky, 2.0f);
	const float AltBase          = FMath::Lerp(SC.MinAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, TerrainStrSky);
	
	// Calculate final skyland altitude with terrain-based adjustments
	const float SkyAltWorld      = RoundedSurfH + AltBase
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

			// Hysteresis bands prevent LOD flip-flopping at borders.
			// Upgrade (lower LOD number = higher detail) only when inside the INNER threshold.
			// Downgrade (higher LOD number = lower detail) only when outside the OUTER threshold.
			static constexpr float HysteresisFactor = 1.10f;
			const float L1I = LOD1Distance;
			const float L1O = LOD1Distance * HysteresisFactor;
			const float L2I = LOD2Distance;
			const float L2O = LOD2Distance * HysteresisFactor;
			const float D   = FMath::Sqrt(DistSq); // single sqrt here, not per-comparison

			int32 TargetLOD = Chunk->LOD;
			if      (Chunk->LOD < 2 && D > L2O) TargetLOD = 2;  // downgrade to LOD2
			else if (Chunk->LOD > 1 && D < L2I) TargetLOD = 1;  // upgrade from LOD2
			else if (Chunk->LOD < 1 && D > L1O) TargetLOD = 1;  // downgrade to LOD1
			else if (Chunk->LOD > 0 && D < L1I) TargetLOD = 0;  // upgrade to full res

			if (TargetLOD != Chunk->LOD)
			{
				Chunk->TransitionToLOD(TargetLOD);
			}
		}
	}

	// ── Add newly desired chunks to the generation queue ──────────────────
	//
	// ── Merge & Re-Sort Generation Queue ──────────────────
	TSet<FIntVector> UniqueMerged;
	UniqueMerged.Reserve(Desired.Num() + (GenerationQueue.Num() - QueueHead));

	// 1. Add newly desired (not yet loaded) chunks
	for (const FIntVector& C : Desired)
	{
		if (!LoadedChunks.Contains(C))
		{
			UniqueMerged.Add(C);
		}
	}

	// 2. Add remaining items already in the queue from past ticks
	for (int32 i = QueueHead; i < GenerationQueue.Num(); ++i)
	{
		UniqueMerged.Add(GenerationQueue[i]);
	}

	// 3. Compute absolute distances and Sort
	TArray<TPair<int32, FIntVector>> SortedQueue;
	SortedQueue.Reserve(UniqueMerged.Num());

	for (const FIntVector& C : UniqueMerged)
	{
		FIntVector Local = C - PlayerCoord;
		const int32 DistSq = Local.X*Local.X + Local.Y*Local.Y + Local.Z*Local.Z;
		SortedQueue.Add(TPair<int32, FIntVector>(DistSq, C));
	}

	// Nearest first so absolute priorites override stale positions FIFO
	SortedQueue.Sort([](const TPair<int32, FIntVector>& A, const TPair<int32, FIntVector>& B) {
		return A.Key < B.Key;
	});

	// 4. Rebuild GenerationQueue
	GenerationQueue.Reset();
	GenerationQueue.Reserve(SortedQueue.Num());
	for (const auto& Pair : SortedQueue)
	{
		GenerationQueue.Add(Pair.Value);
	}
	QueueHead = 0; // Reset queue cursor

	if (SortedQueue.Num() > 0)
	{
		UE_LOG(LogVoxelWorld, Verbose, TEXT("VoxelWorld: Streaming re-sorted %d chunks in generation queue"), SortedQueue.Num());
	}
}
