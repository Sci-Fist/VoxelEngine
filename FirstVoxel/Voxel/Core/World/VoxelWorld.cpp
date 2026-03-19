// VoxelWorld.cpp
// Core implementation of AVoxelWorld.
// Generation, streaming, modification, and water logic live in separate
// _Generation / _Streaming / _Modification .cpp files to keep compile units
// small.
//
// BUG FIXES IN THIS REVISION:
//   - DensityGenerator and WaterSimulator are now properly initialized in
//   BeginPlay()
//   - TickWater now correctly runs the sim step and rebuilds dirty water meshes
//   - InitChunkWater now wires OnChunkWaterReady so sources reach the simulator
//   - PostEditChangeProperty is correctly wrapped in WITH_EDITOR
//   - EndPlay properly cancels all chunks before clearing LoadedChunks

#include "VoxelWorld.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FirstVoxelHUD.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformProcess.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "Voxel/Biomes/VoxelBiomeManager.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "Voxel/Core/VoxelChunk.h"
#include "Voxel/Core/VoxelChunkPool.h"
#include "Voxel/Core/VoxelDataMap.h"
#include "Voxel/Core/World/Water/VoxelWorldWater.h"
#include "Voxel/Generation/VoxelDensityGenerator.h"
#include "Voxel/Generation/VoxelGeneratorTask.h"
#include "Voxel/VoxelLogger.h"
#include "Voxel/Water/VoxelWaterComponent.h"
#include "Voxel/Water/VoxelWaterSimulator.h"

DEFINE_LOG_CATEGORY(LogVoxelWorld);

// ============================================================
//  Constructor
// ============================================================
AVoxelWorld::AVoxelWorld() {
  PrimaryActorTick.bCanEverTick = true;
  PrimaryActorTick.TickGroup = TG_PrePhysics;

  Root = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
  RootComponent = Root;

  WaterComponent =
      CreateDefaultSubobject<UVoxelWaterComponent>(TEXT("WaterComponent"));

  WaterSystemComponent = CreateDefaultSubobject<UVoxelWorldWaterComponent>(
      TEXT("WaterSystemComponent"));

  // Default to false so the game shows the Title Screen overlay on startup
  bAutoGenerateOnBeginPlay = false;
}

AVoxelWorld::~AVoxelWorld() {
  // TUniquePtr members auto-destruct; nothing extra needed here.
}

// ============================================================
//  BeginPlay
//  Critical: initialize DensityGenerator + WaterSimulator here,
//  not in the constructor, so ChunkSize / VoxelSize are final.
// ============================================================
void AVoxelWorld::BeginPlay() {
  Super::BeginPlay();

  // Clean up any unpossessed placeholder characters placed in the Editor
  // (prevents double spawning alongside the GameMode dynamic player spawn).
  TArray<AActor *> FoundCharacters;
  UGameplayStatics::GetAllActorsOfClass(this, APawn::StaticClass(),
                                        FoundCharacters);
  for (AActor *Act : FoundCharacters) {
    APawn *P = Cast<APawn>(Act);
    // If it's a Pawn, not controlled by a player controller, and not the
    // current local viewer
    if (P && !P->IsPlayerControlled() && !P->IsPawnControlled()) {
      UE_LOG(LogVoxelWorld, Warning,
             TEXT("VoxelWorld: Destroying unpossessed editor-placed duplicate "
                  "actor %s to fix double spawn."),
             *Act->GetName());
      Act->Destroy();
    }
  }

  // ---- Density generator (all 3 layers: Surface / Skylands / Caves) ----
  DensityGenerator = MakeUnique<FVoxelDensityGenerator>();

  // ---- Water subsystem component ----
  if (WaterSystemComponent) {
    const FVoxelGenerationConfig &Config = GetEffectiveConfig();
    if (WaterComponent) {
      // Disable static mesh ocean if using voxel ocean for seamless integration
      WaterComponent->bEnableOcean =
          !Config.Water.bUseVoxelOcean && Config.Water.bEnableOcean;
      WaterComponent->SeaLevel = Config.SeaLevel;
    }

    WaterSystemComponent->Initialize(
        MakeUnique<FVoxelWaterSimulator>(ChunkSize, VoxelSize), WaterComponent);
  }

  // ---- Data map (tracks player edits) ----
  if (!bInitialized) {
    DataMap.Init(ChunkSize);
    bInitialized = true;
  }

  // ---- Optional random seed ----
  // Removed to ensure Editor previews match Gameplay 1:1. Use explicit Editor
  // Button to randomize seed.

  // FIX: Hide player actor and show title screen until world generation is
  // complete This ensures the player doesn't see the character floating in
  // empty space
  APawn *Player = UGameplayStatics::GetPlayerPawn(this, 0);
  if (Player) {
    // Hide player actor completely until generation is complete
    Player->SetActorHiddenInGame(true);
    Player->SetActorEnableCollision(false);

    // Freeze movement during generation. MOVE_None is used intentionally here:
    // the hover-lock in Tick will release via MOVE_Falling + bJustTeleported
    // once spawn chunks are ready, which lets UE's landing detection run
    // and correctly transition into MOVE_Walking + grounded anim state.
    if (ACharacter *Character = Cast<ACharacter>(Player)) {
      if (UCharacterMovementComponent *CMC =
              Character->GetCharacterMovement()) {
        CMC->SetMovementMode(EMovementMode::MOVE_None);
      }
    }
  }

  // FIX: Show load bar during generation
  // This ensures the player sees progress during world generation
  if (APlayerController *PC = UGameplayStatics::GetPlayerController(this, 0)) {
    if (AFirstVoxelHUD *HUD = Cast<AFirstVoxelHUD>(PC->GetHUD())) {
      HUD->bShowLoadBar = true;
      HUD->LoadProgress = 0.0f;
    }
  }

  if (bAutoGenerateOnBeginPlay) {
    if (bRandomizeSeedOnStartup) {
      RandomizeSeed();
    }
    ClearWorld();
    GenerateWorldDeferred();
  } else {
    DiscoverExistingChunks(); // Find editor-placed chunks
    ClearWorld();             // Wipe any prior Editor generated chunks
    // Notify HUD to show Title Screen
    if (APlayerController *PC =
            UGameplayStatics::GetPlayerController(this, 0)) {
      if (AFirstVoxelHUD *HUD = Cast<AFirstVoxelHUD>(PC->GetHUD())) {
        HUD->bShowTitleScreen = true;
      }
    }
  }
}

// ============================================================
//  EndPlay
// ============================================================
void AVoxelWorld::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  bShutdown = true;

  // Cancel all in-flight async tasks before we destroy anything.
  for (auto &It : LoadedChunks) {
    if (AVoxelChunk *Chunk = It.Value) {
      if (Chunk->IsGenerating())
        Chunk->CancelGeneration();
    }
  }

  // Brief spin to let queued game-thread callbacks drain (max 3 s).
  const double Deadline = FPlatformTime::Seconds() + 3.0;
  while (ActiveGenerations > 0 && FPlatformTime::Seconds() < Deadline) {
    FPlatformProcess::Sleep(0.01f);
  }

  LoadedChunks.Empty();
  GenerationQueue.Empty();
  EmptyChunks.Empty();
  QueueHead = 0;
  ActiveGenerations = 0;

  Super::EndPlay(EndPlayReason);
}

// ============================================================
//  Tick
// ============================================================
void AVoxelWorld::Tick(float DeltaTime) {
  Super::Tick(DeltaTime);

  // Guard against background generation while on Title Screen Menu
  bool bTitleScreenActive = false;
  if (APlayerController *PC = UGameplayStatics::GetPlayerController(this, 0)) {
    if (class AFirstVoxelHUD *HUD = Cast<class AFirstVoxelHUD>(PC->GetHUD())) {
      bTitleScreenActive = HUD->bShowTitleScreen;
    }
  }
  if (bTitleScreenActive)
    return;

  if (GetWorld()->IsGameWorld()) {
    StreamingTimer += DeltaTime;
    if (StreamingTimer >= StreamingInterval) {
      StreamingTimer = 0.f;
      UpdateChunkStreaming();
    }
  }

  DrainGenerationQueue();

  // ── Initial Spawn Hover Lock ────────────────────────────────────────────
  //
  // DESIGN: The player is held invisible at TargetCoordsZ with MOVE_None until
  // ALL spawn-area chunks have completed mesh upload AND physics collision body
  // cooking. Only then do we place the player on the ground and release.
  //
  // Waiting for collision (IsCollisionReady) rather than just mesh upload
  // (IsReady) is the key fix — async collision cooking can take 1-3 extra
  // frames after the mesh sections are uploaded, and releasing too early causes
  // the character to fall through the terrain with no physics body to land on.
  // ──────────────────────────────────────────────────────────────────────────
  if (!bWaitingForInitialSpawn) {
    SpawnWaitAccum = 0.f;
    SpawnDelayAccum = 0.f;
  } else {
    SpawnWaitAccum += DeltaTime;

    // Hard timeout: 90 s. Generous to handle slow machines and large meshes.
    const bool bTimedOut = (SpawnWaitAccum > 90.f);

    // Count how many spawn-area chunks have their COLLISION body ready
    // (not just mesh uploaded). Collision cooking is async and finishes
    // 1-3 frames after CreateMeshSection — IsCollisionReady() checks the
    // physics body instance is non-null and valid.
    int32 CollisionReadyCount = 0;
    int32 TotalCount = InitialSpawnCoords.Num();
    bool bAllCollisionReady = bTimedOut; // treat timeout as "ready enough"

    if (!bTimedOut) {
      bAllCollisionReady = true;
      for (const FIntVector &C : InitialSpawnCoords) {
        AVoxelChunk **Ptr = LoadedChunks.Find(C);
        if (!Ptr || !(*Ptr)->IsCollisionReady()) {
          bAllCollisionReady = false;
          break;
        }
        ++CollisionReadyCount;
      }
    } else {
      // Timeout: count however many are mesh-ready for the progress bar
      for (const FIntVector &C : InitialSpawnCoords) {
        AVoxelChunk **Ptr = LoadedChunks.Find(C);
        if (Ptr && (*Ptr)->IsReady())
          ++CollisionReadyCount;
      }
    }

    // Update the HUD load bar with collision-ready progress (0 → 1)
    if (APlayerController *PC =
            UGameplayStatics::GetPlayerController(this, 0)) {
      if (AFirstVoxelHUD *HUD = Cast<AFirstVoxelHUD>(PC->GetHUD())) {
        HUD->bShowLoadBar = true;
        HUD->bShowTitleScreen = false;
        HUD->LoadProgress =
            (TotalCount > 0)
                ? FMath::Min((float)CollisionReadyCount / (float)TotalCount,
                             0.99f)
                : 0.f;
        // Cap at 0.99 while waiting — the bar snaps to 1.0 on actual release
        // so the player can see it "complete" when the game starts.
      }
    }

    APawn *SpawnPlayer = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!SpawnPlayer)
      return;

    if (!bAllCollisionReady) {
      // ── HOVER LOCK: keep player frozen at TargetCoordsZ ──────────────
      FVector HoverPos = SpawnPlayer->GetActorLocation();
      HoverPos.Z = TargetCoordsZ;
      SpawnPlayer->SetActorLocation(HoverPos, false, nullptr,
                                    ETeleportType::TeleportPhysics);

      if (ACharacter *Ch = Cast<ACharacter>(SpawnPlayer)) {
        if (UCharacterMovementComponent *CMC = Ch->GetCharacterMovement()) {
          CMC->SetMovementMode(EMovementMode::MOVE_None);
          CMC->bJustTeleported = true;
        }
      }
    } else {
      // ── ALL COLLISION READY: release the player ────────────────────
      if (bTimedOut) {
        UE_LOG(LogVoxelWorld, Warning,
               TEXT("VoxelWorld: Spawn timed out (%.1fs). Releasing with %d/%d "
                    "collision-ready chunks."),
               SpawnWaitAccum, CollisionReadyCount, TotalCount);
      } else {
        UE_LOG(LogVoxelWorld, Log,
               TEXT("VoxelWorld: All %d spawn chunks have collision. Releasing "
                    "player."),
               TotalCount);
      }

      // ── Place player precisely on the ground ────────────────────────
      // Collision is confirmed live, so re-enable it and line-trace to find
      // the exact surface. We place the player's capsule just above the hit
      // point (capsule half-height 96cm + 5cm clearance = 101cm).
      SpawnPlayer->SetActorEnableCollision(true);

      const FVector TraceOrigin = SpawnPlayer->GetActorLocation();
      FHitResult GroundHit;
      FCollisionQueryParams QP;
      QP.AddIgnoredActor(SpawnPlayer);

      // FIX: Start closer to TraceOrigin (50cm above) to avoid penetrating
      // overhead ceilings or hillsides
      const FVector StartPos = TraceOrigin + FVector(0.f, 0.f, 50.f);
      const FVector EndPos = TraceOrigin + FVector(0.f, 0.f, -150000.f);

      // FIX: Sweep with a Sphere (radius 30 cm) to ensure the landing support
      // covers the entire capsule width instead of a single point that could
      // slip between mesh vertices.
      FCollisionShape SweepCap = FCollisionShape::MakeSphere(30.f);

      bool bHit = GetWorld()->SweepSingleByChannel(
          GroundHit, StartPos, EndPos, FQuat::Identity, ECC_Visibility,
          SweepCap, QP);

      if (!bHit) {
        // FALLBACK: If sphere sweep misses, try a simple line trace.
        bHit = GetWorld()->LineTraceSingleByChannel(
            GroundHit, StartPos, EndPos, ECC_Visibility, QP);
        if (bHit) {
          UE_LOG(LogVoxelWorld, Log,
                 TEXT("VoxelWorld: Sphere sweep missed, but Line trace hit at "
                      "Z=%.1f"),
                 GroundHit.ImpactPoint.Z);
        } else {
          // VERBOSE LOGGING: Diagnose why trace missed even though chunk is loaded
          UE_LOG(LogVoxelWorld, Warning, 
                 TEXT("VoxelWorld: ADVANCED TRACE MISS! StartPos=%s EndPos=%s"),
                 *StartPos.ToString(), *EndPos.ToString());

          const FIntVector SpawnCoord = WorldToChunkCoord(StartPos);
          if (AVoxelChunk** ChunkPtr = LoadedChunks.Find(SpawnCoord)) {
              if (AVoxelChunk* Chunk = *ChunkPtr) {
                  UE_LOG(LogVoxelWorld, Log, 
                         TEXT("VoxelWorld: StartPos is inside chunk %s which is FOUND in LoadedChunks. CollisionReady=%d"),
                         *SpawnCoord.ToString(), Chunk->IsCollisionReady() ? 1 : 0);
              }
          } else {
              UE_LOG(LogVoxelWorld, Warning, 
                     TEXT("VoxelWorld: StartPos occupies chunk %s which is MISSING from LoadedChunks!"),
                     *SpawnCoord.ToString());
          }
        }
      }

      if (bHit || bTimedOut) {
        if (bHit) {
          FVector LandPos = TraceOrigin;
          LandPos.Z =
              GroundHit.ImpactPoint.Z + 101.f; // 96cm capsule half + 5cm
          SpawnPlayer->SetActorLocation(LandPos, false, nullptr,
                                        ETeleportType::TeleportPhysics);
          UE_LOG(
              LogVoxelWorld, Log,
              TEXT("VoxelWorld: Placed player at ground Z=%.1f (hit Z=%.1f)"),
              LandPos.Z, GroundHit.ImpactPoint.Z);
        } else {
          UE_LOG(LogVoxelWorld, Warning,
                 TEXT("VoxelWorld: Ground trace missed on timeout — releasing "
                      "at hover height."));
        }

        // Dismantle hover-lock state
        bWaitingForInitialSpawn = false;
        SpawnWaitAccum = 0.f;
        SpawnDelayAccum = 0.f;
        InitialSpawnCoords.Empty();

        // Complete the load bar
        if (APlayerController *PC =
                UGameplayStatics::GetPlayerController(this, 0))
          if (AFirstVoxelHUD *HUD = Cast<AFirstVoxelHUD>(PC->GetHUD())) {
            HUD->LoadProgress = 1.0f;
            HUD->bShowLoadBar = false;
          }

        // Show the player
        SpawnPlayer->SetActorHiddenInGame(false);

        // ── Restore walking movement ────────────────────────────────────
        if (ACharacter *Ch = Cast<ACharacter>(SpawnPlayer)) {
          if (UCharacterMovementComponent *CMC = Ch->GetCharacterMovement()) {
            CMC->SetMovementMode(EMovementMode::MOVE_Walking);
          }
        }
      } else {
        UE_LOG(LogVoxelWorld, Warning,
               TEXT("VoxelWorld: Ground trace missed — staying at hover "
                    "height. Terrain collision may not be ready."));
        // bWaitingForInitialSpawn stays TRUE. Loop continues next frame!
      }
      //  • The player is already placed at ground level by the trace above.
      //  • MOVE_Walking + UpdateFloorFromAdjustment immediately snaps the
      //    capsule to the floor and triggers the correct grounded anim state.
      //  • MOVE_Falling relies on the physics simulation to detect landing,
      //    which can take several frames and leaves the character in the
      //    falling animation until ProcessLanded fires.
      if (ACharacter *Ch = Cast<ACharacter>(SpawnPlayer)) {
        if (UCharacterMovementComponent *CMC = Ch->GetCharacterMovement()) {
          CMC->Velocity = FVector::ZeroVector;
          CMC->SetMovementMode(EMovementMode::MOVE_Walking);
          // UpdateFloorFromAdjustment forces an immediate floor probe so the
          // CMC knows it's grounded right now, not on the next physics tick.
          CMC->UpdateFloorFromAdjustment();
          CMC->bJustTeleported = false; // clear flag so normal movement resumes
        }
      }
    }
  }

  // ── Close-range visibility health check ─────────────────────────────────
  // Proactively check and restore visibility for chunks near the player
  // This prevents chunks from getting stuck in invisible state
  CheckCloseRangeVisibility();

  // ── Dirty-chunk rebuild — O(DirtyQueue) not O(LoadedChunks) ──────────────
  // DirtyRebuildQueue is populated by MarkChunkDirty() (called from
  // SetVoxelSphere etc.). Scanning all LoadedChunks every frame was O(N) even
  // when nothing was dirty.
  TArray<FIntVector> ChunksToRebuild = DirtyRebuildQueue.Array();
  DirtyRebuildQueue.Empty();

  for (int32 i = 0; i < ChunksToRebuild.Num(); ++i) {
    const FIntVector Coord = ChunksToRebuild[i];
    if (ActiveGenerations >= MaxConcurrentGenerations) {
      // Re-queue remaining items to the Set for next tick
      for (int32 j = i; j < ChunksToRebuild.Num(); ++j) {
        DirtyRebuildQueue.Add(ChunksToRebuild[j]);
      }
      break;
    }

    AVoxelChunk **ChunkPtr = LoadedChunks.Find(Coord);
    if (!ChunkPtr || !(*ChunkPtr)) {
      continue;
    }
    AVoxelChunk *Chunk = *ChunkPtr;
    if (Chunk->IsGenerating()) {
      DirtyRebuildQueue.Add(Coord); // still generating, keep in queue
      continue;
    }

    Chunk->bMeshDirty = false;
    ActiveGenerations++;
    TWeakObjectPtr<AVoxelWorld> WeakThis(this);
    Chunk->OnGenerationComplete = [WeakThis]() {
      if (AVoxelWorld *W = WeakThis.Get())
        W->ActiveGenerations = FMath::Max(0, W->ActiveGenerations - 1);
    };
    Chunk->GenerateAsync();
  }

  // Legacy bMeshDirty fallback: any chunk dirtied by code that hasn't been
  // updated to call MarkChunkDirty() yet gets caught here at O(N) but
  // only if it actually has the flag set (short-circuit on false).
  for (auto &It : LoadedChunks) {
    if (AVoxelChunk *Chunk = It.Value) {
      if (Chunk->bMeshDirty && !Chunk->IsGenerating()) {
        // Migrate to proper queue going forward
        Chunk->bMeshDirty = false;
        DirtyRebuildQueue.Add(It.Key);
      }
    }
  }
}

// ============================================================
//  OnConstruction
// ============================================================
void AVoxelWorld::OnConstruction(const FTransform &Transform) {
  Super::OnConstruction(Transform);

  if (!bInitialized && ChunkSize > 0) {
    DataMap.Init(ChunkSize);
    bInitialized = true;
  }
}

// ============================================================
//  ClearWorld
// ============================================================
void AVoxelWorld::MarkChunkDirty(const FIntVector &Coord) {
  if (AVoxelChunk **Ptr = LoadedChunks.Find(Coord)) {
    if (*Ptr)
      (*Ptr)->bMeshDirty = true;
  }
  DirtyRebuildQueue.Add(Coord);
}

void AVoxelWorld::ClearWorld() {
  TArray<FIntVector> Keys;
  LoadedChunks.GetKeys(Keys);
  for (const FIntVector &Coord : Keys)
    DestroyChunk(Coord);

  LoadedChunks.Empty();
  GenerationQueue.Empty();
  EmptyChunks.Empty();
  DirtyRebuildQueue.Empty();
  ChunkManager.Clear();
  QueueHead = 0;
  ActiveGenerations = 0;

  UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: World cleared"));
}

// ============================================================
//  SnapPlayerToGround
// ============================================================
void AVoxelWorld::SnapPlayerToGround() {
  APawn *Player = UGameplayStatics::GetPlayerPawn(this, 0);
  AActor *TargetActor = Player;

  if (!TargetActor) {
    TArray<AActor *> PlayerStarts;
    UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(),
                                          PlayerStarts);
    if (PlayerStarts.Num() > 0) {
      TargetActor = PlayerStarts[0];
      UE_LOG(
          LogVoxelWorld, Log,
          TEXT("VoxelWorld: Snapping PlayerStart instead of Pawn in Editor."));
    }
  }

  if (!TargetActor) {
    UE_LOG(LogVoxelWorld, Warning,
           TEXT("VoxelWorld: SnapPlayerToGround failed - No Player Pawn or "
                "PlayerStart found."));
    return;
  }

  FVector Pos = TargetActor->GetActorLocation();

  const FVoxelGenerationConfig &Config = GetEffectiveConfig();
  if (bForceCraterSpawn) {
    Pos = FindCraterSpawnLocation(Pos, Config);
  }

  // Find ground height at position
  float GroundZ = GetTerrainHeight(Pos.X, Pos.Y);
  Pos.Z = GroundZ + SafeSpawnHeightOffset;

  TargetActor->SetActorLocation(Pos, false, nullptr,
                                ETeleportType::TeleportPhysics);
  UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Snapped %s to Z=%.2f"),
         *TargetActor->GetName(), Pos.Z);
}

// ============================================================
//  Preset helpers (stub — wiring done via Editor buttons)
// ============================================================
void AVoxelWorld::SaveCurrentToPreset() {
  if (BiomePreset)
    BiomePreset->Config = GetEffectiveConfig();
  else
    UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: No BiomePreset assigned"));
}

void AVoxelWorld::LoadFromPreset() {
  if (!BiomePreset)
    UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: No BiomePreset assigned"));
}

// ============================================================
//  Public API stubs
// ============================================================
void AVoxelWorld::GenerateWorld() {
  // 1. Pick a fresh seed so every click produces a new world.
  RandomizeSeed();

  // 2. Wipe all existing chunks and reset generation state.
  ClearWorld();

  // 3. Queue chunks + simulate the player spawn sequence in the editor
  //    viewport (crater search, spawn chunk prioritisation, etc.).
  GenerateWorldDeferred();

  // 4. Print the active seed to the screen so it's easy to note down
  //    or reproduce a world you like.
  if (GEngine) {
    GEngine->AddOnScreenDebugMessage(
        -1, 8.f, FColor::Cyan,
        FString::Printf(TEXT("[VoxelWorld] Generating with seed %d"),
                        GenerationConfig.Seed));
  }
}

void AVoxelWorld::RandomizeSeed() {
  // Only write to GenerationConfig.Seed. GetEffectiveConfig() always injects
  // this seed even when a BiomePreset is active, so we never dirty the asset.
  GenerationConfig.Seed = FMath::RandRange(1, TNumericLimits<int32>::Max());
  UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: New seed = %d"),
         GenerationConfig.Seed);
}
void AVoxelWorld::ClearWorldModifications() { DataMap.Clear(); }
void AVoxelWorld::RebuildWorld() {
  ClearWorld();
  GenerateWorldDeferred();
}
void AVoxelWorld::RunTests() { RunVoxelTests(); }

// ============================================================
//  Coordinate helpers
// ============================================================
FIntVector AVoxelWorld::WorldToChunkCoord(const FVector &WorldPos) const {
  const FVector Anchor = GetActorLocation();
  return FIntVector(
      FMath::FloorToInt((WorldPos.X - Anchor.X) / (ChunkSize * VoxelSize)),
      FMath::FloorToInt((WorldPos.Y - Anchor.Y) / (ChunkSize * VoxelSize)),
      FMath::FloorToInt((WorldPos.Z - Anchor.Z) / (ChunkSize * VoxelSize)));
}

FVector AVoxelWorld::ChunkCoordToWorld(const FIntVector &Coord) const {
  const FVector Anchor = GetActorLocation();
  return Anchor + FVector(Coord.X * ChunkSize * VoxelSize,
                          Coord.Y * ChunkSize * VoxelSize,
                          Coord.Z * ChunkSize * VoxelSize);
}

// ============================================================
//  Editor
// ============================================================
#if WITH_EDITOR
void AVoxelWorld::PostEditChangeProperty(
    FPropertyChangedEvent &PropertyChangedEvent) {
  Super::PostEditChangeProperty(PropertyChangedEvent);
  // Future: trigger selective rebuild when specific properties change.
}
#endif

float AVoxelWorld::GetGenerationProgress() const {
  if (GenerationQueue.Num() == 0)
    return 1.0f;
  return (float)QueueHead / (float)GenerationQueue.Num();
}
