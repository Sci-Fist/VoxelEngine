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

	// --- 🎯 FIX: Proximity-based streaming for close-range visibility ---
	// Ensure chunks within close proximity (2 chunks) are always prioritized
	// This prevents chunks from disappearing when the player is very close
	const int32 CloseRange = 2;
	const FIntVector PlayerChunkCoord = WorldToChunkCoord(CurrentPos);

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
		const float DecoupledH = FMath::Lerp(AbsoluteSkyAnchor, PlayerSurfH, ShardT);
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
	
		// Build desired chunk set for streaming

		TSet<FIntVector> Desired;


	// 1. Ground area: Track local heightmap profile per-column
	// FIX: Reduced render distance to minimize boundary crossings and chunk instability
	// This reduces the frequency of chunks crossing streaming boundaries
	const int32 OptimizedRenderDistanceXY = FMath::Max(1, RenderDistanceXY - 1); // Reduced by 1 to minimize boundary crossings
	const int32 GridDim = 2 * OptimizedRenderDistanceXY + 1;
	const int32 NumCols = GridDim * GridDim;

	TArray<int32> GroundZCenters;
	GroundZCenters.SetNumZeroed(NumCols);

	// FIX: Parallelize heavy noise lookups to prevent GameThread stall during character movement.
	ParallelFor(NumCols, [&](int32 Index)
	{
		const int32 x = -RenderDistanceXY + (Index % GridDim);
		const int32 y = -RenderDistanceXY + (Index / GridDim);

		const float ColX = (PlayerCoord.X + x + 0.5f) * ChunkWorldSize;
		const float ColY = (PlayerCoord.Y + y + 0.5f) * ChunkWorldSize;

		const FVoxelBiomeManager::FWeightsAndHeight Wh = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(ColX, ColY, Config);
		GroundZCenters[Index] = FMath::RoundToInt(Wh.SurfaceHeight / ChunkWorldSize);
	});

	// Populate Desired set sequentially on the GameThread from the parallel-built index array
	for (int32 Index = 0; Index < NumCols; ++Index)
	{
		const int32 x = -RenderDistanceXY + (Index % GridDim);
		const int32 y = -RenderDistanceXY + (Index / GridDim);
		const int32 GroundZCenter = GroundZCenters[Index];

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

	// Use continuous Z bounds (no rounding) to prevent churn when SkyAltWorld varies smoothly
	const float HalfThickWorld = HalfThickCm; // world units (cm)
	const int32 SkyZMin = FMath::FloorToInt((SkyAltWorld - HalfThickWorld) / ChunkWorldSize);
	const int32 SkyZMax = FMath::CeilToInt((SkyAltWorld + HalfThickWorld) / ChunkWorldSize);

	for (int32 z = SkyZMin; z <= SkyZMax; ++z)
	for (int32 y = -SkylandsRenderDistanceXY; y <= SkylandsRenderDistanceXY; ++y)

	for (int32 x = -SkylandsRenderDistanceXY; x <= SkylandsRenderDistanceXY; ++x)

	{

		Desired.Add(FIntVector(PlayerCoord.X + x, PlayerCoord.Y + y, z));
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

	// 
	// CHUNK BORDER GAP FIX: LOD Consistency Enforcement
	// ------------------------------------------------
	// Adjacent chunks must share the same LOD level to ensure mesh vertices
	// align perfectly at chunk boundaries. When neighboring chunks have different
	// LODs, their Surface Nets vertices are computed at different resolutions,
	// causing misalignment and visible gaps.

	//
	// Algorithm (3-pass):
	//   1. Compute desired LOD for each chunk based on distance from player

	//   2. Enforce consistency: if any neighbor has higher detail (lower LOD number),
	//      adopt that LOD for the current chunk. This propagates detail inward
	//      from the player's position, ensuring all chunks within the same

	//      render distance band have uniform LOD.
	//   3. Apply transitions for any LOD changes
	//
	// The neighbor check uses 6-directional adjacency (up/down/north/south/east/west).
	// This guarantees that the entire loaded volume is LOD-uniform except at the
	// outermost boundary where lower-detail chunks may appear.
	//
	// Performance: O(N) where N = number of loaded chunks. Each chunk checks up

	// to 6 neighbors. For typical view distances (5-8 chunks), this is negligible.
	//
	// NOTE: The hysteresis bands (L1ISq, L1OSq, etc.) prevent rapid LOD flip-flopping
	// when a chunk sits near a distance threshold. The factor of 1.10 provides a
	// 10% deadband to stabilize transitions.
	//

	// Update LOD levels for existing chunks based on distance from player

	// PASS 1: Compute desired LOD for each chunk based on distance from player
	TMap<FIntVector, int32> DesiredLODs;
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

			DesiredLODs.Add(It.Key, TargetLOD);
		}
	}

	// PASS 2: Enforce LOD consistency between neighbors
	// If any neighbor has higher detail (lower LOD number), adopt that LOD
	TMap<FIntVector, int32> FinalLODs = DesiredLODs;
	
	// FIX: Use multiple passes to ensure full consistency propagation
	// Single pass may not catch all inconsistencies when multiple chunks need to adjust
	bool bChanged = true;
	int32 PassCount = 0;
	const int32 MaxPasses = 6; // Safety limit to prevent infinite loops
	
	while (bChanged && PassCount < MaxPasses)
	{
		bChanged = false;
		PassCount++;
		
		for (auto& It : DesiredLODs)
		{
			const FIntVector& ChunkCoord = It.Key;
			int32 CurrentLOD = FinalLODs[ChunkCoord];
			
			// Check all 6 adjacent neighbors (up/down/north/south/east/west)
			const FIntVector Neighbors[6] = {
				FIntVector(1, 0, 0),  // +X (east)
				FIntVector(-1, 0, 0), // -X (west)
				FIntVector(0, 1, 0),  // +Y (south)
				FIntVector(0, -1, 0), // -Y (north)
				FIntVector(0, 0, 1),  // +Z (up)
				FIntVector(0, 0, -1)  // -Z (down)
			};

			for (const FIntVector& Offset : Neighbors)
			{
				const FIntVector NeighborCoord = ChunkCoord + Offset;
				if (FinalLODs.Contains(NeighborCoord))
				{
					int32 NeighborLOD = FinalLODs[NeighborCoord];
					// If neighbor has higher detail (lower LOD number), adopt it
					if (NeighborLOD < CurrentLOD)
					{
						CurrentLOD = NeighborLOD;
						bChanged = true;
					}
				}
			}
			
			FinalLODs[ChunkCoord] = CurrentLOD;
		}
	}

	// PASS 3: Apply transitions for any LOD changes
	for (auto& It : LoadedChunks)
	{
		const FIntVector& ChunkCoord = It.Key;
		AVoxelChunk* Chunk = It.Value;
		if (IsValid(Chunk) && FinalLODs.Contains(ChunkCoord))
		{
			int32 FinalLOD = FinalLODs[ChunkCoord];
			if (FinalLOD != Chunk->LOD)
			{
				// FIX: Only transition if chunk is ready and not currently generating
				// This prevents race conditions where LOD transition tries to modify
				// a chunk that's still being generated or has invalid mesh data
				if (Chunk->IsReady() && !Chunk->IsGenerating())
				{
					Chunk->TransitionToLOD(FinalLOD);
				}
				else
				{
					// Mark chunk as needing LOD update once it's ready
					Chunk->bPendingLODTransition = true;
					Chunk->PendingLOD = FinalLOD;
				}
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

	// 3. Compute absolute distances and Sort with proximity-based prioritization
	TArray<TPair<int32, FIntVector>> SortedQueue;
	SortedQueue.Reserve(UniqueMerged.Num());

	for (const FIntVector& C : UniqueMerged)
	{
		FIntVector Local = C - PlayerCoord;
		const int32 DistSq = Local.X*Local.X + Local.Y*Local.Y + Local.Z*Local.Z;
		
		// FIX: Prioritize close-range chunks to prevent visibility issues
		// Chunks within CloseRange get a significant priority boost
		const int32 ManhattanDist = FMath::Abs(Local.X) + FMath::Abs(Local.Y) + FMath::Abs(Local.Z);
		int32 PriorityScore = DistSq;
		
		// Boost priority for very close chunks (within 2 chunks)
		if (ManhattanDist <= CloseRange)
		{
			PriorityScore = FMath::Max(1, DistSq / 100); // Strong priority boost for close chunks
		}
		
		SortedQueue.Add(TPair<int32, FIntVector>(PriorityScore, C));
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

// ADD: Enhanced visibility state management for close-range chunks
void AVoxelWorld::ApplyMeshToChunk(AVoxelChunk* Chunk)
{
	if (!Chunk || !Chunk->IsReady())
	{
		return;
	}

	// Ensure chunk is visible before applying mesh
	Chunk->SetVisibility(true);
	Chunk->SetHidden(false);
	
	// Apply mesh data
	if (Chunk->ApplyMesh())
	{
		// Verify visibility after successful mesh application
		if (Chunk->IsReady() && !Chunk->IsGenerating())
		{
			Chunk->SetVisibility(true);
			Chunk->SetHidden(false);
		}
	}
	else
	{
		// If mesh application fails, keep chunk visible but mark for retry
		Chunk->SetVisibility(true);
		Chunk->SetHidden(false);
		Chunk->bPendingMeshRetry = true;
	}
}

// ADD: Protect close-range chunks from aggressive LOD transitions
void AVoxelWorld::EnforceLODConsistency()
{
	const FVector PlayerPos = GetPlayerPosition();
	const float CloseRangeThreshold = ChunkSize * VoxelSize * 3.0f; // 3 chunks distance
	
	for (auto& ChunkPair : LoadedChunks)
	{
		AVoxelChunk* Chunk = ChunkPair.Value;
		if (!Chunk) continue;
		
		const float Distance = FVector::Dist(Chunk->GetActorLocation(), PlayerPos);
		
		// Protect close-range chunks from aggressive LOD downgrades
		if (Distance < CloseRangeThreshold)
		{
			// Force close chunks to use highest detail LOD
			const int32 TargetLOD = FMath::Min(Chunk->CurrentLOD, 0);
			if (Chunk->CurrentLOD != TargetLOD)
			{
				Chunk->TransitionToLOD(TargetLOD);
				Chunk->SetVisibility(true); // Ensure visibility during transition
			}
		}
		else
		{
			// Apply normal LOD consistency rules for distant chunks
			// ... existing LOD consistency logic from UpdateChunkStreaming
		}
	}
}

// ADD: Visibility health check system
void AVoxelWorld::CheckCloseRangeVisibility()
{
	const FVector PlayerPos = GetPlayerPosition();
	const float CloseRangeThreshold = ChunkSize * VoxelSize * 3.0f;
	
	for (auto& ChunkPair : LoadedChunks)
	{
		AVoxelChunk* Chunk = ChunkPair.Value;
		if (!Chunk) continue;
		
		const float Distance = FVector::Dist(Chunk->GetActorLocation(), PlayerPos);
		
		// Check visibility status of close-range chunks
		if (Distance < CloseRangeThreshold)
		{
			if (!Chunk->IsVisible() && Chunk->IsReady() && !Chunk->IsGenerating())
			{
				// Force visibility restoration for close chunks
				UE_LOG(LogVoxelWorld, Warning, TEXT("Restoring visibility for close chunk at %s"), 
					*Chunk->GetActorLocation().ToString());
				
				Chunk->SetVisibility(true);
				Chunk->SetHidden(false);
				
				// Trigger mesh re-application if needed
				if (Chunk->bPendingMeshRetry)
				{
					ApplyMeshToChunk(Chunk);
					Chunk->bPendingMeshRetry = false;
				}
			}
		}
	}
}
