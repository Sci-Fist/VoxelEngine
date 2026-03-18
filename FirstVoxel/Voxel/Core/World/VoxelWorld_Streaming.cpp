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

	// FIX: StreamingTimer is already incremented in AVoxelWorld::Tick.
	// Double-incrementing here caused streaming to fire at half the intended interval.
	// This function is only called when the timer has already elapsed — just reset it.
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

	// FIX: Cache SkyAlt and only recompute when player moves > SkyAltSnapDist.
	// Previously this ran full biome noise every 0.25s even when the player was
	// standing still on flat terrain.
	if (FVector::DistSquared(PlayerPos, LastSkyAltPos) > SkyAltSnapDist * SkyAltSnapDist)
	{
		LastSkyAltPos = PlayerPos;

		const FVoxelBiomeManager::FWeightsAndHeight Wh = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(
			PlayerPos.X, PlayerPos.Y, Config);
		const float PlayerSurfH   = Wh.SurfaceHeight;
		const float RoundedSurfH  = FMath::GridSnap(PlayerSurfH, 1000.f);

		const float HeightNormSky    = FMath::Clamp(PlayerSurfH / SC.MaxTerrainReference, 0.f, 1.f);
		const float RoughnessNormSky = FMath::Clamp(Wh.Weights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);
		const float TerrainStrSky    = FMath::Clamp(HeightNormSky * 1.5f + RoughnessNormSky * 0.8f, 0.f, 1.f);
		CachedCurvedH               = FMath::Pow(HeightNormSky,    2.5f);
		CachedCurvedR               = FMath::Pow(RoughnessNormSky, 2.0f);
		const float AltBase          = FMath::Lerp(SC.MinAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, TerrainStrSky);

		// STREAMING BUG FIX: mirror the GetSkylandColumnCache altitude formula exactly.
		//
		// Previous code:  CachedSkyAltWorld = SurfH + AltBase  (~3800 cm for flat terrain)
		// Actual shard altitude in GetSkylandColumnCache:
		//   DecoupledHeight = Lerp(AbsoluteSkyAnchor=15000, CenterHeight, CellShardT)
		//   SkyAlt = DecoupledHeight + AltBase + CellShardT * HeightAltitudeBonus
		//
		// For shards (CellShardT=0):  SkyAlt = 15000 + MinAlt  =>  ~15800 cm
		// For islands (CellShardT=1): SkyAlt = SurfH + BaseAlt + HeightAltBonus
		//
		// The old formula gave ~3800 cm for flat terrain: 120 m below where shards
		// actually live.  The streaming volume was centred in the wrong place so
		// skyland chunks were never spawned — skylands were completely invisible.
		static constexpr float AbsoluteSkyAnchor = 15000.f; // must match GetSkylandColumnCache
		const float ShardT     = FMath::SmoothStep(0.f, SC.ShardTransitionStrength, TerrainStrSky);
		const float DecoupledH = FMath::Lerp(AbsoluteSkyAnchor, RoundedSurfH, ShardT);
		CachedSkyAltWorld      = DecoupledH + AltBase
		                       + ShardT * (CachedCurvedH * SC.HeightAltitudeBonus
		                                  + CachedCurvedR * SC.RoughnessAltitudeBonus);
	}

	// Use the cached sky altitude (recomputed above if player moved enough)
	const float SkyAltWorld = CachedSkyAltWorld;

	// Calculate skyland thickness in chunks with margin
	const float IslandSize    = FMath::Max(SC.BaseIslandSize,
		                          SC.BaseIslandSize + CachedCurvedH * SC.HeightSizeBonus + CachedCurvedR * SC.RoughnessSizeBonus);
	const float HalfThickCm   = FMath::Max(200.f, IslandSize * SC.ThicknessRatio);
	const int32 SkyThickness  = FMath::CeilToInt(HalfThickCm / ChunkWorldSize) + 2;
	const int32 SkyZCoordCenter = FMath::RoundToInt(SkyAltWorld / ChunkWorldSize);
	
	// Build desired chunk set for streaming
	TSet<FIntVector> Desired;

	// 1. Ground area: Track local heightmap profile per-column
	// This prevents mountain peaks/valleys from unloading when the player stands on the opposite altitude extremum.
	for (int32 y = -RenderDistanceXY; y <= RenderDistanceXY; ++y)
	for (int32 x = -RenderDistanceXY; x <= RenderDistanceXY; ++x)
	{
		const float ColX = (PlayerCoord.X + x + 0.5f) * ChunkWorldSize;
		const float ColY = (PlayerCoord.Y + y + 0.5f) * ChunkWorldSize;

		const FVoxelBiomeManager::FWeightsAndHeight Wh = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(ColX, ColY, Config);
		const int32 GroundZCenter = FMath::RoundToInt(Wh.SurfaceHeight / ChunkWorldSize);

		for (int32 z = -RenderDistanceZ; z <= RenderDistanceZ; ++z)
		{
			Desired.Add(FIntVector(PlayerCoord.X + x, PlayerCoord.Y + y, GroundZCenter + z));
		}
	}

	// 2. Play space fallback: centered on Player altitude
	// This guarantees any player-placed structures or items high above are streaming properly.
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
	for (const FIntVector& C : ToRemove)
	{
		DestroyChunk(C);
		// FIX: Prune EmptyChunks when chunks leave range so the set doesn't grow unboundedly.
		// Previously EmptyChunks was only cleared on ClearWorld(), leaking memory over long sessions.
		EmptyChunks.Remove(C);
	}

	// --- 3. DYNAMIC LOD MULTIPLIERS FOR EXISTING CHUNKS ---
	// Update LOD levels for existing chunks based on distance from player
	for (auto& It : LoadedChunks)
	{
		AVoxelChunk* Chunk = It.Value;
		if (IsValid(Chunk))
		{
			// Calculate distance to chunk center for LOD determination.
			// FIX: Compare DistSq against squared thresholds directly — eliminates
			// sqrt() per loaded chunk per streaming update (was ~500 sqrts every 0.25s).
			FVector ChunkPos = ChunkCoordToWorld(It.Key) + FVector(ChunkSize * VoxelSize * 0.5f);
			const float DistSq = FVector::DistSquared(PlayerPos, ChunkPos);

			// Hysteresis bands (pre-squared) prevent LOD flip-flopping at borders.
			static constexpr float HysteresisFactor = 1.10f;
			const float L1ISq = LOD1Distance * LOD1Distance;
			const float L1OSq = LOD1Distance * LOD1Distance * HysteresisFactor * HysteresisFactor;
			const float L2ISq = LOD2Distance * LOD2Distance;
			const float L2OSq = LOD2Distance * LOD2Distance * HysteresisFactor * HysteresisFactor;

			int32 TargetLOD = Chunk->LOD;
			if      (Chunk->LOD < 2 && DistSq > L2OSq) TargetLOD = 2;
			else if (Chunk->LOD > 1 && DistSq < L2ISq) TargetLOD = 1;
			else if (Chunk->LOD < 1 && DistSq > L1OSq) TargetLOD = 1;
			else if (Chunk->LOD > 0 && DistSq < L1ISq) TargetLOD = 0;

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
		if (!LoadedChunks.Contains(C) && !EmptyChunks.Contains(C))
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
