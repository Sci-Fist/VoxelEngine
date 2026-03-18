// VoxelWorld_Generation.cpp
// 
// Core world generation implementation for the voxel engine.
// Contains all generation-related functions separated from the main VoxelWorld class
// to improve code organization and maintainability.
//
// GENERATION PIPELINE:
// 1. World Discovery: Locate existing chunkgots and avoid overlap
// 2. Bounds Calculation: Determine generation area centered on world location
// 3. Chunk Queueing: Sort chunks by distance for optimal generation order
// 4. Async Generation: Spawn chunks with proper configuration and threading
// 5. Player Spawning: Position player in appropriate biome with safety checks
//
// PERFORMANCE FEATURES:
// - Editor vs Game threading differences
// - Chunk pooling for memory efficiency
// - Async generation with proper completion callbacks
// - Smart bounds calculation to prevent excessive generation

#include "VoxelWorld.h"
#include "Voxel/Core/World/Water/VoxelWorldWater.h"
#include "Voxel/Water/VoxelWaterSimulator.h"
#include "Voxel/Core/VoxelChunk.h"
#include "Voxel/Core/VoxelChunkPool.h"
#include "Voxel/Generation/VoxelGeneratorTask.h"
#include "Voxel/Generation/VoxelDensityGenerator.h"
#include "Voxel/Biomes/VoxelBiomeManager.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "Voxel/VoxelLogger.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"

#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Misc/DateTime.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "FirstVoxelHUD.h"


// ============================================================
//  World Generation Implementation
// ============================================================

void AVoxelWorld::GenerateWorldDeferred()
{
	if (!GetWorld() || bShutdown) return;

	UVoxelLogger::LogVoxelEvent(TEXT("VoxelWorld: GenerateWorldDeferred started."));
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: GenerateWorldDeferred started at location %s"), *GetActorLocation().ToString());

	// FIX: Show progress feedback to prevent editor freeze perception
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
		{
			HUD->bShowLoadBar = true;
			HUD->LoadProgress = 0.05f; // Start at 5% to show immediate feedback
		}
	}

	// 0. Reconcile existing chunks to avoid "stacking"
	DiscoverExistingChunks();

	// 1. Integrated Smart Area Search - moved to async task
	TWeakObjectPtr<AVoxelWorld> WeakThis(this);
	
	// Use async task to prevent main thread blocking
	Async(EAsyncExecution::ThreadPool, [WeakThis]()
	{
		AVoxelWorld* Self = WeakThis.Get();
		if (!Self || Self->bShutdown) return;

		// Heavy computation moved to background thread
		Self->PerformWorldDiscoveryAndBoundsCalculation();
		
		// Return to game thread to finalize setup
		AsyncTask(ENamedThreads::GameThread, [WeakThis]()
		{
			AVoxelWorld* Self = WeakThis.Get();
			if (!Self || Self->bShutdown) return;
			
			Self->FinalizeGenerationSetup();
		});
	});
}

void AVoxelWorld::PerformWorldDiscoveryAndBoundsCalculation()
{
	if (!GetWorld() || bShutdown) return;

	// Distance to jump between world seeds to avoid overlap
	float WorldRadius = RenderDistanceXY * ChunkSize * VoxelSize;
	float JumpStep = WorldRadius * 3.f; 

	FVector CandidatePos = GetActorLocation();

	// Prioritise centering generation on the PlayerStart to accurately match Play Mode centering
	TArray<AActor*> PlayerStarts;
	UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), PlayerStarts);
	
	// FIX: Always center generation on the PlayerStart location for consistent behavior
	// This ensures the world generates around where the player will spawn
	if (PlayerStarts.Num() > 0 && PlayerStarts[0])
	{
		CandidatePos = PlayerStarts[0]->GetActorLocation();
		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Centering GenerateWorld on PlayerStart %s"), *CandidatePos.ToString());
	}

	// FIX: Only search for conflicts in standalone game builds, not in PIE
	// In PIE mode, we want to generate terrain exactly where the VoxelWorld actor is placed
	// to avoid creating terrain far away from the intended location
#if WITH_EDITOR
	// In editor builds, always use current location without conflict checking
	// This covers both editor viewport and PIE mode
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Editor build - using current location %s"), *CandidatePos.ToString());
#else
	// In standalone game builds, do conflict checking to prevent overlapping worlds
	bool bFoundConflict = true;
	int32 MaxAttempts = 100;

	while (bFoundConflict && MaxAttempts-- > 0)
	{
		bFoundConflict = false;
		for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
		{
			AVoxelWorld* Other = *It;
			if (Other == this) continue;

			float DistanceXY = FVector::Dist2D(CandidatePos, Other->GetActorLocation());
			if (DistanceXY < JumpStep)
			{
				CandidatePos.X += JumpStep;
				if (FMath::Abs(CandidatePos.X) > 1000000.f)
				{
					CandidatePos.X = 0.f;
					CandidatePos.Y += JumpStep;
				}
				bFoundConflict = true;
				break; 
			}
		}
	}
#endif

	if (GetActorLocation() != CandidatePos)
	{
		SetActorLocation(CandidatePos);
		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Relocated to suitable area at %s"), *CandidatePos.ToString());
	}
	SpawnTargetPos = CandidatePos;

	// 3. Ensure DataMap is initialized (critical for editor calls)
	if (!bInitialized)
	{
		DataMap.Init(ChunkSize);
		bInitialized = true;
	}

	// RESET active generations for editor calls. 
	ActiveGenerations = 0;
	GenerationQueue.Empty();
	EmptyChunks.Empty();
	QueueHead = 0;

	// 4. Calculate generation bounds
	// Center generation on the player position when in PIE mode, or on the VoxelWorld actor location otherwise.
	// This ensures terrain generates around the player when pressing Play.
	FVector Anchor;
	if (GetWorld()->IsGameWorld())
	{
		// In PIE mode, center generation on the player position
		APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
		if (Player)
		{
			Anchor = Player->GetActorLocation();
			UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Centering generation on player position %s"), *Anchor.ToString());
		}
		else
		{
			// Fallback to VoxelWorld location if no player found
			Anchor = GetActorLocation();
			UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: No player found, using VoxelWorld location %s"), *Anchor.ToString());
		}
	}
	else
	{
		// In editor viewport, use VoxelWorld actor location
		Anchor = GetActorLocation();
		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Editor mode - using VoxelWorld location %s"), *Anchor.ToString());
	}
	
	FIntVector Origin = WorldToChunkCoord(Anchor);
	
	FIntVector MinCoord = Origin - FIntVector(RenderDistanceXY, RenderDistanceXY, RenderDistanceZ);
	FIntVector MaxCoord = Origin + FIntVector(RenderDistanceXY, RenderDistanceXY, RenderDistanceZ);

	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Generating chunks from (%d, %d, %d) to (%d, %d, %d)"),
	       MinCoord.X, MinCoord.Y, MinCoord.Z, MaxCoord.X, MaxCoord.Y, MaxCoord.Z);

	// Calculate center for sorting logic
	FIntVector CenterCoord = MinCoord + (MaxCoord - MinCoord) / 2;

	TArray<TPair<int32, FIntVector>> SortedChunks;
	for (int32 z = MinCoord.Z; z <= MaxCoord.Z; ++z)
	{
		for (int32 y = MinCoord.Y; y <= MaxCoord.Y; ++y)
		{
			for (int32 x = MinCoord.X; x <= MaxCoord.X; ++x)
			{
				FIntVector Coord(x, y, z);
				
				// Skip if it already exists in the world
				if (LoadedChunks.Contains(Coord))
				{
					continue;
				}

				int32 Dist = FMath::Max3(FMath::Abs(x - CenterCoord.X), FMath::Abs(y - CenterCoord.Y), FMath::Abs(z - CenterCoord.Z));
				SortedChunks.Add(TPair<int32, FIntVector>(Dist, Coord));
			}
		}
	}

	// Nearest first to the center bounds
	SortedChunks.Sort([](const TPair<int32,FIntVector>& A, const TPair<int32,FIntVector>& B){ return A.Key < B.Key; });

	// Use TSet for O(1) dedupe; GenerationQueue.Contains() in a loop was O(n^2) and froze editor with 3k+ chunks.
	TSet<FIntVector> QueueSet;
	for (auto& P : SortedChunks)
	{
		if (!QueueSet.Contains(P.Value))
		{
			QueueSet.Add(P.Value);
			GenerationQueue.Add(P.Value);
		}
	}
	
#if WITH_EDITOR
	if (!GetWorld()->IsGameWorld())
	{
		const FVoxelGenerationConfig& Cfg = GetEffectiveConfig();
		FVector Pos = Anchor; 
		Pos.Z = 0.f; 
		Pos = SnapToVoxelGrid(Pos);
		
		const FVoxelBiomeManager::FWeightsAndHeight Wh = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(Pos.X, Pos.Y, Cfg);
		const float Surface = Wh.SurfaceHeight;
		const float SafeOffset = GetSafeSpawnHeightOffset();
		
		float TargetZ = Surface + SafeOffset;
		if (Wh.Weights.GetWeight(EVoxelBiome::Craters) > 0.3f)
		{
			TargetZ = Surface + SafeOffset;
		}

		const FSkylandsLayerConfig& SC = Cfg.SkylandsLayer;
		const float HeightNorm    = FMath::Clamp(Surface / SC.MaxTerrainReference, 0.f, 1.f);
		const float RoughnessNorm = FMath::Clamp(Wh.Weights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);
		const float TerrainStr    = FMath::Clamp(HeightNorm * 1.5f + RoughnessNorm * 0.8f, 0.f, 1.f);
		const float AltBase       = FMath::Lerp(SC.MinAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, TerrainStr);
		const float SkyAlt        = Surface + AltBase + HeightNorm * SC.HeightAltitudeBonus + RoughnessNorm * SC.RoughnessAltitudeBonus;
		const float IslandHalfThick = (SC.BaseIslandSize + HeightNorm * SC.HeightSizeBonus + RoughnessNorm * SC.RoughnessSizeBonus) * SC.ThicknessRatio;

		static FVoxelDensityGenerator EditorSpawnProbe;
		bool bFoundSkyland = false;
		
		if (SkyAlt > Surface + 5000.f)
		{
			for (float z = SkyAlt + IslandHalfThick; z >= FMath::Max(SkyAlt - IslandHalfThick, Surface + 500.f); z -= 200.f)
			{
				if (EditorSpawnProbe.GetDensity(Pos.X, Pos.Y, z, Cfg) > 0.f)
				{
					TargetZ = z + SafeOffset;
					bFoundSkyland = true;
					break;
				}
			}
		}

		const float ChunkHeight = ChunkSize * VoxelSize;
		const int32 SpawnChunkZ = FMath::FloorToInt(TargetZ / ChunkHeight);
		const FIntVector SpawnCoord = WorldToChunkCoord(Anchor); 

		for (int32 x = -1; x <= 1; ++x)
		{
			for (int32 y = -1; y <= 1; ++y)
			{
				if (bFoundSkyland && SpawnChunkZ != 0)
				{
					FIntVector SkyCoord(SpawnCoord.X + x, SpawnCoord.Y + y, SpawnChunkZ);
					if (!QueueSet.Contains(SkyCoord)) { QueueSet.Add(SkyCoord); GenerationQueue.Add(SkyCoord); }
					
					if (SpawnChunkZ > 0)
					{
						FIntVector BelowCoord(SpawnCoord.X + x, SpawnCoord.Y + y, SpawnChunkZ - 1);
						if (!QueueSet.Contains(BelowCoord)) { QueueSet.Add(BelowCoord); GenerationQueue.Add(BelowCoord); }
					}
				}
			}
		}
	}
#endif

	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Queued %d new chunks to extend the world."), GenerationQueue.Num());
	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelWorld: Queued %d chunks."), GenerationQueue.Num()));

	if (GenerationQueue.Num() == 0)
	{
		UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: No chunks were queued for generation. This may indicate an issue with world bounds or chunk coordinates."));
	}
}

void AVoxelWorld::FinalizeGenerationSetup()
{
	if (!GetWorld() || bShutdown) return;

	// FIX: Update load bar progress during main world generation
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
		{
			HUD->bShowLoadBar = true;
			HUD->LoadProgress = 0.1f; // Set to 10% after bounds calculation
		}
	}

	// 5. Editor generation should never block the game thread.
	// The old tight while-loop prevented async chunk completion callbacks from
	// running on the game thread, which could freeze the editor when generating.
	if (!GetWorld()->IsGameWorld())
	{
#if WITH_EDITOR
		TWeakObjectPtr<AVoxelWorld> WeakThis(this);
		DrainTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakThis](float) -> bool
			{
				AVoxelWorld* Self = WeakThis.Get();
				if (!Self || Self->bShutdown)
				{
					#if WITH_EDITOR
					if (Self) Self->DrainTickerHandle.Reset();
					#endif
					return false;
				}

				Self->DrainGenerationQueue();
				const bool bDone = Self->QueueHead >= Self->GenerationQueue.Num();
				if (bDone)
				{
					UE_LOG(LogVoxelWorld, Log,
						TEXT("VoxelWorld: Editor generation complete. %d chunks loaded."),
						Self->LoadedChunks.Num());
					UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelWorld: Editor generation complete, %d chunks."), Self->LoadedChunks.Num()));
					#if WITH_EDITOR
					Self->DrainTickerHandle.Reset();
					#endif
				}
				return !bDone; // keep ticking until the queue is empty
			}),
			0.1f);
#endif
	}

	// 5. If at runtime, prepare for player spawn
	if (GetWorld()->IsGameWorld())
	{
		// ── Prepare Spawn State ──────────────────────────────────────────
		// Do NOT activate hover-lock here - let ProcessInitialPlayerSpawn handle it
		// This prevents double chunk generation and state conflicts
		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Runtime generation complete, spawn will be handled by ProcessInitialPlayerSpawn"));
		
		// FIX: Initialize spawn wait state to ensure player only spawns after spawn area is ready
		// This prevents the player from spawning in empty space before terrain is generated
		APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
		if (Player)
		{
			// Park player at high altitude to prevent falling through empty terrain
			FVector ParkPos = Player->GetActorLocation();
			ParkPos.Z = 100000.0f; // High altitude parking position
			Player->SetActorLocation(ParkPos, false, nullptr, ETeleportType::TeleportPhysics);
			
			// Player movement is now handled normally - no freezing or hiding
			// The hover-lock system in Tick() will handle the waiting and positioning
		}
		
		// FIX: Only start spawn area generation after main world generation is complete
		// This prevents generation deadlock and ensures proper chunk ordering
		// The hover-lock system in Tick() will handle the spawn area generation
		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Main world generation complete, spawn area will be handled by hover-lock system"));
		
		// Trigger initial spawn sequence calculation and wait lock
		ProcessInitialPlayerSpawn();
	}
}

void AVoxelWorld::SpawnChunk(const FIntVector& Coord, bool bSyncCollision)
{
	if (LoadedChunks.Contains(Coord)) return;

	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Spawning chunk at coordinate (%d, %d, %d)"), Coord.X, Coord.Y, Coord.Z);

	FVector Loc = ChunkCoordToWorld(Coord);
	AVoxelChunk* Chunk = nullptr;

	Chunk = ChunkPool.RetrieveOrCreateChunk(GetWorld(), Loc, this);
	if (!Chunk) return;

	// FIX: Keep chunk visible but hide mesh components until generation is complete
	// This prevents invisible terrain while allowing proper visibility management
	Chunk->SetActorHiddenInGame(false);
	if (Chunk->GetProceduralMesh()) 
	{
		Chunk->GetProceduralMesh()->SetVisibility(false);
		// FIX: Force synchronous cooking for spawn area chunks to prevent falling through floor
		Chunk->GetProceduralMesh()->bUseAsyncCooking = !bSyncCollision;
	}

	// SetOwner links the detached chunk to this world so it can be rediscovered on re-play reconciliations
	// without forcing it into the actor attachment tree.
	Chunk->SetOwner(this);

	Chunk->SetActorLabel(FString::Printf(TEXT("Chunk_%d_%d_%d"), Coord.X, Coord.Y, Coord.Z));

	// Detached actors correctly live in root directories and use folder path structures
	// without collapsing under their generation tree in the Outliner view.

	// Organize in the World Outliner: g_VoxelChunks/Z{n}
	// Hierarchical folders prevent the "Outliner Lockup" that happens with too many actors in one folder.
	FString FolderName = FString::Printf(TEXT("g_VoxelChunks/Z%d"), Coord.Z);
	Chunk->SetFolderPath(FName(*FolderName));

	Chunk->ChunkCoord = Coord;
	
	const float SizeInCm = ChunkSize * VoxelSize;
	Chunk->SetActorLocation(FVector(Coord.X * SizeInCm, Coord.Y * SizeInCm, Coord.Z * SizeInCm));

	ConfigureChunk(Chunk);

	LoadedChunks.Add(Coord, Chunk);
	
	// Async generation enabled for both games and Editor runs
	ActiveGenerations++;
	TWeakObjectPtr<AVoxelWorld> WeakThis(this);
	Chunk->OnGenerationComplete = [WeakThis, Coord]() 
	{ 
		if (AVoxelWorld* StrongThis = WeakThis.Get())
		{
			StrongThis->ActiveGenerations--; 

			// Optimization removed: Destroying empty chunks (e.g. at high LOD) sets them into EmptyChunks
			// and locks them forever, preventing LOD Transitions from re-evaluating detail layers correctly.
			// Keeping them in LoadedChunks naturally limits draw overhead without breaking LOD detail updates.
		}
	};

	// Wire water sim into this chunk
	if (WaterSystemComponent)
	{
		WaterSystemComponent->InitChunkWater(Chunk);
	}

	Chunk->GenerateAsync();

}

void AVoxelWorld::DestroyChunk(const FIntVector& Coord)
{
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Destroying chunk at coordinate (%d, %d, %d)"), Coord.X, Coord.Y, Coord.Z);

	AVoxelChunk** ChunkPtr = LoadedChunks.Find(Coord);
	if (ChunkPtr && *ChunkPtr)
	{
		AVoxelChunk* Chunk = *ChunkPtr;

		// Unregister from water simulator before clearing the chunk
		if (WaterSystemComponent)
		{
			if (WaterSystemComponent->GetSimulator())
				WaterSystemComponent->GetSimulator()->UnregisterChunk(Coord);

			WaterSystemComponent->RemoveChunkFromWaterSimulation(Coord);
		}

		if (Chunk->IsGenerating())
		{
			Chunk->CancelGeneration();
		}

		LoadedChunks.Remove(Coord);
		
		// Return chunk to pool instead of destroying
		ChunkPool.ReturnChunk(Chunk);
	}
}

void AVoxelWorld::DrainGenerationQueue()
{
	if (!GetWorld()) return;

	const bool bIsEditor = !GetWorld()->IsGameWorld();
	// FIX: Boost limit during wait screen (InitialSpawn) to saturate thread pool faster, 
	// since frame rate doesn't matter during a load screen.
	const int32 Limit = bIsEditor ? 2 : (bWaitingForInitialSpawn ? 24 : 6); 

	int32 ProcessedThisTick = 0;
	while (ProcessedThisTick < Limit && QueueHead < GenerationQueue.Num())
	{
		if (ActiveGenerations >= MaxConcurrentGenerations)
		{
			break; // Throttle to prevent overloading thread-pool scheduler
		}

		const FIntVector Coord = GenerationQueue[QueueHead++];
		SpawnChunk(Coord);
		ProcessedThisTick++;
	}

	// Compact the queue when the consumed head grows large.
	// FIX: old threshold was 50, causing an O(N) RemoveAt every ~6 ticks (at 8/tick).
	// Raised to 256 so compaction runs ~every 32 ticks instead.
	// UpdateChunkStreaming rebuilds the queue from scratch every 0.25s anyway,
	// so we only need this as a memory safety net during initial load.
	if (QueueHead > 256)
	{
		GenerationQueue.RemoveAt(0, QueueHead);
		QueueHead = 0;
	}
}

void AVoxelWorld::RebuildChunk(const FIntVector& Coord)
{
	if (AVoxelChunk** ChunkPtr = LoadedChunks.Find(Coord))
	{
		if (AVoxelChunk* Chunk = *ChunkPtr)
		{
			if (!Chunk->IsGenerating())
				Chunk->GenerateAsync();
		}
	}
}

void AVoxelWorld::DiscoverExistingChunks()
{
	if (!GetWorld()) return;

	int32 FoundCount = 0;
	for (TActorIterator<AVoxelChunk> It(GetWorld()); It; ++It)
	{
		AVoxelChunk* Chunk = *It;
		if (Chunk && Chunk->GetOwner() == this)
		{
			FIntVector Coord = Chunk->ChunkCoord;
			LoadedChunks.Add(Coord, Chunk);
			FoundCount++;
		}
	}
	
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Discovered %d existing chunks"), FoundCount);
}

void AVoxelWorld::ConfigureChunk(AVoxelChunk* Chunk) const
{
	if (!Chunk) return;

	const FVoxelGenerationConfig& EffectiveConfig = GetEffectiveConfig();

	Chunk->ChunkSize          = ChunkSize;
	Chunk->VoxelSize          = VoxelSize;
	Chunk->MasterFlatMaterial  = MasterFlatMaterial;
	Chunk->MasterSlopeMaterial = MasterSlopeMaterial;
	Chunk->SlopeThreshold      = SlopeThreshold;
	Chunk->GenerationConfig    = EffectiveConfig;
	Chunk->TreeMesh           = TreeMesh;
	Chunk->GrassMesh          = GrassMesh;
	Chunk->FoliageDensity     = FoliageDensity;
	Chunk->MaxFoliageSlope    = MaxFoliageSlope;

	Chunk->DensityGenerator   = DensityGenerator.Get();
	
	// FIX: Inject absolute anchor offsets to center the mathematical Crater spawn basin
	// relative to where the world actor stands flawlessly.
	Chunk->GenerationConfig.Craters.ForcedCraterCenter = FVector2D(GetActorLocation().X, GetActorLocation().Y);

	// FIX: Propagate world-level SlopeThreshold into the generation config so
	// VoxelMeshGenerator uses the right cutoff for flat vs slope classification.
	// Previously this was set on the chunk directly but never written into
	// GenerationConfig, so the mesh generator always used the struct default.
	Chunk->GenerationConfig.SlopeThreshold = SlopeThreshold;

	// Wire water material from the world-level config.
	Chunk->WaterMaterial      = GenerationConfig.Water.OceanMaterial.Get();

	// FIX: Inject dense nodes grid cache from parent ChunkManager.
	Chunk->DenseChunk = const_cast<AVoxelWorld*>(this)->ChunkManager.GetOrCreateChunk(Chunk->ChunkCoord, Chunk->ChunkSize);
}

// ============================================================
//  ProcessInitialPlayerSpawn
//  Validates crater/skyland height bounds and shifts player position.
//  FIXED: Proper player positioning and hover-lock logic
// ============================================================
void AVoxelWorld::ProcessInitialPlayerSpawn()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player) return;

	const FVoxelGenerationConfig& Config = GetEffectiveConfig();

	// FIX: Do NOT use Player->GetActorLocation() here.
	// The player was parked at Z=100,000 in BeginPlay, so its XY is 0,0 —
	// which may not be where the PlayerStart actually is.
	// Instead, read XY from the PlayerStart actor so spawn height is sampled
	// at the correct world position.
	// Read XY from the PlayerStart — GenerateWorldDeferred already moved it
	// to the best crater position for this seed, so we just use that directly.
	// Do NOT re-run FindCraterSpawnLocation here; it would search again from
	// the PlayerStart's new position and potentially drift to a different crater.
	FVector Pos = SpawnTargetPos;
	if (Pos.IsZero())
	{
		TArray<AActor*> PlayerStarts;
		UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), PlayerStarts);
		if (PlayerStarts.Num() > 0 && PlayerStarts[0])
		{
			Pos = PlayerStarts[0]->GetActorLocation();
		}
	}
	Pos.Z = 0.f; // Z will be determined from surface height below
	Pos = SnapToVoxelGrid(Pos);
	
	// FIXED: Calculate proper spawn height based on terrain
	const FVoxelBiomeManager::FWeightsAndHeight Wh = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(Pos.X, Pos.Y, Config);
	const FVoxelBiomeWeightMap& Weights = Wh.Weights;
	const float Surface = Wh.SurfaceHeight;

	// Calculate safe spawn height with proper offset
	const float SafeOffset = GetSafeSpawnHeightOffset();
	float TargetZ = Surface + SafeOffset;
	
	// Crater spawn: place player on the crater floor with a comfortable offset.
	// Surface here is the blended height which for a crater center is the FLOOR
	// of the basin (negative depth from BasePlains). We add SafeOffset so the
	// player stands on the floor rather than spawning inside it.
	const float CraterWeight = Weights.GetWeight(EVoxelBiome::Craters);
	if (CraterWeight > 0.3f)
	{
		// Surface already gives us the crater floor height from the noise blend.
		// Just add normal safe offset — don't jump to rim height.
		TargetZ = Surface + SafeOffset;
	}
	
	// FIXED: Check for skylands and adjust if needed
	const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
	const float HeightNorm    = FMath::Clamp(Surface / SC.MaxTerrainReference, 0.f, 1.f);
	const float RoughnessNorm = FMath::Clamp(Weights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);
	const float TerrainStr    = FMath::Clamp(HeightNorm * 1.5f + RoughnessNorm * 0.8f, 0.f, 1.f);
	const float AltBase       = FMath::Lerp(SC.MinAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, TerrainStr);
	const float SkyAlt        = Surface + AltBase + HeightNorm * SC.HeightAltitudeBonus + RoughnessNorm * SC.RoughnessAltitudeBonus;
	const float IslandHalfThick = (SC.BaseIslandSize + HeightNorm * SC.HeightSizeBonus + RoughnessNorm * SC.RoughnessSizeBonus) * SC.ThicknessRatio;

	const float SearchTop = SkyAlt + IslandHalfThick;
	const float SearchBot = FMath::Max(SkyAlt - IslandHalfThick, Surface + 500.f);

	static FVoxelDensityGenerator SpawnProbe;
	bool bFoundSkyland = false;
	
	// FIXED: Only check for skylands if they're significantly higher than surface
	// AND if the surface height is reasonable (not extremely high)
	if (SkyAlt > Surface + 5000.f && Surface < 50000.f)
	{
		for (float z = SearchTop; z >= SearchBot; z -= 200.f)
		{
			if (SpawnProbe.GetDensity(Pos.X, Pos.Y, z, Config) > 0.f)
			{
				TargetZ = z + SafeOffset;
				bFoundSkyland = true;
				break;
			}
		}
	}
	
	// FIXED: Ensure spawn height is reasonable - prevent extremely high spawns
	// If TargetZ is extremely high (>100000), something went wrong with the calculation
	if (TargetZ > 100000.f)
	{
		UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: Spawn height calculation resulted in extremely high value (%.2f). Using surface height instead."), TargetZ);
		TargetZ = Surface + SafeOffset;
	}

	// Player movement is now handled normally - no need to restore anything
	// since we don't freeze or hide the player anymore

	Pos.Z = TargetZ;
	TargetCoordsZ = TargetZ; // FIX: Ensure Loading Wait Screen parks player above ground
	Player->SetActorLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);
	
	// FIXED: Initialize spawn tracking with proper coordinate alignment
	// Only proceed if we're not already in a spawn state to prevent recursion
	if (bWaitingForInitialSpawn)
	{
		UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: ProcessInitialPlayerSpawn called while already in spawn state. Ignoring."));
		return;
	}

	InitialSpawnCoords.Empty();
	bWaitingForInitialSpawn = true;
	TargetCoordsZ = TargetZ;
	CachedSurfaceHeight = Surface;

	// FIXED: Use consistent coordinate system - spawn chunks around the actual spawn position
	// This ensures the hover-lock waits for the same chunks that were actually generated
	// FIX: Center wait area bounds around actual Surface height instead of TargetZ (park position above ground)
	// This ensures the hover-lock is held for the actual terrain mesh chunks, and their collision, before releasing the player.
	const FIntVector SpawnCoord = WorldToChunkCoord(FVector(Pos.X, Pos.Y, Surface));
	const float ChunkHeight = ChunkSize * VoxelSize;
	const int32 SpawnChunkZ = FMath::FloorToInt(TargetZ / ChunkHeight);

	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Spawning spawn area chunks around position %s (Z=%.2f)"), 
	       *FVector(Pos.X, Pos.Y, TargetZ).ToString(), TargetZ);

	// Spawn chunks in a focused grid around the spawn position
	// This ensures the player has solid ground without overwhelming the generation system
	// FIX: Set RadiusXY to 7 to create a 15x15 area (from -7 to +7 = 15 chunks total)
	// This gives us approximately 16x16 chunks around the spawn point for proper crater coverage
	TArray<FIntVector> SpawnAreaCoords;
	const int32 RadiusXY = 7; 
	for (int32 x = -RadiusXY; x <= RadiusXY; ++x)
	{
		for (int32 y = -RadiusXY; y <= RadiusXY; ++y)
		{
			for (int32 z = -1; z <= 1; ++z)
			{
				FIntVector NeighborCoord = SpawnCoord + FIntVector(x, y, z);
				SpawnAreaCoords.Add(NeighborCoord);
			}
		}
	}

	// Sort spawn area chunks by distance from player to generate closest first
	// FIX: Prioritize chunks that are in crater biome to ensure crater generation
	SpawnAreaCoords.Sort([SpawnCoord, this](const FIntVector& A, const FIntVector& B) {
		// Check if chunks are in crater biome
		const FVector WorldPosA = ChunkCoordToWorld(A);
		const FVector WorldPosB = ChunkCoordToWorld(B);
		
		const FVoxelGenerationConfig& Config = GetEffectiveConfig();
		const FVoxelBiomeWeightMap WeightsA = FVoxelBiomeManager::GetBiomeWeightsStatic(WorldPosA.X, WorldPosA.Y, Config);
		const FVoxelBiomeWeightMap WeightsB = FVoxelBiomeManager::GetBiomeWeightsStatic(WorldPosB.X, WorldPosB.Y, Config);
		
		const bool bIsCraterA = WeightsA.GetWeight(EVoxelBiome::Craters) > 0.1f;
		const bool bIsCraterB = WeightsB.GetWeight(EVoxelBiome::Craters) > 0.1f;
		
		// Prioritize crater chunks first
		if (bIsCraterA && !bIsCraterB) return true;
		if (!bIsCraterA && bIsCraterB) return false;
		
		// If both or neither are craters, sort by distance
		int32 DistA = FMath::Abs(A.X - SpawnCoord.X) + FMath::Abs(A.Y - SpawnCoord.Y) + FMath::Abs(A.Z - SpawnCoord.Z);
		int32 DistB = FMath::Abs(B.X - SpawnCoord.X) + FMath::Abs(B.Y - SpawnCoord.Y) + FMath::Abs(B.Z - SpawnCoord.Z);
		return DistA < DistB;
	});

	for (const FIntVector& Coord : SpawnAreaCoords)
	{
		InitialSpawnCoords.Add(Coord);

		// FIX: Explicitly call SpawnChunk for the wait zone. Because GenerateWorldDeferred 
		// centers main generation bounds height on the high-parked player elevation, the 
		// ground chunks might have been skipped there. Spawning them here triggers async 
		// background tasks immediately for safe release tracking.
		if (!LoadedChunks.Contains(Coord))
		{
			SpawnChunk(Coord);
		}
	}

	// Safety: ensure the two chunks directly below the player exist and
	// use synchronous collision cooking so IsCollisionReady() passes quickly.
	const FIntVector PlayerChunkBelow  = SpawnCoord + FIntVector(0, 0, -1);
	const FIntVector PlayerChunkBelow2 = SpawnCoord + FIntVector(0, 0, -2);

	if (!LoadedChunks.Contains(PlayerChunkBelow))
		SpawnChunk(PlayerChunkBelow, /*bSyncCollision=*/true);
	InitialSpawnCoords.Add(PlayerChunkBelow);

	if (!LoadedChunks.Contains(PlayerChunkBelow2))
		SpawnChunk(PlayerChunkBelow2, /*bSyncCollision=*/true);
	InitialSpawnCoords.Add(PlayerChunkBelow2);

	// Spawn additional skyland chunks if needed (only if not already handled above)
	if (bFoundSkyland && SpawnChunkZ != SpawnCoord.Z)
	{
		for (int32 x = -1; x <= 1; ++x)
		{
			for (int32 y = -1; y <= 1; ++y)
			{
				FIntVector SkyCoord = FIntVector(SpawnCoord.X + x, SpawnCoord.Y + y, SpawnChunkZ);
				if (!LoadedChunks.Contains(SkyCoord))
				{
					SpawnChunk(SkyCoord);
					InitialSpawnCoords.Add(SkyCoord);
				}
				
				if (SpawnChunkZ > 0)
				{
					FIntVector BelowCoord = FIntVector(SpawnCoord.X + x, SpawnCoord.Y + y, SpawnChunkZ - 1);
					if (!LoadedChunks.Contains(BelowCoord))
					{
						SpawnChunk(BelowCoord);
						InitialSpawnCoords.Add(BelowCoord);
					}
				}
			}
		}
	}

	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Spawn chunks initialized. Waiting for %d chunks to be ready."), InitialSpawnCoords.Num());
}

