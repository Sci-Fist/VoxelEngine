// VoxelWorld.cpp
// Core implementation of AVoxelWorld.
// Generation, streaming, modification, and water logic live in separate
// _Generation / _Streaming / _Modification .cpp files to keep compile units small.
//
// BUG FIXES IN THIS REVISION:
//   - DensityGenerator and WaterSimulator are now properly initialized in BeginPlay()
//   - TickWater now correctly runs the sim step and rebuilds dirty water meshes
//   - InitChunkWater now wires OnChunkWaterReady so sources reach the simulator
//   - PostEditChangeProperty is correctly wrapped in WITH_EDITOR
//   - EndPlay properly cancels all chunks before clearing LoadedChunks

#include "VoxelWorld.h"
#include "Voxel/Core/World/Water/VoxelWorldWater.h"
#include "Voxel/Core/VoxelChunk.h"
#include "Voxel/Core/VoxelChunkPool.h"
#include "Voxel/Core/VoxelDataMap.h"
#include "Voxel/Generation/VoxelGeneratorTask.h"
#include "Voxel/Generation/VoxelDensityGenerator.h"
#include "Voxel/Biomes/VoxelBiomeManager.h"
#include "FirstVoxelHUD.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "Voxel/VoxelLogger.h"
#include "Voxel/Water/VoxelWaterSimulator.h"
#include "Voxel/Water/VoxelWaterComponent.h"
#include "Engine/World.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Misc/DateTime.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformProcess.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

DEFINE_LOG_CATEGORY(LogVoxelWorld);

// ============================================================
//  Constructor
// ============================================================
AVoxelWorld::AVoxelWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
	RootComponent = Root;

	WaterComponent = CreateDefaultSubobject<UVoxelWaterComponent>(TEXT("WaterComponent"));

	WaterSystemComponent = CreateDefaultSubobject<UVoxelWorldWaterComponent>(TEXT("WaterSystemComponent"));

	// Default to false so the game shows the Title Screen overlay on startup
	bAutoGenerateOnBeginPlay = false;
}

AVoxelWorld::~AVoxelWorld()
{
	// TUniquePtr members auto-destruct; nothing extra needed here.
}

// ============================================================
//  BeginPlay
//  Critical: initialize DensityGenerator + WaterSimulator here,
//  not in the constructor, so ChunkSize / VoxelSize are final.
// ============================================================
void AVoxelWorld::BeginPlay()
{
	Super::BeginPlay();

	// Clean up any unpossessed placeholder characters placed in the Editor 
	// (prevents double spawning alongside the GameMode dynamic player spawn).
	TArray<AActor*> FoundCharacters;
	UGameplayStatics::GetAllActorsOfClass(this, APawn::StaticClass(), FoundCharacters);
	for (AActor* Act : FoundCharacters)
	{
		APawn* P = Cast<APawn>(Act);
		// If it's a Pawn, not controlled by a player controller, and not the current local viewer
		if (P && !P->IsPlayerControlled() && !P->IsPawnControlled())
		{
			UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: Destroying unpossessed editor-placed duplicate actor %s to fix double spawn."), *Act->GetName());
			Act->Destroy();
		}
	}

	// ---- Density generator (all 3 layers: Surface / Skylands / Caves) ----
	DensityGenerator = MakeUnique<FVoxelDensityGenerator>();

	// ---- Water subsystem component ----
	if (WaterSystemComponent)
	{
		const FVoxelGenerationConfig& Config = GetEffectiveConfig();
		if (WaterComponent)
		{
			// Disable static mesh ocean if using voxel ocean for seamless integration
			WaterComponent->bEnableOcean = !Config.Water.bUseVoxelOcean && Config.Water.bEnableOcean;
			WaterComponent->SeaLevel     = Config.SeaLevel;
		}
		
		WaterSystemComponent->Initialize(MakeUnique<FVoxelWaterSimulator>(ChunkSize, VoxelSize), WaterComponent);
	}

	// ---- Data map (tracks player edits) ----
	if (!bInitialized)
	{
		DataMap.Init(ChunkSize);
		bInitialized = true;
	}

	// ---- Optional random seed ----
	// Removed to ensure Editor previews match Gameplay 1:1. Use explicit Editor Button to randomize seed.

	// FIX: Hide player actor and show title screen until world generation is complete
	// This ensures the player doesn't see the character floating in empty space
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (Player)
	{
		// Hide player actor completely until generation is complete
		Player->SetActorHiddenInGame(true);
		Player->SetActorEnableCollision(false);
		
		// Freeze movement during generation. MOVE_None is used intentionally here:
		// the hover-lock in Tick will release via MOVE_Falling + bJustTeleported
		// once spawn chunks are ready, which lets UE's landing detection run
		// and correctly transition into MOVE_Walking + grounded anim state.
		if (ACharacter* Character = Cast<ACharacter>(Player))
		{
			if (UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
			{
				CMC->SetMovementMode(EMovementMode::MOVE_None);
			}
		}
	}

	// FIX: Show load bar during generation
	// This ensures the player sees progress during world generation
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
		{
			HUD->bShowLoadBar = true;
			HUD->LoadProgress = 0.0f;
		}
	}

	if (bAutoGenerateOnBeginPlay)
	{
		if (bRandomizeSeedOnStartup)
		{
			RandomizeSeed();
		}
		ClearWorld();
		GenerateWorldDeferred();
	}
	else
	{
		DiscoverExistingChunks(); // Find editor-placed chunks
		ClearWorld();            // Wipe any prior Editor generated chunks
		// Notify HUD to show Title Screen
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
			{
				HUD->bShowTitleScreen = true;
			}
		}
	}
}

// ============================================================
//  EndPlay
// ============================================================
void AVoxelWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bShutdown = true;

	// Cancel all in-flight async tasks before we destroy anything.
	for (auto& It : LoadedChunks)
	{
		if (AVoxelChunk* Chunk = It.Value)
		{
			if (Chunk->IsGenerating())
				Chunk->CancelGeneration();
		}
	}

	// Brief spin to let queued game-thread callbacks drain (max 3 s).
	const double Deadline = FPlatformTime::Seconds() + 3.0;
	while (ActiveGenerations > 0 && FPlatformTime::Seconds() < Deadline)
		FPlatformProcess::Sleep(0.01f);

	LoadedChunks.Empty();
	GenerationQueue.Empty();
	QueueHead = 0;
	ActiveGenerations = 0;

	Super::EndPlay(EndPlayReason);
}

// ============================================================
//  Tick
// ============================================================
void AVoxelWorld::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Guard against background generation while on Title Screen Menu
	bool bTitleScreenActive = false;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (class AFirstVoxelHUD* HUD = Cast<class AFirstVoxelHUD>(PC->GetHUD()))
		{
			bTitleScreenActive = HUD->bShowTitleScreen;
		}
	}
	if (bTitleScreenActive) return;

	if (GetWorld()->IsGameWorld())
		UpdateChunkStreaming();

	DrainGenerationQueue();

	// ── Initial Spawn Hover Lock ─────────────────────────────────────
	if (!bWaitingForInitialSpawn)
	{
		SpawnWaitAccum = 0.f;
	}
	else
	{
		SpawnWaitAccum += DeltaTime;
		// FIX: Increased timeout to 30 seconds to allow for slower chunk generation
		// This ensures the spawn area has enough time to generate completely
		const bool bTimedOut = (SpawnWaitAccum > 30.f);

		bool bAllReady = bTimedOut;
		if (!bTimedOut)
		{
			bAllReady = true;
			int32 ReadyCount = 0;
			int32 TotalCount = InitialSpawnCoords.Num();
			
			for (const FIntVector& C : InitialSpawnCoords)
			{
				AVoxelChunk** Ptr = LoadedChunks.Find(C);
				if (Ptr == nullptr)
				{
					UE_LOG(LogVoxelWorld, Verbose, TEXT("VoxelWorld: Chunk (%d,%d,%d) not found in LoadedChunks"), C.X, C.Y, C.Z);
					bAllReady = false;
					break;
				}
				if (!(*Ptr)->IsReady())
				{
					UE_LOG(LogVoxelWorld, Verbose, TEXT("VoxelWorld: Chunk (%d,%d,%d) not ready"), C.X, C.Y, C.Z);
					bAllReady = false;
					break;
				}
				ReadyCount++;
			}
			
			UE_LOG(LogVoxelWorld, Verbose, TEXT("VoxelWorld: Spawn progress %d/%d chunks ready (%.1f%%)"), 
			       ReadyCount, TotalCount, (float)ReadyCount / (float)TotalCount * 100.0f);
			
			// FIX: Update load bar progress during spawn area generation
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
			{
				if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
				{
					HUD->bShowLoadBar = true;
					HUD->bShowTitleScreen = false; // Hide title screen during spawn area generation
					HUD->LoadProgress = (float)ReadyCount / (float)TotalCount; // Update progress 0.0 to 1.0
				}
			}
		}

		APawn* SpawnPlayer = UGameplayStatics::GetPlayerPawn(this, 0);
		if (SpawnPlayer != nullptr)
		{
			if (!bAllReady || SpawnDelayAccum < SpawnHoldDelay)
			{
				if (bAllReady)
				{
					SpawnDelayAccum += DeltaTime;
				}
				// FIX: Enhanced hover-lock positioning with visual feedback
				// Move player to the calculated spawn height immediately, don't keep them at sky-hold
				FVector HoverPos = SpawnPlayer->GetActorLocation();
				// Always move player to the calculated spawn height, regardless of current position
				HoverPos.Z = TargetCoordsZ;
				SpawnPlayer->SetActorLocation(HoverPos, false, nullptr, ETeleportType::TeleportPhysics);
				
				// Keep movement frozen during hover-lock.
				// bJustTeleported=true tells the CMC to flush its floor cache so
				// the position override above is respected this tick.
				if (ACharacter* Character = Cast<ACharacter>(SpawnPlayer))
				{
					if (UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
					{
						CMC->SetMovementMode(EMovementMode::MOVE_None);
						CMC->bJustTeleported = true;
					}
				}
			}
			else
			{
				if (bTimedOut)
				{
					UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: Spawn hover-lock timed out after %.1f seconds. Proceeding with available chunks."), SpawnWaitAccum);
				}
				else
				{
					UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Spawn ready at Z=%.2f"), TargetCoordsZ);
				}
				
				// FIX: Properly release player from hover-lock and enable movement
				bWaitingForInitialSpawn = false;
				SpawnWaitAccum = 0.f;
				InitialSpawnCoords.Empty();
				
				// Mark load bar complete and hide it so the HUD returns to gameplay mode.
				if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
				{
					if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
					{
						HUD->LoadProgress  = 1.0f;
						HUD->bShowLoadBar  = false; // FIX: was never cleared — load bar stayed on screen permanently
					}
				}
				
				// Release player from hover-lock: place exactly on ground, enable collision,
				// then switch to MOVE_Falling so ProcessLanded fires within 1-2 frames.
				if (SpawnPlayer)
				{
					// Step 1: Re-enable collision first so the line trace hits terrain.
					SpawnPlayer->SetActorEnableCollision(true);

					// Step 2: Line-trace straight down from the hover position to find
					// the exact ground surface. This snaps the player to just above the
					// terrain so ProcessLanded fires within 1-2 physics frames instead
					// of after a long freefall from TargetCoordsZ (which is 3000cm up).
					const FVector TraceStart = SpawnPlayer->GetActorLocation();
					const FVector TraceEnd   = TraceStart + FVector(0.f, 0.f, -100000.f);
					FHitResult GroundHit;
					FCollisionQueryParams TraceParams;
					TraceParams.AddIgnoredActor(SpawnPlayer);
					const bool bFoundGround = GetWorld()->LineTraceSingleByChannel(
						GroundHit, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams);

					if (bFoundGround)
					{
						// Place player 120cm above the hit point (capsule half-height ~96cm + 24cm margin).
						// This ensures the capsule bottom just touches the floor surface so
						// the very first CMC sweep detects the floor and fires ProcessLanded.
						FVector LandPos = SpawnPlayer->GetActorLocation();
						LandPos.Z = GroundHit.ImpactPoint.Z + 120.f;
						SpawnPlayer->SetActorLocation(LandPos, false, nullptr, ETeleportType::TeleportPhysics);
					}

					// Step 3: Show the player now that we are at the right height.
					SpawnPlayer->SetActorHiddenInGame(false);

					// Step 4: Release movement via MOVE_Falling + bJustTeleported.
					// MOVE_Falling tells UE "check for a floor this tick".
					// bJustTeleported flushes the CMC's stale floor cache so it
					// probes immediately rather than on the next movement frame.
					// When the capsule hits the ground ProcessLanded fires automatically,
					// switching to MOVE_Walking and resetting the animation state.
					if (ACharacter* Character = Cast<ACharacter>(SpawnPlayer))
					{
						if (UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
						{
							CMC->SetMovementMode(EMovementMode::MOVE_Falling);
							CMC->bJustTeleported = true;
							// Zero out any accumulated velocity from the hover-lock period
							// so the character doesn't shoot sideways on release.
							CMC->Velocity = FVector::ZeroVector;
						}
					}
				}
				
				// Remainder of release logic completes normally or simply ends here
			}
		}
	}

	// Rebuild any chunks dirtied by player edits.
	// FIX: Must increment ActiveGenerations and wire OnGenerationComplete BEFORE
	// calling GenerateAsync(), otherwise the counter saturates after ~12 digs
	// and all future async generation (both tools AND streaming) silently stops.
	for (auto& It : LoadedChunks)
	{
		if (AVoxelChunk* Chunk = It.Value)
		{
			if (Chunk->bMeshDirty && !Chunk->IsGenerating())
			{
				Chunk->bMeshDirty = false;
				if (ActiveGenerations < MaxConcurrentGenerations)
				{
					ActiveGenerations++;
					TWeakObjectPtr<AVoxelWorld> WeakThis(this);
					Chunk->OnGenerationComplete = [WeakThis]()
					{
						if (AVoxelWorld* W = WeakThis.Get())
							W->ActiveGenerations = FMath::Max(0, W->ActiveGenerations - 1);
					};
					Chunk->GenerateAsync();
				}
				else
				{
					// Re-flag dirty so it retries next tick when a slot frees up
					Chunk->bMeshDirty = true;
				}
			}
		}
	}
}

// ============================================================
//  OnConstruction
// ============================================================
void AVoxelWorld::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!bInitialized && ChunkSize > 0)
	{
		DataMap.Init(ChunkSize);
		bInitialized = true;
	}
}


// ============================================================
//  ClearWorld
// ============================================================
void AVoxelWorld::ClearWorld()
{
	TArray<FIntVector> Keys;
	LoadedChunks.GetKeys(Keys);
	for (const FIntVector& Coord : Keys)
		DestroyChunk(Coord);

	LoadedChunks.Empty();
	GenerationQueue.Empty();
	QueueHead = 0;
	ActiveGenerations = 0;

	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: World cleared"));
}

// ============================================================
//  SnapPlayerToGround
// ============================================================
void AVoxelWorld::SnapPlayerToGround()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	AActor* TargetActor = Player;

	if (!TargetActor)
	{
		TArray<AActor*> PlayerStarts;
		UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), PlayerStarts);
		if (PlayerStarts.Num() > 0)
		{
			TargetActor = PlayerStarts[0];
			UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Snapping PlayerStart instead of Pawn in Editor."));
		}
	}

	if (!TargetActor)
	{
		UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: SnapPlayerToGround failed - No Player Pawn or PlayerStart found."));
		return;
	}

	FVector Pos = TargetActor->GetActorLocation();

	const FVoxelGenerationConfig& Config = GetEffectiveConfig();
	if (bForceCraterSpawn)
	{
		Pos = FindCraterSpawnLocation(Pos, Config);
	}

	// Find ground height at position
	float GroundZ = GetTerrainHeight(Pos.X, Pos.Y);
	Pos.Z = GroundZ + SafeSpawnHeightOffset;

	TargetActor->SetActorLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Snapped %s to Z=%.2f"), *TargetActor->GetName(), Pos.Z);
}

// ============================================================
//  Preset helpers (stub — wiring done via Editor buttons)
// ============================================================
void AVoxelWorld::SaveCurrentToPreset()
{
	if (BiomePreset)
		BiomePreset->Config = GetEffectiveConfig();
	else
		UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: No BiomePreset assigned"));
}

void AVoxelWorld::LoadFromPreset()
{
	if (!BiomePreset)
		UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: No BiomePreset assigned"));
}

// ============================================================
//  Public API stubs
// ============================================================
void AVoxelWorld::GenerateWorld()
{
	// 1. Pick a fresh seed so every click produces a new world.
	RandomizeSeed();

	// 2. Wipe all existing chunks and reset generation state.
	ClearWorld();

	// 3. Queue chunks + simulate the player spawn sequence in the editor
	//    viewport (crater search, spawn chunk prioritisation, etc.).
	GenerateWorldDeferred();

	// 4. Print the active seed to the screen so it's easy to note down
	//    or reproduce a world you like.
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1, 8.f, FColor::Cyan,
			FString::Printf(TEXT("[VoxelWorld] Generating with seed %d"), GenerationConfig.Seed));
	}
}

void AVoxelWorld::RandomizeSeed()
{
	// Only write to GenerationConfig.Seed. GetEffectiveConfig() always injects
	// this seed even when a BiomePreset is active, so we never dirty the asset.
	GenerationConfig.Seed = FMath::RandRange(1, TNumericLimits<int32>::Max());
	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: New seed = %d"), GenerationConfig.Seed);
}
void AVoxelWorld::ClearWorldModifications() { DataMap.Clear(); }
void AVoxelWorld::RebuildWorld()     { ClearWorld(); GenerateWorldDeferred(); }
void AVoxelWorld::RunTests()         { RunVoxelTests(); }

// ============================================================
//  Coordinate helpers
// ============================================================
FIntVector AVoxelWorld::WorldToChunkCoord(const FVector& WorldPos) const
{
	const FVector Anchor = GetActorLocation();
	return FIntVector(
		FMath::FloorToInt((WorldPos.X - Anchor.X) / (ChunkSize * VoxelSize)),
		FMath::FloorToInt((WorldPos.Y - Anchor.Y) / (ChunkSize * VoxelSize)),
		FMath::FloorToInt((WorldPos.Z - Anchor.Z) / (ChunkSize * VoxelSize)));
}

FVector AVoxelWorld::ChunkCoordToWorld(const FIntVector& Coord) const
{
	const FVector Anchor = GetActorLocation();
	return Anchor + FVector(
		Coord.X * ChunkSize * VoxelSize,
		Coord.Y * ChunkSize * VoxelSize,
		Coord.Z * ChunkSize * VoxelSize);
}

// ============================================================
//  Editor
// ============================================================
#if WITH_EDITOR
void AVoxelWorld::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Future: trigger selective rebuild when specific properties change.
}
#endif

float AVoxelWorld::GetGenerationProgress() const
{
	if (GenerationQueue.Num() == 0) return 1.0f;
	return (float)QueueHead / (float)GenerationQueue.Num();
}
