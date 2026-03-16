#include "VoxelWorld.h"
#include "../../FirstVoxel.h"
#include "VoxelChunk.h"
#include "Generation/VoxelDensityGenerator.h"
#include "Biomes/VoxelBiomeManager.h"
#include "Biomes/VoxelBiomeGenerators.h"
#include "Components/VoxelWaterComponent.h"
#include "VoxelLogger.h"
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Components/SceneComponent.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "CoreGlobals.h" // IsEngineExitRequested
#include "TimerManager.h" // FTimerHandle / GetWorldTimerManager
#include "EngineUtils.h"

#if WITH_EDITOR
#include "Selection.h"
#include "Editor.h"
#include "Containers/Ticker.h"   // FTSTicker for deferred post-PIE rebuild
#endif

DEFINE_LOG_CATEGORY(LogVoxelWorld);

AVoxelWorld::AVoxelWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	WaterComponent = CreateDefaultSubobject<UVoxelWaterComponent>(TEXT("VoxelWaterComponent"));

	// Initialize per-biome water configs with tuned defaults
	DesertWater  = MakeDesertWaterDefaults();
	PeaksWater   = MakePeaksWaterDefaults();
	CliffsWater  = MakeCliffsWaterDefaults();
	MesaWater    = MakeMesaWaterDefaults();
	CratersWater = MakeCratersWaterDefaults();
}


AVoxelWorld::~AVoxelWorld() = default;

void AVoxelWorld::BeginPlay()
{
	if (bShutdown)
	{
		return;
	}

	if (WaterComponent)
	{
		const FVoxelGenerationConfig& Config = GetEffectiveConfig();
		WaterComponent->SeaLevel = Config.SeaLevel;
		WaterComponent->bEnableOcean = Config.Water.bEnableOcean;
		WaterComponent->OceanPlaneScale = RenderDistanceXY * ChunkSize * VoxelSize * 1.5f;
		if (Config.Water.OceanMaterial)
			WaterComponent->OceanMaterial = Config.Water.OceanMaterial;
		UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelWorld: Water synced (SeaLevel=%.0f, bEnableOcean=%d)"), Config.SeaLevel, Config.Water.bEnableOcean ? 1 : 0));
	}

	Super::BeginPlay();

	DataMap.Init(ChunkSize);
	bInitialized = true;

	DensityGenerator = MakeUnique<FVoxelDensityGenerator>();

	// Disable player gravity during initial world setup to prevent falling through empty spaces
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (ACharacter* Character = Cast<ACharacter>(Player))
	{
	}

	// Increase render distance for immersive gameplay view on runtimes

	RenderDistanceXY = 4; 
	// Expand Z heights to cover tall peaks (20 chunks * 16m = 320m above/below player).
	// With Peaks now reaching ~880m, the player needs high Z coverage to see mountain tops.
	RenderDistanceZ = 20;

	// Auto-tune: allow one chunk per physical CPU core so all threads are utilised.
	// Capped at 16 to avoid overwhelming the thread pool on machines with many cores.
	const int32 CoreCount = FMath::Max(1, FPlatformMisc::NumberOfCores());
	// Cap at 6: more than 6 simultaneous chunks causes game-thread ApplyMesh spikes on startup.
	MaxConcurrentGenerations = FMath::Clamp(CoreCount, 2, 6);
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: MaxConcurrentGenerations auto-set to %d (CPU cores: %d)"),
		MaxConcurrentGenerations, CoreCount);

	// Ramp-up: start with only 2 concurrent tasks so the first few frames stay smooth,
	// then open the throttle fully after 4 seconds when the initial burst is over.
	const int32 FinalMax = MaxConcurrentGenerations;
	MaxConcurrentGenerations = 2;
	FTimerHandle RampHandle;
	GetWorldTimerManager().SetTimer(RampHandle, [this, FinalMax]()
	{
		if (bShutdown)
		{
			return;
		}
		MaxConcurrentGenerations = FinalMax;
		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Generation ramp-up complete, MaxConcurrent=%d"), FinalMax);
	}, 4.0f, false);

	// Randomize seed on start to guarantee unique world layouts each run
	if (bRandomizeSeedOnStartup)
	{
		int32 NewSeed = FMath::Rand();
		GenerationConfig.Seed = NewSeed;
		if (BiomePreset)
		{
			BiomePreset->Config.Seed = NewSeed;
		}
		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Initial seed randomized to %d"), NewSeed);
	}

	if (bAutoGenerateOnBeginPlay)
	{
		GenerateWorld();
	}
}

void AVoxelWorld::GenerateWorld()
{
	if (!GetWorld() || bShutdown) return;

	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: GenerateWorld called."));
	UVoxelLogger::LogVoxelEvent(TEXT("VoxelWorld: GenerateWorld called."));

#if WITH_EDITOR
	// Guard against multiple concurrent generation tickers
	if (DrainTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DrainTickerHandle);
		DrainTickerHandle.Reset();
	}
#endif

	// Auto-randomize seed every time "Generate World" is pressed in the editor so
	if (!GetWorld()->IsGameWorld())
	{
		const uint64 CycleBits  = FPlatformTime::Cycles64();
		const uint64 ClockBits  = (uint64)FDateTime::Now().GetTicks();
		const int32  NewSeed    = (int32)(CycleBits ^ (ClockBits << 13) ^ (ClockBits >> 7));
		GenerationConfig.Seed   = NewSeed;
		if (BiomePreset)
			BiomePreset->Config.Seed = NewSeed;
		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Editor generate — new seed %d"), NewSeed);
	}

	if (!GetWorld()->IsGameWorld())
	{
#if WITH_EDITOR
		TWeakObjectPtr<AVoxelWorld> WeakThis(this);
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakThis](float) -> bool
			{
				AVoxelWorld* Self = WeakThis.Get();
				if (!Self || Self->bShutdown || !Self->GetWorld()) return false;
				Self->GenerateWorldDeferred();
				return false;
			}),
			0.0f);
#endif
		return;
	}
	GenerateWorldDeferred();
}

void AVoxelWorld::GenerateWorldDeferred()
{
	if (!GetWorld() || bShutdown) return;

	UVoxelLogger::LogVoxelEvent(TEXT("VoxelWorld: GenerateWorldDeferred started."));

	// 0. Reconcile existing chunks to avoid "stacking"
	DiscoverExistingChunks();

	// 1. Integrated Smart Area Search
	// Distance to jump between world seeds to avoid overlap
	float WorldRadius = RenderDistanceXY * ChunkSize * VoxelSize;
	float JumpStep = WorldRadius * 3.f; 

	FVector CandidatePos = GetActorLocation();
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
	FIntVector MinCoord(FIntVector::ZeroValue);
	FIntVector MaxCoord(FIntVector::ZeroValue);

	if (LoadedChunks.Num() > 0)
	{
		// Find the bounding box of existing chunks
		bool bFirst = true;
		for (const auto& Pair : LoadedChunks)
		{
			FIntVector C = Pair.Key;
			if (bFirst)
			{
				MinCoord = MaxCoord = C;
				bFirst = false;
			}
			else
			{
				MinCoord.X = FMath::Min(MinCoord.X, C.X);
				MinCoord.Y = FMath::Min(MinCoord.Y, C.Y);
				MinCoord.Z = FMath::Min(MinCoord.Z, C.Z);
				MaxCoord.X = FMath::Max(MaxCoord.X, C.X);
				MaxCoord.Y = FMath::Max(MaxCoord.Y, C.Y);
				MaxCoord.Z = FMath::Max(MaxCoord.Z, C.Z);
			}
		}
		
		// Expand the bounds by RenderDistance bounds
		MinCoord.X -= RenderDistanceXY;
		MinCoord.Y -= RenderDistanceXY;
		MinCoord.Z -= RenderDistanceZ;
		MaxCoord.X += RenderDistanceXY;
		MaxCoord.Y += RenderDistanceXY;
		MaxCoord.Z += RenderDistanceZ;
	}
	else
	{
		// No chunks exist, generate around 0,0,0 instead of Actor Location
		FVector Anchor = FVector(0.f, 0.f, GetEffectiveConfig().SeaLevel);
		FIntVector Origin = WorldToChunkCoord(Anchor);
		
		MinCoord = Origin - FIntVector(RenderDistanceXY, RenderDistanceXY, RenderDistanceZ);
		MaxCoord = Origin + FIntVector(RenderDistanceXY, RenderDistanceXY, RenderDistanceZ);
	}

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
			0.0f);
#endif
	}

	// 5. If at runtime, snap the player
	if (GetWorld()->IsGameWorld())
	{
		FTimerHandle TempHandle;
		GetWorldTimerManager().SetTimer(TempHandle, [this]()
		{
			if (bShutdown)
			{
				return;
			}
			APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
			if (!Player) return;

			const FVoxelGenerationConfig& Config = GetEffectiveConfig();
			FVector Pos = Player->GetActorLocation();
			Pos = SnapToVoxelGrid(Pos);
			if (bForceCraterSpawn)
			{
				Pos = FindCraterSpawnLocation(Pos, Config);
			}
			Player->SetActorLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);
			const FSkylandsLayerConfig& SC = Config.SkylandsLayer;

			const FVoxelBiomeManager::FWeightsAndHeight Wh = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(Pos.X, Pos.Y, Config);
			const FVoxelBiomeWeightMap& Weights = Wh.Weights;
			const float Surface = Wh.SurfaceHeight;

			// --- TERRAIN-DRIVEN SKYLAND SPAWN FINDER ---
			// Compute the island altitude band for this exact (X,Y) column using the same
			// formula that FVoxelDensityGenerator uses, so we always search the right height.
			const float HeightNorm    = FMath::Clamp(Surface / SC.MaxTerrainReference, 0.f, 1.f);
			const float RoughnessNorm = FMath::Clamp(Weights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);
			const float TerrainStr    = FMath::Clamp(HeightNorm * 1.5f + RoughnessNorm * 0.8f, 0.f, 1.f);
			const float AltBase       = FMath::Lerp(SC.MinAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, TerrainStr);
			const float SkyAlt        = Surface
				+ AltBase
				+ HeightNorm    * SC.HeightAltitudeBonus
				+ RoughnessNorm * SC.RoughnessAltitudeBonus;
			const float IslandHalfThick = (SC.BaseIslandSize
				+ HeightNorm    * SC.HeightSizeBonus
				+ RoughnessNorm * SC.RoughnessSizeBonus) * SC.ThicknessRatio;

			const float SearchTop = SkyAlt + IslandHalfThick;
			// Clamp SearchBot well above the terrain surface so the density probe never
			// hits solid ground and mistakes it for a skyland.
			const float SearchBot = FMath::Max(SkyAlt - IslandHalfThick, Surface + 500.f);

			const float SafeOffset = GetSafeSpawnHeightOffset();
			float TargetZ = Surface + SafeOffset; // fallback: above ground surface
			bool bFoundSkyland = false;

			static FVoxelDensityGenerator SpawnProbe;
			for (float z = SearchTop; z >= SearchBot; z -= 200.f)
			{
				if (SpawnProbe.GetDensity(Pos.X, Pos.Y, z, Config) > 0.f)
				{
					// Place clearly above first solid so we don't spawn inside mesh
					TargetZ = z + SafeOffset;
					bFoundSkyland = true;
					break;
				}
			}

			// --- ASYNC LOAD AT TARGET (include chunk at spawn Z so mesh exists before teleport) ---
			InitialSpawnCoords.Empty();
			bWaitingForInitialSpawn = true;
			TargetCoordsZ = TargetZ;
			CachedSurfaceHeight = Surface;

			const FIntVector LandCoord = WorldToChunkCoord(Pos);
			const float ChunkHeight = ChunkSize * VoxelSize;
			const int32 SpawnChunkZ = FMath::FloorToInt(TargetZ / ChunkHeight);

			for (int32 x = -1; x <= 1; ++x)
			{
				for (int32 y = -1; y <= 1; ++y)
				{
					// Load horizontal neighbors at ground (Z=0) and at spawn height so mesh is ready
					FIntVector NeighborCoord = LandCoord + FIntVector(x, y, 0);
					SpawnChunk(NeighborCoord);
					InitialSpawnCoords.Add(NeighborCoord);
					if (SpawnChunkZ != 0)
					{
						FIntVector SkyCoord = FIntVector(LandCoord.X + x, LandCoord.Y + y, SpawnChunkZ);
						if (!LoadedChunks.Contains(SkyCoord))
						{
							SpawnChunk(SkyCoord);
							InitialSpawnCoords.Add(SkyCoord);
						}
						// Also load chunk above/below spawn Z for smooth transition
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
		}, 0.8f, false);
	}
}

void AVoxelWorld::ClearWorld()
{
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Clearing World."));
	UVoxelLogger::LogVoxelEvent(TEXT("VoxelWorld: ClearWorld called."));
	
	// Flush queue
	GenerationQueue.Empty();
	QueueHead = 0;

	// Destroy all chunks
	TArray<FIntVector> Keys;
	LoadedChunks.GetKeys(Keys);
	for (const FIntVector& K : Keys)
	{
		DestroyChunk(K);
	}
	
	ChunkPool.Clear();
	
	LoadedChunks.Empty();
	DataMap.Clear();

}

void AVoxelWorld::SnapPlayerToGround()
{
#if WITH_EDITOR
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Snapping Player to Ground."));

	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	AActor* TargetActor = Player;

#if WITH_EDITOR
	if (!TargetActor && GEditor)
	{
		USelection* SelectedActors = GEditor->GetSelectedActors();
		if (SelectedActors && SelectedActors->Num() > 0)
		{
			TargetActor = SelectedActors->GetTop<AActor>();
		}
	}
#endif

	if (!TargetActor)
	{
		UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: No Actor or Player Pawn found to snap."));
		return;
	}

	FVector Pos = TargetActor->GetActorLocation();
	const FVoxelGenerationConfig& Config = GetEffectiveConfig();
	const FVoxelBiomeManager::FWeightsAndHeight Wh = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(Pos.X, Pos.Y, Config);
	const float Surface = Wh.SurfaceHeight;

	const float SafeOffset = GetSafeSpawnHeightOffset();
	// Snap player above the surface using the configured safe offset
	TargetActor->SetActorLocation(FVector(Pos.X, Pos.Y, Surface + SafeOffset), false, nullptr, ETeleportType::TeleportPhysics);
	
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Snapped actor %s to Z=%.0f"), *TargetActor->GetName(), Surface + SafeOffset);
#endif // WITH_EDITOR
}

FVector AVoxelWorld::SnapToVoxelGrid(const FVector& WorldPos) const
{
	if (VoxelSize <= 0.f)
	{
		return WorldPos;
	}

	const float HalfVoxel = VoxelSize * 0.5f;
	const float SnappedX = FMath::GridSnap(WorldPos.X, VoxelSize);
	const float SnappedY = FMath::GridSnap(WorldPos.Y, VoxelSize);
	return FVector(SnappedX + HalfVoxel, SnappedY + HalfVoxel, WorldPos.Z);
}

FVector AVoxelWorld::FindCraterSpawnLocation(const FVector& StartPos, const FVoxelGenerationConfig& Config) const
{
	if (CraterSpawnSearchRadius <= 0.f || CraterSpawnSearchStep <= 0.f)
	{
		return StartPos;
	}

	const float SearchRadius = FMath::Max(CraterSpawnSearchRadius, CraterSpawnSearchStep);
	const float Step = FMath::Max(CraterSpawnSearchStep, 100.f);

	FVector BestPos = StartPos;
	float BestWeight = -1.f;

	for (float y = -SearchRadius; y <= SearchRadius; y += Step)
	{
		for (float x = -SearchRadius; x <= SearchRadius; x += Step)
		{
			const FVector Candidate = StartPos + FVector(x, y, 0.f);
			const FVoxelBiomeWeightMap Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(
				Candidate.X, Candidate.Y, Config);
			const float CraterWeight = Weights.GetWeight(EVoxelBiome::Craters);

			if (CraterWeight > BestWeight)
			{
				BestWeight = CraterWeight;
				BestPos = Candidate;
			}
		}
	}

	if (BestWeight >= CraterSpawnMinWeight)
	{
		return BestPos;
	}

	return StartPos;
}

float AVoxelWorld::GetSafeSpawnHeightOffset() const
{
	return FMath::Max(SafeSpawnHeightOffset, VoxelSize);
}

void AVoxelWorld::SaveCurrentToPreset()
{
#if WITH_EDITOR
	if (!BiomePreset)
	{
		UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: No BiomePreset assigned. Cannot save."));
		return;
	}

	BiomePreset->Modify();
	BiomePreset->Config = GenerationConfig;
	BiomePreset->MarkPackageDirty();

	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Saved inline config to preset asset %s"), *BiomePreset->GetName());
#endif
}

void AVoxelWorld::LoadFromPreset()
{
#if WITH_EDITOR
	if (!BiomePreset)
	{
		UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: No BiomePreset assigned. Cannot load."));
		return;
	}

	Modify();
	GenerationConfig = BiomePreset->Config;

	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Loaded config from preset asset %s"), *BiomePreset->GetName());
#endif
}

void AVoxelWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if WITH_EDITOR
	const bool bIsEngineExitRequested = IsEngineExitRequested();
	if (!bIsEngineExitRequested && GEditor && GetWorld() && GetWorld()->IsPlayInEditor())
	{
		// Capture state we need for the deferred rebuild before 'this' is destroyed.
		const FString   MyLabel            = GetActorLabel();
		FVoxelGenerationConfig CapturedConfig = GenerationConfig;
		const bool      bShouldRebuild     = bRegenerateViewportAfterPIE;

		// Copy live config + DataMap onto the editor-world counterpart right now
		// (DataMap holds any voxel edits the player made during PIE).
		AVoxelWorld* EditorWorld = nullptr;
		for (TActorIterator<AVoxelWorld> It(GEditor->GetEditorWorldContext().World()); It; ++It)
		{
			if (It->GetActorLabel() == MyLabel)
			{
				It->Modify();
				It->GenerationConfig = CapturedConfig;
				It->DataMap.CopyFrom(DataMap);
				// Mirror the per-session render distances used at runtime
				It->RenderDistanceXY = RenderDistanceXY;
				It->RenderDistanceZ  = RenderDistanceZ;
				EditorWorld = *It;
				break;
			}
		}

		// Defer the RebuildWorld call by one engine tick so that PIE teardown
		// (actor destruction, GC pass) has fully completed before we try to
		// spawn new chunk actors in the editor world.
		if (bShouldRebuild && EditorWorld)
		{
			TWeakObjectPtr<AVoxelWorld> WeakEditorWorld(EditorWorld);
			FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateLambda([WeakEditorWorld](float) -> bool
				{
					if (AVoxelWorld* W = WeakEditorWorld.Get())
					{
						UE_LOG(LogVoxelWorld, Log,
							TEXT("VoxelWorld: Post-PIE viewport rebuild triggered (seed=%d)."),
							W->GetEffectiveConfig().Seed);
						W->RebuildWorld();
					}
					return false; // fire once only
				}),
				0.05f // 50ms delay — gives UE5 time to finish PIE actor cleanup
			);
		}
	}
#endif

	// Signal shutdown FIRST so in-flight async tasks skip DataMap access
	bShutdown = true;

	// Cancel any in-flight generation to prevent ActiveGenerations from sticking.
	for (auto& Pair : LoadedChunks)
	{
		if (IsValid(Pair.Value) && Pair.Value->IsGenerating())
		{
			Pair.Value->CancelGeneration();
		}
	}

	// NULL the DataMap pointer on all chunks so background threads see it gone
	for (auto& Pair : LoadedChunks)
	{
		if (IsValid(Pair.Value))
			Pair.Value->DataMap = nullptr;
	}

	// Spin-wait for all async tasks to complete (max 3 seconds to be safe)
	const double Deadline = FPlatformTime::Seconds() + 3.0;
	while (ActiveGenerations > 0 && FPlatformTime::Seconds() < Deadline)
	{
		FPlatformProcess::Sleep(0.01f);
	}

	// Destroy all loaded chunks before the DataMap goes out of scope
	for (auto& Pair : LoadedChunks)
	{
		if (IsValid(Pair.Value)) Pair.Value->Destroy();
	}
	LoadedChunks.Empty();
	
	ChunkPool.Clear();
	
	GenerationQueue.Empty();

	Super::EndPlay(EndPlayReason);
}

void AVoxelWorld::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bInitialized || bShutdown) return;
	UpdateChunkStreaming();
	DrainGenerationQueue();





	// --- Continuous Dig/Build Throttle ---
	for (auto& It : LoadedChunks)
	{
		if (It.Value && It.Value->bMeshDirty && !It.Value->IsGenerating())
		{
			It.Value->bMeshDirty = false;
			It.Value->DestroyAndRebuildMesh();
		}
	}

	// --- Startup Spawn Tracking ---
	if (bWaitingForInitialSpawn)
	{
		bool bAllDone = true;
		for (const FIntVector& C : InitialSpawnCoords)
		{
			AVoxelChunk** Ptr = LoadedChunks.Find(C);
			if (!Ptr || (*Ptr)->IsGenerating())
			{
				bAllDone = false;
				break;
			}
		}

		if (bAllDone)
		{
			bWaitingForInitialSpawn = false;

			APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
			if (Player)
			{
				FVector Pos = Player->GetActorLocation();
				const float SafeOffset = GetSafeSpawnHeightOffset();
				// Ensure Z is above surface and above any detected solid so we don't spawn inside mesh
				const float SafeZ = FMath::Max(TargetCoordsZ, CachedSurfaceHeight + SafeOffset);
				Player->SetActorLocation(FVector(Pos.X, Pos.Y, SafeZ), false, nullptr, ETeleportType::TeleportPhysics);
				UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Async Spawn Finished. Set player at Z=%.0f (surface+offset=%.0f)"), SafeZ, CachedSurfaceHeight + SafeOffset);
				UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelWorld: Async spawn finished, player Z=%.0f"), SafeZ));

				if (ACharacter* Character = Cast<ACharacter>(Player))
				{
					if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
					{
						MoveComp->SetMovementMode(MOVE_Walking);
					}
				}
			}
		}
	}
}

void AVoxelWorld::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
}

void AVoxelWorld::SetVoxelSphere(FVector WorldPosition, float Radius, float DensityValue, bool bRebuildChunks)
{
	DataMap.SetSphere(WorldPosition, Radius, DensityValue, VoxelSize);

	if (bRebuildChunks)
	{
		const float R = Radius + VoxelSize * 2.f;
		for (auto& It : LoadedChunks)
		{
			FVector ChunkPos = ChunkCoordToWorld(It.Key);
			FVector Closest = FVector(
				FMath::Clamp(WorldPosition.X, ChunkPos.X, ChunkPos.X + ChunkSize * VoxelSize),
				FMath::Clamp(WorldPosition.Y, ChunkPos.Y, ChunkPos.Y + ChunkSize * VoxelSize),
				FMath::Clamp(WorldPosition.Z, ChunkPos.Z, ChunkPos.Z + ChunkSize * VoxelSize)
			);

			if (FVector::DistSquared(WorldPosition, Closest) < R * R)
			{
				It.Value->bMeshDirty = true;
			}
		}
	}
}

void AVoxelWorld::UpdateChunkStreaming()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player) 
	{
		return;
	}

	StreamingTimer += GetWorld()->GetDeltaSeconds();
	if (StreamingTimer < StreamingInterval) return;
	StreamingTimer = 0.f;

	// --- ⚡ Optimization: Skip building streaming volumes if player is stationary ---
	const FVector CurrentPos = Player->GetActorLocation();
	const float MinStep = ChunkSize * VoxelSize * 0.4f; 
	if (FVector::DistSquared(CurrentPos, LastStreamedPos) < MinStep * MinStep)
	{
		return; // No movement, preserve CPU budget
	}
	LastStreamedPos = CurrentPos;


	// Use full 3D player position for volumetric streaming.
	FVector PlayerPos = Player->GetActorLocation();
	FIntVector PlayerCoord = WorldToChunkCoord(PlayerPos);

	const FVoxelGenerationConfig& Config = GetEffectiveConfig();
	const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
	const float ChunkWorldSize = ChunkSize * VoxelSize;

	// Compute the skyland altitude directly above the player's current terrain so
	// that over mountains (surface ~80,000cm) islands at ~100,000cm are streamed in.
	// The old approach used only BaseAltitudeAboveTerrain (4500cm), placing the
	// skylands pass at chunk Z ~3, while mountain skylands live at chunk Z ~60+.
	const FVoxelBiomeManager::FWeightsAndHeight Wh = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(
		PlayerPos.X, PlayerPos.Y, Config);
	const float PlayerSurfH = Wh.SurfaceHeight;

	const float HeightNormSky    = FMath::Clamp(PlayerSurfH / SC.MaxTerrainReference, 0.f, 1.f);
	const float RoughnessNormSky = FMath::Clamp(Wh.Weights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);
	const float TerrainStrSky    = FMath::Clamp(HeightNormSky * 1.5f + RoughnessNormSky * 0.8f, 0.f, 1.f);
	const float CurvedH          = FMath::Pow(HeightNormSky,    2.5f);
	const float CurvedR          = FMath::Pow(RoughnessNormSky, 2.0f);
	const float AltBase          = FMath::Lerp(SC.MinAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, TerrainStrSky);
	const float SkyAltWorld      = PlayerSurfH + AltBase
		                           + CurvedH * SC.HeightAltitudeBonus
		                           + CurvedR * SC.RoughnessAltitudeBonus;

	// Island vertical half-thickness in chunks (+ 2 margin chunks on each side)
	const float IslandSize    = FMath::Max(SC.BaseIslandSize,
		                          SC.BaseIslandSize + CurvedH * SC.HeightSizeBonus + CurvedR * SC.RoughnessSizeBonus);
	const float HalfThickCm   = FMath::Max(200.f, IslandSize * SC.ThicknessRatio);
	const int32 SkyThickness  = FMath::CeilToInt(HalfThickCm / ChunkWorldSize) + 2;
	const int32 SkyZCoordCenter = FMath::RoundToInt(SkyAltWorld / ChunkWorldSize);
	TSet<FIntVector> Desired;

	// 1. Ground area

	for (int32 z = -RenderDistanceZ; z <= RenderDistanceZ; ++z)
	for (int32 y = -RenderDistanceXY; y <= RenderDistanceXY; ++y)
	for (int32 x = -RenderDistanceXY; x <= RenderDistanceXY; ++x)
	{
		Desired.Add(PlayerCoord + FIntVector(x, y, z));
	}

	// 2. Skylands Pass: larger range to maintain distance visibility without overloading ground chunks
	for (int32 z = -SkyThickness; z <= SkyThickness; ++z)
	for (int32 y = -SkylandsRenderDistanceXY; y <= SkylandsRenderDistanceXY; ++y)
	for (int32 x = -SkylandsRenderDistanceXY; x <= SkylandsRenderDistanceXY; ++x)
	{
		Desired.Add(FIntVector(PlayerCoord.X + x, PlayerCoord.Y + y, SkyZCoordCenter + z));
	}


	TArray<FIntVector> ToRemove;
	for (auto& It : LoadedChunks)
	{
		if (!Desired.Contains(It.Key)) ToRemove.Add(It.Key);
	}
	for (const FIntVector& C : ToRemove) DestroyChunk(C);

	// --- 3. DYNAMIC LOD MULTIPLIERS FOR EXISTING CHUNKS ---
	for (auto& It : LoadedChunks)
	{
		AVoxelChunk* Chunk = It.Value;
		if (IsValid(Chunk))
		{
			// Midpoint of the chunk for accurate spherical distance
			FVector ChunkPos = ChunkCoordToWorld(It.Key) + FVector(ChunkSize * VoxelSize * 0.5f);
			float DistSq = FVector::DistSquared(PlayerPos, ChunkPos);

			int32 TargetLOD = 0;
			if (DistSq > LOD2Distance * LOD2Distance) TargetLOD = 2;
			else if (DistSq > LOD1Distance * LOD1Distance) TargetLOD = 1;

			if (Chunk->LOD != TargetLOD)
			{
				Chunk->LOD = TargetLOD;
				// Trigger async mesh rebuild with new StepSize
				Chunk->DestroyAndRebuildMesh(); 
			}
		}
	}

	// Add new desired chunks, sorted nearest-first
	TArray<TPair<int32, FIntVector>> NewChunks;

	// OPTIMIZATION: convert queue to set for O(1) lookup to prevent main-thread freeze with large volumes
	TSet<FIntVector> QueueSet(GenerationQueue);

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
	NewChunks.Sort([](const TPair<int32,FIntVector>& A, const TPair<int32,FIntVector>& B){ return A.Key < B.Key; });
	
	const int32 NumNew = NewChunks.Num();
	if (NumNew > 0)
	{
		GenerationQueue.InsertDefaulted(0, NumNew);
		for (int32 i = 0; i < NumNew; ++i)
		{
			GenerationQueue[i] = NewChunks[i].Value;
		}
	}
}

void AVoxelWorld::DrainGenerationQueue()
{
	const bool bIsEditor = !GetWorld()->IsGameWorld();

	// In gameplay & Editor, run async with the configured concurrency limit.
	const int32 Limit = MaxConcurrentGenerations;


	int32 ProcessedThisTick = 0;

	while (ActiveGenerations < Limit && QueueHead < GenerationQueue.Num())
	{
		const FIntVector Coord = GenerationQueue[QueueHead++];
		if (!LoadedChunks.Contains(Coord))
		{
			SpawnChunk(Coord);
		}
	}


	// Compact the queue periodically to reclaim memory
	if (QueueHead > 100)
	{
		GenerationQueue.RemoveAt(0, QueueHead);
		QueueHead = 0;
	}
}

void AVoxelWorld::SpawnChunk(const FIntVector& Coord)
{
	if (LoadedChunks.Contains(Coord)) return;

	FVector Loc = ChunkCoordToWorld(Coord);
	AVoxelChunk* Chunk = nullptr;

	Chunk = ChunkPool.RetrieveOrCreateChunk(GetWorld(), Loc, this);

	if (!Chunk) return;

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
	ConfigureChunk(Chunk);

	LoadedChunks.Add(Coord, Chunk);
	
	// Async generation enabled for both games and Editor runs
	ActiveGenerations++;
	Chunk->OnGenerationComplete = [this]() { ActiveGenerations--; };
	Chunk->GenerateAsync();

}

void AVoxelWorld::DestroyChunk(const FIntVector& Coord)
{
	AVoxelChunk** ChunkPtr = LoadedChunks.Find(Coord);
	if (ChunkPtr && *ChunkPtr)
	{
		AVoxelChunk* Chunk = *ChunkPtr;
		if (Chunk->IsGenerating())
		{
			Chunk->CancelGeneration();
			Chunk->Destroy();
		}
		else
		{
			Chunk->ClearMesh(); // Clear old buffers before placing into pool
			ChunkPool.ReturnChunk(Chunk);
		}

	}
	LoadedChunks.Remove(Coord);
}

void AVoxelWorld::ConfigureChunk(AVoxelChunk* Chunk) const
{
	Chunk->ChunkSize             = ChunkSize;
	Chunk->VoxelSize             = VoxelSize;
	Chunk->MasterFlatMaterial    = MasterFlatMaterial;
	Chunk->MasterSlopeMaterial   = MasterSlopeMaterial;
	Chunk->SlopeThreshold        = SlopeThreshold;
	Chunk->DataMap               = const_cast<FVoxelDataMap*>(&DataMap);
	Chunk->TreeMesh              = TreeMesh;
	Chunk->GrassMesh             = GrassMesh;
	Chunk->FoliageDensity        = FoliageDensity;
	Chunk->MaxFoliageSlope       = MaxFoliageSlope;
	// Pass the effective config so BiomePreset DataAsset overrides are honoured
	Chunk->GenerationConfig      = GetEffectiveConfig();
	// Inject the per-biome render configs from the world-level Details-panel properties
	// into the config copy the chunk and task will use.  These are not UPROPERTYs inside
	// FVoxelGenerationConfig so they never appear there — they live on the world actor.
	Chunk->GenerationConfig.ForestRender   = ForestRender;
	Chunk->GenerationConfig.DesertRender   = DesertRender;
	Chunk->GenerationConfig.PeaksRender    = PeaksRender;
	Chunk->GenerationConfig.CliffsRender   = CliffsRender;
	Chunk->GenerationConfig.MesaRender     = MesaRender;
	Chunk->GenerationConfig.CratersRender  = CratersRender;
	Chunk->GenerationConfig.SkylandsRender = SkylandsRender;

	// Inject per-biome water configs into the chunk's generation config
	Chunk->GenerationConfig.ForestWater    = ForestWater;
	Chunk->GenerationConfig.DesertWater    = DesertWater;
	Chunk->GenerationConfig.PeaksWater     = PeaksWater;
	Chunk->GenerationConfig.CliffsWater    = CliffsWater;
	Chunk->GenerationConfig.MesaWater      = MesaWater;
	Chunk->GenerationConfig.CratersWater   = CratersWater;
	Chunk->GenerationConfig.SkylandsWater  = SkylandsWater;

	if (DensityGenerator.IsValid())
	{
		Chunk->DensityGenerator = DensityGenerator.Get();
	}

	// Calculate LOD based on distance to player
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (Player)
	{
		float Dist = FVector::Dist(Player->GetActorLocation(), Chunk->GetActorLocation());
		if (Dist > LOD2Distance)      Chunk->LOD = 2;
		else if (Dist > LOD1Distance) Chunk->LOD = 1;
		else                          Chunk->LOD = 0;
	}

	// (World Border logic removed for infinite world)
}

FIntVector AVoxelWorld::WorldToChunkCoord(const FVector& WorldPos) const
{
	float S = ChunkSize * VoxelSize;
	if (S <= 0.f) return FIntVector(0,0,0);
	return FIntVector(
		FMath::FloorToInt(WorldPos.X / S),
		FMath::FloorToInt(WorldPos.Y / S),
		FMath::FloorToInt(WorldPos.Z / S)
	);
}

FVector AVoxelWorld::ChunkCoordToWorld(const FIntVector& Coord) const
{
	float S = ChunkSize * VoxelSize;
	return FVector(Coord.X * S, Coord.Y * S, Coord.Z * S);
}

void AVoxelWorld::SaveToFile(const FString& SlotName) {}
void AVoxelWorld::LoadFromFile(const FString& SlotName) {}
void AVoxelWorld::ClearWorldModifications() { DataMap.Clear(); }

void AVoxelWorld::RebuildWorld()
{
	// 1. Absolute cleanup of ALL VoxelChunk actors in the level to prevent overlaps
	TArray<AActor*> AllChunks;
	UGameplayStatics::GetAllActorsOfClass(this, AVoxelChunk::StaticClass(), AllChunks);
	for (AActor* Chunk : AllChunks)
	{
		if (Chunk) Chunk->Destroy();
	}

	LoadedChunks.Empty();
	ChunkPool.Clear();
	GenerationQueue.Empty();
	QueueHead = 0;

	// 2. Reset data limits and trigger loop
	DataMap.Init(ChunkSize);
	GenerateWorld();
	
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Rebuild triggered. All chunk actors destroyed."));
}

void AVoxelWorld::RandomizeSeed()
{
	// GenerateWorld() (called inside RebuildWorld) already picks a fresh seed via
	// hardware entropy on the editor path, so we just trigger a rebuild here.
	// The Seed field in the Details Panel updates automatically after generation.
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: RandomizeSeed triggered — rebuilding with new entropy seed."));
	RebuildWorld();
}

void AVoxelWorld::ClearModifications()
{
	DataMap.Clear();
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Modifications Cleared."));
}

void AVoxelWorld::SaveDefaultSlot()
{
	SaveToFile(TEXT("DefaultSlot"));
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Saved to DefaultSlot."));
}

void AVoxelWorld::LoadDefaultSlot()
{
	LoadFromFile(TEXT("DefaultSlot"));
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Loaded to DefaultSlot."));
}

void AVoxelWorld::RunTests()
{
	RunVoxelTests();
}
float AVoxelWorld::GetTerrainHeight(float X, float Y) const
{
	return FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(X, Y, GetEffectiveConfig()).SurfaceHeight;
}
float AVoxelWorld::GetSurfaceZ(float X, float Y) const { return GetTerrainHeight(X, Y); }

void AVoxelWorld::OnChunkGenerationComplete() { ActiveGenerations--; }

void AVoxelWorld::RunVoxelTests()
{
	UE_LOG(LogVoxelWorld, Log, TEXT("--- Starting Voxel System Tests ---"));

	float TestX = 1000.f;
	float TestY = 1000.f;

	// 1. Biome Weight Test
	FVoxelBiomeWeightMap Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(TestX, TestY, GetEffectiveConfig());
	float TotalWeight = 0.f;
	for (int32 i = 0; i < FVoxelBiomeWeightMap::MaxBiomes; ++i) TotalWeight += Weights[i];

	if (FMath::IsNearlyEqual(TotalWeight, 1.0f, 0.01f))
	{
		UE_LOG(LogVoxelWorld, Log, TEXT("[PASS] Biome Weights correctly normalized (%.2f)"), TotalWeight);
	}
	else
	{
		UE_LOG(LogVoxelWorld, Error, TEXT("[FAIL] Biome Weights NOT normalized! Total: %.2f"), TotalWeight);
	}

	// 2. Surface Height Test
	float Height = FVoxelBiomeManager::GetSurfaceHeightStatic(TestX, TestY, Weights, GetEffectiveConfig());
	UE_LOG(LogVoxelWorld, Log, TEXT("[INFO] Sampled Surface Height at (%.f, %.f): %.f"), TestX, TestY, Height);
	if (FMath::Abs(Height) < 100000.f)
	{
		UE_LOG(LogVoxelWorld, Log,     TEXT("[PASS] Surface Height within reasonable range."));
	}
	else
	{
		UE_LOG(LogVoxelWorld, Warning, TEXT("[WARN] Surface Height extreme: %.f"), Height);
	}

	// 3. Density Gradient Test â€” use the full 3-layer generator
	static FVoxelDensityGenerator TestGen;
	float DensityAbove = TestGen.GetDensity(TestX, TestY, Height + 500.f, GetEffectiveConfig());
	float DensityBelow = TestGen.GetDensity(TestX, TestY, Height - 500.f, GetEffectiveConfig());
	if (DensityAbove < 0.f && DensityBelow > 0.f)
	{
		UE_LOG(LogVoxelWorld, Log,   TEXT("[PASS] Density gradient correct (Above: %.2f, Below: %.2f)"), DensityAbove, DensityBelow);
	}
	else
	{
		UE_LOG(LogVoxelWorld, Error, TEXT("[FAIL] Density gradient BROKEN (Above: %.2f, Below: %.2f)"), DensityAbove, DensityBelow);
	}

	UE_LOG(LogVoxelWorld, Log, TEXT("--- Voxel System Tests Complete ---"));
}

void AVoxelWorld::RebuildChunk(const FIntVector& Coord) {}

#if WITH_EDITOR
void AVoxelWorld::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif



void AVoxelWorld::DiscoverExistingChunks()
{
	if (!GetWorld()) return;

	int32 FoundCount = 0;

	for (TActorIterator<AVoxelChunk> It(GetWorld()); It; ++It)
	{
		AVoxelChunk* Chunk = *It;
		// Relax owner check to capture saved chunks without owner references
		if (IsValid(Chunk) && (Chunk->GetOwner() == this || Chunk->GetOwner() == nullptr))
		{
			// Recalculate coordinates from position for absolute accuracy
			FIntVector Coord = WorldToChunkCoord(Chunk->GetActorLocation());
			
			// Claim ownership so chunk updates track them
			Chunk->SetOwner(this);
			Chunk->ChunkCoord = Coord;

#if WITH_EDITOR
			// Organize in the World Outliner
			FString FolderName = FString::Printf(TEXT("g_VoxelChunks/Z%d"), Coord.Z);
			if (Chunk->GetFolderPath() != FName(*FolderName))
			{
				Chunk->SetFolderPath(FName(*FolderName));
			}
#endif

			if (!LoadedChunks.Contains(Coord))
			{
				LoadedChunks.Add(Coord, Chunk);
				FoundCount++;
			}
		}
	}

	if (FoundCount > 0)
	{
		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Discovered and re-tracked %d existing chunks in Outliner."), FoundCount);
	}
}

// --- Unreal Automation Tests ---
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelBiomeWeightTest, "FirstVoxel.Voxel.BiomeWeights", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)
bool FVoxelBiomeWeightTest::RunTest(const FString& Parameters)
{
    FVoxelBiomeWeightMap Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(1000.f, 1000.f, FVoxelGenerationConfig{});
    float TotalWeight = 0.f;
    for (int32 i = 0; i < FVoxelBiomeWeightMap::MaxBiomes; ++i) TotalWeight += Weights[i];
    TestTrue(TEXT("Biome weights should sum to 1.0"), FMath::IsNearlyEqual(TotalWeight, 1.0f, 0.01f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelDensityGradientTest, "FirstVoxel.Voxel.DensityGradient", EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)
bool FVoxelDensityGradientTest::RunTest(const FString& Parameters)
{
	static FVoxelDensityGenerator Gen;
	FVoxelBiomeWeightMap Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(100.f, 100.f, FVoxelGenerationConfig{});
	float Height = FVoxelBiomeManager::GetSurfaceHeightStatic(100.f, 100.f, Weights, FVoxelGenerationConfig{});
	float Above  = Gen.GetDensity(100.f, 100.f, Height + 1000.f, FVoxelGenerationConfig{});
	float Below  = Gen.GetDensity(100.f, 100.f, Height - 1000.f, FVoxelGenerationConfig{});
	TestTrue(TEXT("Density above surface should be Air (<0)"),    Above < 0.f);
	TestTrue(TEXT("Density below surface should be Solid (>0)"), Below > 0.f);
	return true;
}
#endif