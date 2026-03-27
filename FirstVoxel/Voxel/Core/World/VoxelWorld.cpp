// VoxelWorld.cpp
// FIX N4 — EndPlay deadlock removed. The old code did:
//   while (ActiveGenerations > 0) FPlatformProcess::Sleep(0.01f);
// This deadlocked because OnGenerationComplete lambdas are dispatched as
// AsyncTask(GameThread,...) and cannot fire while the game thread is sleeping.
// Fix: just set bShutdown and return; any in-flight tasks will call their
// completion lambdas (which WeakPtr-guard the decrement) harmlessly.
//
// FIX N13 — BeginPlay no longer destroys unpossessed pawns.
// The original code iterated all APawns and destroyed any not IsPlayerControlled.
// This would destroy AI characters, debug actors, any NPC placed in the level.
// The intent was to avoid duplicate player spawns; the correct approach is to
// let GameMode::HandleStartingNewPlayer handle possession as designed.

#include "VoxelWorld.h"
#include "Streaming/VoxelStreamingComponent.h"
#include "Spawn/VoxelSpawnHandlerComponent.h"
#include "FirstVoxelCharacter.h"

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
AVoxelWorld::AVoxelWorld()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup    = TG_PrePhysics;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
    RootComponent = Root;

    WaterComponent       = CreateDefaultSubobject<UVoxelWaterComponent>(TEXT("WaterComponent"));
    WaterSystemComponent = CreateDefaultSubobject<UVoxelWorldWaterComponent>(TEXT("WaterSystemComponent"));
    StreamingComponent    = CreateDefaultSubobject<UVoxelStreamingComponent>(TEXT("StreamingComponent"));
    SpawnHandlerComponent = CreateDefaultSubobject<UVoxelSpawnHandlerComponent>(TEXT("SpawnHandlerComponent"));

    bAutoGenerateOnBeginPlay = false;

}

AVoxelWorld::~AVoxelWorld() {}

// ============================================================
//  BeginPlay
// ============================================================
void AVoxelWorld::BeginPlay()
{
    Super::BeginPlay();

    // FIX N13: Removed aggressive unpossessed-pawn cleanup.
    // The original code destroyed any pawn not IsPlayerControlled(),
    // which would have killed AI NPCs, debug visualization actors, etc.
    // GameMode handles duplicate-spawn prevention through its possession flow.

    DensityGenerator = MakeUnique<FVoxelDensityGenerator>();

    if (WaterSystemComponent)
    {
        // FIX #30 (return-by-value): store config as a local value, not const&
        const FVoxelGenerationConfig Cfg = GetEffectiveConfig();
        if (WaterComponent)
        {
            WaterComponent->bEnableOcean = !Cfg.Water.bUseVoxelOcean && Cfg.Water.bEnableOcean;
            WaterComponent->SeaLevel     = Cfg.SeaLevel;
        }
        WaterSystemComponent->Initialize(
            MakeUnique<FVoxelWaterSimulator>(ChunkSize, VoxelSize), WaterComponent);
    }

    if (!bInitialized)
    {
        DataMap.Init(ChunkSize);
        bInitialized = true;
    }

    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (Player)
    {
        Player->SetActorHiddenInGame(true);
        Player->SetActorEnableCollision(false);
        if (ACharacter* Ch = Cast<ACharacter>(Player))
            if (UCharacterMovementComponent* CMC = Ch->GetCharacterMovement())
                CMC->SetMovementMode(EMovementMode::MOVE_None);
    }

    if (bAutoGenerateOnBeginPlay)
    {
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
            if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
            { HUD->bShowLoadBar = true; HUD->LoadProgress = 0.f; }

        if (bRandomizeSeedOnStartup) RandomizeSeed();
        ClearWorld();
        GenerateWorldDeferred();
    }
    else
    {
        DiscoverExistingChunks();
        ClearWorld();
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
            if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
            {
                HUD->bShowLoadBar = false;
                HUD->bShowTitleScreen = true;
            }
    }
}

// ============================================================
//  EndPlay — FIX N4: no spin-wait (was deadlocking the game thread)
// ============================================================
void AVoxelWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bShutdown = true;

    // Signal all in-flight tasks to cancel. Their OnGenerationComplete
    // lambdas are weak-ptr-guarded so they safely no-op after teardown.
    for (auto& It : LoadedChunks)
        if (AVoxelChunk* Chunk = It.Value)
            if (Chunk->IsGenerating()) Chunk->CancelGeneration();

    // FIX N4: Removed the blocking spin-wait loop.
    // The old code: while (ActiveGenerations > 0) FPlatformProcess::Sleep(0.01f);
    // This deadlocked because the OnGenerationComplete lambdas decrement
    // ActiveGenerations via AsyncTask(GameThread,...). Those callbacks can only
    // execute when the game thread is NOT sleeping. The wait was self-defeating.
    // Solution: just let the actors be cleaned up by UE's normal GC flow.
    // CancelGeneration() above sets bCancelled=true on each task so background
    // threads will exit their Execute() early without touching game objects.

    LoadedChunks.Empty();
    GenerationQueue.Empty();
    EmptyChunks.Empty();
    DirtyRebuildQueue.Empty();
    QueueHead         = 0;
    ActiveGenerations = 0;

    if (StreamingComponent) StreamingComponent->ClearState();
    if (SpawnHandlerComponent) SpawnHandlerComponent->ClearState();

    Super::EndPlay(EndPlayReason);
}

// ============================================================
//  Tick
// ============================================================
void AVoxelWorld::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    bool bTitleScreen = false;
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
            bTitleScreen = HUD->bShowTitleScreen;
    if (bTitleScreen) return;

    if (GetWorld()->IsGameWorld())
    {
        // Components tick themselves to handle Streaming and Initial Spawn updates.
    }

    DrainGenerationQueue();


    // ── Update Loading Screen HUD ────────────────────────────────────────
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
    {
        if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
        {
            if (HUD->bShowLoadBar)
            {
                const int32 TotalQ = GenerationQueue.Num();
                const bool bW = IsWaitingForInitialSpawn();
                if (!bW && TotalQ > 0 && QueueHead >= TotalQ && ActiveGenerations == 0)
                {
                    HUD->LoadProgress = 1.f;
                    HUD->bShowLoadBar = false;
                }
                else if (TotalQ > 0)
                {
                    HUD->bShowTitleScreen = false;
                    HUD->LoadProgress = FMath::Min((float)QueueHead / (float)TotalQ, 0.99f);
                }
            }
        }
    }

    // Handled by StreamingComponent

    // ── Dirty-chunk rebuild from DirtyRebuildQueue ───────────────────────
    // FIX-4: Early exit — if generation quota is already full, no point
    // iterating the queue just to discover every entry must wait.
    if (ActiveGenerations >= MaxConcurrentGenerations) return;

    for (int32 i = DirtyRebuildQueue.Num()-1; i >= 0; --i)
    {
        const FIntVector Coord = DirtyRebuildQueue[i];
        AVoxelChunk** PP = LoadedChunks.Find(Coord);
        if (!PP || !(*PP)) { DirtyRebuildQueue.RemoveAtSwap(i); continue; }
        AVoxelChunk* Chunk = *PP;
        if (Chunk->IsGenerating()) continue;
        DirtyRebuildQueue.RemoveAtSwap(i);
        if (ActiveGenerations >= MaxConcurrentGenerations) { DirtyRebuildQueue.Add(Coord); break; }
        Chunk->MarkMeshDirty(false);
        ActiveGenerations.FetchAdd(1);
        TWeakObjectPtr<AVoxelWorld> W(this);
        Chunk->OnGenerationComplete = [W](){ if (AVoxelWorld* S=W.Get()) S->ActiveGenerations.FetchSub(1); };
        Chunk->GenerateAsync();
    }

    // FIX-2: Removed the O(N) bMeshDirty fallback scan (lines 370-373 in original).
    // Any code setting bMeshDirty=true must call MarkChunkDirty() which already
    // adds to DirtyRebuildQueue — the scan was always redundant.
}

// ============================================================
//  OnConstruction
// ============================================================
void AVoxelWorld::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if (!bInitialized && ChunkSize > 0) { DataMap.Init(ChunkSize); bInitialized = true; }
}

// ============================================================
//  MarkChunkDirty / ClearWorld
// ============================================================
void AVoxelWorld::MarkChunkDirty(const FIntVector& Coord)
{
    if (AVoxelChunk** P = LoadedChunks.Find(Coord))
        if (*P) (*P)->MarkMeshDirty(true);

    // Directive 2: Implement Backpressure
    // AddUnique is O(N). If the queue becomes too large (e.g. during massive 
    // destructive operations), we cap it to prevent game-thread stalls.
    if (DirtyRebuildQueue.Num() < 512)
    {
        DirtyRebuildQueue.AddUnique(Coord);
    }
}

void AVoxelWorld::ClearWorld()
{
    TArray<FIntVector> Keys; LoadedChunks.GetKeys(Keys);
    for (const FIntVector& C : Keys) DestroyChunk(C);
    LoadedChunks.Empty(); GenerationQueue.Empty(); EmptyChunks.Empty();
    DirtyRebuildQueue.Empty(); ChunkManager.Clear(); QueueHead = 0;
    ActiveGenerations = 0;
    ChunksNeedingVisibilityCheck.Empty(); // FIX-1: clear pending-set on world reset
    
    if (StreamingComponent) StreamingComponent->ClearState();
    if (SpawnHandlerComponent) SpawnHandlerComponent->ClearState();

    // Reset crater logic
    GenerationConfig.Craters.ForcedCraterCenter = FVector2D(1000000.f, 1000000.f); // Use far-out value to disable
    bSpawnInNaturalCrater = false; // Disable by default on clear
    
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: World cleared"));
}

// ============================================================
//  SnapPlayerToGround
// ============================================================
void AVoxelWorld::SnapPlayerToGround()
{
    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    AActor* Target = Player;
    if (!Target)
    {
        TArray<AActor*> PS;
        UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), PS);
        if (PS.Num() > 0) Target = PS[0];
    }
    if (!Target) return;

    FVector Pos = Target->GetActorLocation();
    const FVoxelGenerationConfig Cfg = GetEffectiveConfig(); // FIX #30: value copy
    if (bSpawnInNaturalCrater)
    {
        Pos = FindCraterSpawnLocation(Pos, Cfg);
        if (BiomePreset != nullptr)
        {
            BiomePreset->Config.Craters.ForcedCraterCenter = FVector2D(Pos.X, Pos.Y);
        }
        GenerationConfig.Craters.ForcedCraterCenter = FVector2D(Pos.X, Pos.Y);
    }

    Pos.Z = GetTerrainHeight(Pos.X, Pos.Y) + SafeSpawnHeightOffset;
    Target->SetActorLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Snapped %s to Z=%.2f"), *Target->GetName(), Pos.Z);
}

// ============================================================
//  Preset helpers
// ============================================================
void AVoxelWorld::SaveCurrentToPreset()
{
    if (BiomePreset) BiomePreset->Config = GetEffectiveConfig();
    else UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: No BiomePreset assigned"));
}
void AVoxelWorld::LoadFromPreset()
{
    if (!BiomePreset) UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: No BiomePreset assigned"));
}

// ============================================================
//  Public API
// ============================================================
void AVoxelWorld::GenerateWorld()
{
    RandomizeSeed(); ClearWorld(); GenerateWorldDeferred();
    if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Cyan,
        FString::Printf(TEXT("[VoxelWorld] Generating with seed %d"), GenerationConfig.Seed));
}

void AVoxelWorld::RandomizeSeed()
{
    GenerationConfig.Seed = FMath::RandRange(1, TNumericLimits<int32>::Max());
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: New seed = %d"), GenerationConfig.Seed);
}

void AVoxelWorld::RebuildWorld() { ClearWorld(); GenerateWorldDeferred(); }
void AVoxelWorld::RunTests()     { RunVoxelTests(); }

// ============================================================
//  Coordinate helpers
// ============================================================
FIntVector AVoxelWorld::WorldToChunkCoord(const FVector& WorldPos) const
{
    const FVector Anchor = GetActorLocation();
    const float CW = ChunkSize * VoxelSize;
    return FIntVector(
        FMath::FloorToInt((WorldPos.X - Anchor.X) / CW),
        FMath::FloorToInt((WorldPos.Y - Anchor.Y) / CW),
        FMath::FloorToInt((WorldPos.Z - Anchor.Z) / CW));
}

FVector AVoxelWorld::ChunkCoordToWorld(const FIntVector& Coord) const
{
    const FVector Anchor = GetActorLocation();
    const float CW = ChunkSize * VoxelSize;
    return Anchor + FVector(Coord.X * CW, Coord.Y * CW, Coord.Z * CW);
}

// ============================================================
//  Editor
// ============================================================
#if WITH_EDITOR
void AVoxelWorld::PostEditChangeProperty(FPropertyChangedEvent& Ev)
{
    Super::PostEditChangeProperty(Ev);

    const FName MemberName = (Ev.MemberProperty != nullptr) ? Ev.MemberProperty->GetFName() : NAME_None;
    if (MemberName == GET_MEMBER_NAME_CHECKED(AVoxelWorld, ChunkSize))
    {
        // Force fully clean fully re-initialise on next generation pass
        bInitialized = false;
        ClearWorld(); // Safe cleanup of old mesh sections/actors
    }
}
#endif

bool AVoxelWorld::IsWaitingForInitialSpawn() const
{
    return SpawnHandlerComponent != nullptr ? SpawnHandlerComponent->IsWaitingForInitialSpawn() : bWaitingForInitialSpawn;
}

float AVoxelWorld::GetGenerationProgress() const
{
    if (IsWaitingForInitialSpawn() && SpawnHandlerComponent)
    {
        const int32 Total = SpawnHandlerComponent->GetTotalCollisionCount() + SpawnHandlerComponent->GetTotalVisualCount();
        if (Total <= 0) return 0.f;
        const int32 Ready = SpawnHandlerComponent->GetCollisionReadyCount() + SpawnHandlerComponent->GetVisualReadyCount();
        return (float)Ready / (float)Total;
    }
    if (GenerationQueue.Num() == 0) return 1.f;
    return (float)QueueHead / (float)GenerationQueue.Num();
}

FString AVoxelWorld::GetGenerationStatusString() const
{
    if (IsWaitingForInitialSpawn() && SpawnHandlerComponent)
    {
        const int32 Total = SpawnHandlerComponent->GetTotalCollisionCount() + SpawnHandlerComponent->GetTotalVisualCount();
        if (Total <= 0) return TEXT("Initializing Spawn Radius...");
        const int32 Ready = SpawnHandlerComponent->GetCollisionReadyCount() + SpawnHandlerComponent->GetVisualReadyCount();
        return FString::Printf(TEXT("Securing Spawn Area: %d / %d"), Ready, Total);
    }
    
    if (GenerationQueue.Num() == 0) return TEXT("Discovering Terrain structure...");
    
    const int32 H = QueueHead;
    const int32 T = GenerationQueue.Num();
    if (H >= T) return TEXT("Finalizing geometry mesh descriptions...");
    
    return FString::Printf(TEXT("Building World: %d / %d"), H, T);
}
