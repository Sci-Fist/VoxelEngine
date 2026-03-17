// VoxelWorld_Generation.cpp
// 
// Core world generation implementation for the voxel engine.
// Contains all generation-related functions separated from the main VoxelWorld class
// to improve code organization and maintainability.
//
// GENERATION PIPELINE:
// 1. World Discovery: Locate existing chunks and avoid overlap
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


// ============================================================
//  World Generation Implementation
// ============================================================

void AVoxelWorld::GenerateWorldDeferred()
{
	if (!GetWorld() || bShutdown) return;

	UVoxelLogger::LogVoxelEvent(TEXT("VoxelWorld: GenerateWorldDeferred started."));
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: GenerateWorldDeferred started at location %s"), *GetActorLocation().ToString());

	// 0. Reconcile existing chunks to avoid "stacking"
	DiscoverExistingChunks();

	// 1. Integrated Smart Area Search
	// Distance to jump between world seeds to avoid overlap
	float WorldRadius = RenderDistanceXY * ChunkSize * VoxelSize;
	float JumpStep = WorldRadius * 3.f; 

	FVector CandidatePos = GetActorLocation();

	// Prioritise centering generation on the PlayerStart to accurately match Play Mode centering
	TArray<AActor*> PlayerStarts;
	UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), PlayerStarts);
	
	// If bForceCraterSpawn is enabled, find the best crater spot near the
	// PlayerStart BEFORE queuing chunks so the entire generation is centred on
	// the crater rather than on the fixed editor PlayerStart position.
	// This prevents the "always same mountain at spawn" issue caused by always
	// anchoring generation to the same hard-coded PlayerStart XY in the editor.
	if (bForceCraterSpawn)
	{
		// Always search from world origin (0,0) — NOT from the PlayerStart's
		// current position. The PlayerStart gets moved each generation, so
		// using it as the search base makes every run find the same relative
		// crater from the last run's position. Searching from (0,0) with the
		// new seed means every generation scans fresh and finds a different crater.
		const FVoxelGenerationConfig& Cfg = GetEffectiveConfig();
		const FVector SearchOrigin(0.f, 0.f, 0.f);
		CandidatePos   = FindCraterSpawnLocation(SearchOrigin, Cfg);
		CandidatePos.Z = 0.f;
		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Crater spawn at %s (seed %d)"), *CandidatePos.ToString(), Cfg.Seed);

		// Move PlayerStart to the found crater XY so UE's GameMode spawns the
		// pawn near the right place before ProcessInitialPlayerSpawn fires.
		if (PlayerStarts.Num() > 0 && PlayerStarts[0])
		{
			if (PlayerStarts[0]->GetRootComponent())
				PlayerStarts[0]->GetRootComponent()->SetMobility(EComponentMobility::Movable);
			FVector PSPos = PlayerStarts[0]->GetActorLocation();
			PSPos.X = CandidatePos.X;
			PSPos.Y = CandidatePos.Y;
			PlayerStarts[0]->SetActorLocation(PSPos, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	else if (PlayerStarts.Num() > 0 && PlayerStarts[0])
	{
		CandidatePos = PlayerStarts[0]->GetActorLocation();
		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Centering GenerateWorld on PlayerStart %s"), *CandidatePos.ToString());
	}
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

	if (GetActorLocation() != CandidatePos)
	{
		SetActorLocation(CandidatePos);
		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Relocated to suitable area at %s"), *CandidatePos.ToString());
	}

	// 3. Ensure DataMap is initialized (critical for editor calls)
	if (!bInitialized)
	{
		DataMap.Init(ChunkSize);
		bInitialized = true;
	}

	// RESET active generations for editor calls. 
	ActiveGenerations = 0;
	GenerationQueue.Empty();
	QueueHead = 0;

	// 4. Calculate generation bounds
	// Always generate bounds strictly centered on the AVoxelWorld actor location.
	// Bounding boxes spanning LoadedChunks can explode to millions of
	// iterations if those chunks are disconnected on the grid for any reason.
	FVector Anchor = GetActorLocation();
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
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Queued %d new chunks to extend the world."), GenerationQueue.Num());
	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelWorld: Queued %d chunks."), GenerationQueue.Num()));

	if (GenerationQueue.Num() == 0)
	{
		UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: No chunks were queued for generation. This may indicate an issue with world bounds or chunk coordinates."));
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

	// 5. If at runtime, snap the player
	if (GetWorld()->IsGameWorld())
	{
		FTimerHandle TempHandle;
		TWeakObjectPtr<AVoxelWorld> WeakThis(this);
		// 2.5s gives the initial nearby chunks time to fully generate before
		// snapping the player. With MaxConcurrentGenerations=12 and drain=8,
		// the spawn-area chunks (3x3x3 = 27) finish well within this window.
		GetWorldTimerManager().SetTimer(TempHandle, [WeakThis]()
		{
			if (AVoxelWorld* StrongThis = WeakThis.Get())
			{
				if (StrongThis->bShutdown) return;
				StrongThis->ProcessInitialPlayerSpawn();
			}
		}, 2.5f, false);
	}
}

void AVoxelWorld::SpawnChunk(const FIntVector& Coord)
{
	if (LoadedChunks.Contains(Coord)) return;

	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Spawning chunk at coordinate (%d, %d, %d)"), Coord.X, Coord.Y, Coord.Z);

	FVector Loc = ChunkCoordToWorld(Coord);
	AVoxelChunk* Chunk = nullptr;

	Chunk = ChunkPool.RetrieveOrCreateChunk(GetWorld(), Loc, this);
	if (!Chunk) return;

	// FIX: Hide the actor IMMEDIATELY so recycled pool chunks do not flash
	// older components while the async generation task runs in the background.
	Chunk->SetActorHiddenInGame(true);
	if (Chunk->GetProceduralMesh()) Chunk->GetProceduralMesh()->SetVisibility(false);

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
	Chunk->OnGenerationComplete = [WeakThis]() 
	{ 
		if (AVoxelWorld* StrongThis = WeakThis.Get())
		{
			StrongThis->ActiveGenerations--; 
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
	const int32 Limit = bIsEditor ? 2 : 8; // Editor: 2 per tick, Game: up to 8

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

	// Compact the queue periodically to reclaim memory
	if (QueueHead > 100)
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

void AVoxelWorld::OnChunkGenerationComplete()
{
	ActiveGenerations = FMath::Max(0, ActiveGenerations - 1);
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
	Chunk->MasterFlatMaterial = MasterFlatMaterial;
	Chunk->MasterSlopeMaterial= MasterSlopeMaterial;
	Chunk->SlopeThreshold     = SlopeThreshold;
	Chunk->GenerationConfig   = EffectiveConfig;
	Chunk->TreeMesh           = TreeMesh;
	Chunk->GrassMesh          = GrassMesh;
	Chunk->FoliageDensity     = FoliageDensity;
	Chunk->MaxFoliageSlope    = MaxFoliageSlope;

	// CRITICAL: Inject the world-level density generator so the chunk uses
	// the full 3-layer pipeline (Surface + Skylands + Caves).
	// Without this the chunk falls back to a plain static FVoxelDensityGenerator
	// which is functionally equivalent but skips any future per-world overrides.
	Chunk->DensityGenerator   = DensityGenerator.Get();

	// Wire water material from the world-level config.
	Chunk->WaterMaterial      = GenerationConfig.Water.OceanMaterial.Get();
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
	FVector Pos = FVector::ZeroVector;
	TArray<AActor*> PlayerStarts;
	UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), PlayerStarts);
	if (PlayerStarts.Num() > 0 && PlayerStarts[0])
	{
		Pos = PlayerStarts[0]->GetActorLocation();
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
	if (SkyAlt > Surface + 5000.f)
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

	// Restore the player that was frozen in BeginPlay.
	Player->SetActorHiddenInGame(false);
	ACharacter* SpawnChar = Cast<ACharacter>(Player);
	if (SpawnChar && SpawnChar->GetCharacterMovement())
	{
		SpawnChar->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}

	// Re-capture viewport input focus so the player can move immediately
	// without needing to left-click first. DisableMovement() + the hidden-pawn
	// period causes UE to lose viewport focus; we restore it explicitly here.
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		FInputModeGameOnly GameInputMode;
		PC->SetInputMode(GameInputMode);
		PC->SetShowMouseCursor(false);
		// FlushPressedKeys clears any stale held-key state accumulated
		// during the frozen period so movement doesn't "lurch" on restore.
		PC->FlushPressedKeys();
	}

	Pos.Z = TargetZ;
	Player->SetActorLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);
	
	// FIXED: Initialize spawn tracking
	InitialSpawnCoords.Empty();
	bWaitingForInitialSpawn = true;
	TargetCoordsZ = TargetZ;
	CachedSurfaceHeight = Surface;

	// FIXED: Spawn chunks around the player position
	const FIntVector LandCoord = WorldToChunkCoord(Pos);
	const float ChunkHeight = ChunkSize * VoxelSize;
	const int32 SpawnChunkZ = FMath::FloorToInt(TargetZ / ChunkHeight);

	// Spawn ground chunks
	for (int32 x = -1; x <= 1; ++x)
	{
		for (int32 y = -1; y <= 1; ++y)
		{
			FIntVector NeighborCoord = LandCoord + FIntVector(x, y, 0);
			SpawnChunk(NeighborCoord);
			InitialSpawnCoords.Add(NeighborCoord);
		}
	}

	// Spawn skyland chunks if needed
	if (bFoundSkyland && SpawnChunkZ != 0)
	{
		for (int32 x = -1; x <= 1; ++x)
		{
			for (int32 y = -1; y <= 1; ++y)
			{
				FIntVector SkyCoord = FIntVector(LandCoord.X + x, LandCoord.Y + y, SpawnChunkZ);
				if (!LoadedChunks.Contains(SkyCoord))
				{
					SpawnChunk(SkyCoord);
					InitialSpawnCoords.Add(SkyCoord);
				}
				
				if (SpawnChunkZ > 0)
				{
					FIntVector BelowCoord = FIntVector(LandCoord.X + x, LandCoord.Y + y, SpawnChunkZ - 1);
					if (!LoadedChunks.Contains(BelowCoord))
					{
						SpawnChunk(BelowCoord);
						InitialSpawnCoords.Add(BelowCoord);
					}
				}
			}
		}
	}
}

