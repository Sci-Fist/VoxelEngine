// VoxelWorld.cpp
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
}

AVoxelWorld::~AVoxelWorld() {}

FVoxelGenerationConfig AVoxelWorld::GetEffectiveConfig() const
{
    if (BiomePreset != nullptr)
    {
        FVoxelGenerationConfig Out = BiomePreset->Config;
        Out.Seed = GenerationConfig.Seed;
        return Out;
    }
    return GenerationConfig;
}

void AVoxelWorld::BeginPlay()
{
    Super::BeginPlay();
    DensityGenerator = MakeUnique<FVoxelDensityGenerator>();
    if (WaterSystemComponent)
    {
        const FVoxelGenerationConfig Cfg = GetEffectiveConfig();
        if (WaterComponent)
        {
            WaterComponent->bEnableOcean = !Cfg.Water.bUseVoxelOcean && Cfg.Water.bEnableOcean;
            WaterComponent->SeaLevel     = Cfg.SeaLevel;
        }
        WaterSystemComponent->Initialize(MakeUnique<FVoxelWaterSimulator>(ChunkSize, VoxelSize), WaterComponent);
    }
    if (!bInitialized) { DataMap.Init(ChunkSize); bInitialized = true; }

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
            { HUD->bShowLoadBar = false; HUD->bShowTitleScreen = true; }
    }
}

void AVoxelWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bShutdown = true;
    for (auto& It : LoadedChunks)
        if (AVoxelChunk* Chunk = It.Value)
            if (Chunk->IsGenerating()) Chunk->CancelGeneration();
    LoadedChunks.Empty();
    if (StreamingComponent) StreamingComponent->ClearState();
    if (SpawnHandlerComponent) SpawnHandlerComponent->ClearState();
    {
        FScopeLock Lock(&GenerationQueueLock);
        GenerationQueue.Empty();
    }
    Super::EndPlay(EndPlayReason);
}

void AVoxelWorld::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    bool bTitleScreen = false;
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
            bTitleScreen = HUD->bShowTitleScreen;
    if (bTitleScreen) return;

    DrainGenerationQueue();

    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
            if (HUD->bShowLoadBar)
            {
                const int32 TotalQ = GenerationQueue.Num();
                if (!IsWaitingForInitialSpawn() && TotalQ == 0 && ActiveGenerations == 0)
                {
                    HUD->LoadProgress = 1.f;
                    HUD->bShowLoadBar = false;
                }
                else if (TotalQ > 0)
                {
                    HUD->bShowTitleScreen = false;
                    HUD->LoadProgress = 0.5f; 
                }
            }

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
        ActiveChunkGenerations.Add(Chunk);

        TWeakObjectPtr<AVoxelWorld> W(this);
        TWeakObjectPtr<AVoxelChunk> C(Chunk);
        Chunk->OnGenerationComplete = [W, C](){ 
            if (AVoxelWorld* S=W.Get()) {
                S->ActiveGenerations.FetchSub(1);
                if (C.IsValid()) S->ActiveChunkGenerations.Remove(C.Get());
            } 
        };
        Chunk->GenerateAsync();
    }
}

void AVoxelWorld::DrainGenerationQueue()
{
    if (bShutdown) return;
    if (ActiveGenerations >= MaxConcurrentGenerations) return;

    FScopeLock Lock(&GenerationQueueLock);
    while (GenerationQueue.Num() > 0)
    {
        // Peek at the highest priority to see if we should preempt
        FVoxelGenerationQueueEntry BestEntry = GenerationQueue[0]; // Heap root is always at 0

        if (ActiveGenerations >= MaxConcurrentGenerations)
        {
            // PREEMPTION LOGIC: If the best waiting task is very high priority (>8000) 
            // and we have active tasks with very low priority (<100), abort the low one.
            if (BestEntry.Priority > 8000.0f)
            {
                AVoxelChunk* Victim = nullptr;
                float LowestActivePriority = 1e10f;

                for (AVoxelChunk* ActiveChunk : ActiveChunkGenerations)
                {
                    if (!ActiveChunk) continue;
                    float P = (StreamingComponent) ? StreamingComponent->GetChunkPriority(ActiveChunk->GetChunkCoord()) : 0.f;
                    if (P < LowestActivePriority)
                    {
                        LowestActivePriority = P;
                        Victim = ActiveChunk;
                    }
                }

                if (Victim && LowestActivePriority < 200.0f)
                {
                    UE_LOG(LogVoxelWorld, Warning, TEXT("Preempting low-priority chunk (%d,%d,%d) P=%.1f for high-priority (%d,%d,%d) P=%.1f"),
                        Victim->GetChunkCoord().X, Victim->GetChunkCoord().Y, Victim->GetChunkCoord().Z, LowestActivePriority,
                        BestEntry.Coord.X, BestEntry.Coord.Y, BestEntry.Coord.Z, BestEntry.Priority);
                    
                    Victim->CancelGeneration(); // This will trigger ActiveGenerations.FetchSub(1) and removal from set
                    break; // Wait for slot to free up
                }
            }
            break; // Still full and no good preemption candidates
        }

        FVoxelGenerationQueueEntry Entry;
        GenerationQueue.HeapPop(Entry, true); 

        if (LoadedChunks.Contains(Entry.Coord) || EmptyChunks.Contains(Entry.Coord)) continue;

        SpawnChunk(Entry.Coord);
    }
}

void AVoxelWorld::ClearWorld()
{
    TArray<FIntVector> Keys; LoadedChunks.GetKeys(Keys);
    for (const FIntVector& C : Keys) DestroyChunk(C);
    LoadedChunks.Empty(); 
    {
        FScopeLock Lock(&GenerationQueueLock);
        GenerationQueue.Empty(); 
    }
    EmptyChunks.Empty();
    ActiveGenerations = 0;
    ChunksNeedingVisibilityCheck.Empty();
    if (StreamingComponent) StreamingComponent->ClearState();
    if (SpawnHandlerComponent) SpawnHandlerComponent->ClearState();
    GenerationConfig.Craters.ForcedCraterCenter = FVector2D(1000000.f, 1000000.f);
    bSpawnInNaturalCrater = false;
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: World cleared"));
}

void AVoxelWorld::SnapPlayerToGround()
{
    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    AActor* Target = Player;
    if (!Target) {
        TArray<AActor*> PS; UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), PS);
        if (PS.Num() > 0) Target = PS[0];
    }
    if (!Target) return;
    FVector Pos = Target->GetActorLocation();
    const FVoxelGenerationConfig Cfg = GetEffectiveConfig();
    if (bSpawnInNaturalCrater) {
        Pos = FindCraterSpawnLocation(Pos, Cfg);
        GenerationConfig.Craters.ForcedCraterCenter = FVector2D(Pos.X, Pos.Y);
    }
    Pos.Z = GetTerrainHeight(Pos.X, Pos.Y) + SafeSpawnHeightOffset;
    Target->SetActorLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);
}

void AVoxelWorld::GenerateWorld() { RandomizeSeed(); ClearWorld(); GenerateWorldDeferred(); }
void AVoxelWorld::RandomizeSeed() { GenerationConfig.Seed = FMath::RandRange(1, TNumericLimits<int32>::Max()); }
void AVoxelWorld::RebuildWorld() { ClearWorld(); GenerateWorldDeferred(); }
void AVoxelWorld::RunTests()     { RunVoxelTests(); }

FIntVector AVoxelWorld::WorldToChunkCoord(const FVector& WorldPos) const {
    const FVector Anchor = GetActorLocation();
    const float CW = ChunkSize * VoxelSize;
    return FIntVector(FMath::FloorToInt((WorldPos.X - Anchor.X) / CW), FMath::FloorToInt((WorldPos.Y - Anchor.Y) / CW), FMath::FloorToInt((WorldPos.Z - Anchor.Z) / CW));
}

FVector AVoxelWorld::ChunkCoordToWorld(const FIntVector& Coord) const {
    const FVector Anchor = GetActorLocation();
    const float CW = ChunkSize * VoxelSize;
    return Anchor + FVector(Coord.X * CW, Coord.Y * CW, Coord.Z * CW);
}

void AVoxelWorld::ClearEmptyChunksInRange(int32 MinZ, int32 MaxZ) {
    TArray<FIntVector> ToRemove;
    for (const FIntVector& C : EmptyChunks) if (C.Z >= MinZ && C.Z <= MaxZ) ToRemove.Add(C);
    for (const FIntVector& C : ToRemove) EmptyChunks.Remove(C);
}

float AVoxelWorld::GetGenerationProgress() const {
    if (IsWaitingForInitialSpawn() && SpawnHandlerComponent) {
        const int32 Total = SpawnHandlerComponent->GetTotalCollisionCount() + SpawnHandlerComponent->GetTotalVisualCount();
        if (Total <= 0) return 0.f;
        const int32 Ready = SpawnHandlerComponent->GetCollisionReadyCount() + SpawnHandlerComponent->GetVisualReadyCount();
        return (float)Ready / (float)Total;
    }
    if (GenerationQueue.Num() == 0) return 1.f;
    return 0.5f;
}

FString AVoxelWorld::GetGenerationStatusString() const {
    if (IsWaitingForInitialSpawn() && SpawnHandlerComponent) {
        const int32 Total = SpawnHandlerComponent->GetTotalCollisionCount() + SpawnHandlerComponent->GetTotalVisualCount();
        if (Total <= 0) return TEXT("Initializing Spawn Radius...");
        const int32 Ready = SpawnHandlerComponent->GetCollisionReadyCount() + SpawnHandlerComponent->GetVisualReadyCount();
        return FString::Printf(TEXT("Securing Spawn Area: %d / %d"), Ready, Total);
    }
    if (GenerationQueue.Num() == 0) return TEXT("Discovering Terrain structure...");
    return FString::Printf(TEXT("Building World (Priority): %d chunks left"), GenerationQueue.Num());
}

// Stub implementations for the rest to keep it compiling
void AVoxelWorld::OnConstruction(const FTransform& T) { Super::OnConstruction(T); if (!bInitialized && ChunkSize > 0) { DataMap.Init(ChunkSize); bInitialized = true; } }
void AVoxelWorld::SetVoxelSphere(FVector P, float R, float D, bool RB) {}
void AVoxelWorld::RunVoxelTests() {}
void AVoxelWorld::SaveToFile(const FString& S) {}
void AVoxelWorld::LoadFromFile(const FString& S) {}
void AVoxelWorld::ClearModifications() {}
void AVoxelWorld::SaveDefaultSlot() {}
void AVoxelWorld::LoadDefaultSlot() {}
void AVoxelWorld::TestSmoothLODTransitions() {}
float AVoxelWorld::GetTerrainHeight(float X, float Y) const { return 0.f; }
float AVoxelWorld::GetSurfaceZ(float X, float Y) const { return 0.f; }
void AVoxelWorld::MarkChunkDirty(const FIntVector& C) {}
void AVoxelWorld::UpdateChunkStreaming() {}
void AVoxelWorld::GenerateWorldDeferred()
{
    if (DeferTickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(DeferTickerHandle);
    DeferTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTSTicker::FDelegate::CreateLambda([this](float){
            PerformWorldDiscoveryAndBoundsCalculation();
            FinalizeGenerationSetup();
            return false;
        }), 0.1f);
}

void AVoxelWorld::DiscoverExistingChunks()
{
    // Implementation for discovering chunks in the level if needed
}
void AVoxelWorld::ConfigureChunk(AVoxelChunk* Chunk) const
{
    if (!Chunk) return;
    
    Chunk->SetActorLocation(ChunkCoordToWorld(Chunk->Coord));
    Chunk->MasterFlatMaterial  = MasterFlatMaterial;
    Chunk->MasterSlopeMaterial = MasterSlopeMaterial;
    Chunk->SlopeThreshold      = SlopeThreshold;
    Chunk->World               = const_cast<AVoxelWorld*>(this);
}
void AVoxelWorld::PerformWorldDiscoveryAndBoundsCalculation()
{
    // This is essentially just preparing the initial spawn area for now
    if (StreamingComponent)
    {
        StreamingComponent->InitialPass();
    }
}

void AVoxelWorld::FinalizeGenerationSetup()
{
    if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, TEXT("[VoxelWorld] Finalizing Setup..."));
}
void AVoxelWorld::CheckCloseRangeVisibility() {}
void AVoxelWorld::SpawnChunk(FIntVector Coord, bool bSyncCollision)
{
    if (bShutdown) return;
    
    TWeakObjectPtr<AVoxelWorld> W(this);
    
    // Check if pooled
    AVoxelChunk* Chunk = ChunkPool.AcquireChunk(this, ChunkSize, VoxelSize);
    if (!Chunk)
    {
        // Fallback to spawning (should not happen with 1200 chunk pool)
        FActorSpawnParameters Params;
        Params.Owner = this;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Chunk = GetWorld()->SpawnActor<AVoxelChunk>(AVoxelChunk::StaticClass(), GetActorLocation(), FRotator::ZeroRotator, Params);
    }
    
    if (Chunk)
    {
        Chunk->Coord = Coord;
        ConfigureChunk(Chunk);
        
        {
            FWriteScopeLock Lock(LoadedChunksLock);
            LoadedChunks.Add(Coord, Chunk);
        }

        ActiveGenerations.FetchAdd(1);
        Chunk->OnGenerationComplete = [W](){ if (AVoxelWorld* S=W.Get()) S->ActiveGenerations.FetchSub(1); };
        
        if (bSyncCollision) Chunk->GenerateSync();
        else Chunk->GenerateAsync();
    }
}
void AVoxelWorld::DestroyChunk(const FIntVector& Coord)
{
    FWriteScopeLock Lock(LoadedChunksLock);
    AVoxelChunk* Chunk = nullptr;
    if (LoadedChunks.RemoveAndCopyValue(Coord, Chunk))
    {
        if (Chunk)
        {
            Chunk->CancelGeneration();
            ChunkPool.ReleaseChunk(Chunk);
        }
    }
}

void AVoxelWorld::RebuildChunk(const FIntVector& Coord)
{
    DestroyChunk(Coord);
    SpawnChunk(Coord);
}
void AVoxelWorld::ProcessInitialPlayerSpawn() {}
FVector AVoxelWorld::FindCraterSpawnLocation(const FVector& S, const FVoxelGenerationConfig& C) const { return S; }
FVector AVoxelWorld::SnapToVoxelGrid(const FVector& W) const { return W; }
float AVoxelWorld::GetSafeSpawnHeightOffset() const { return SafeSpawnHeightOffset; }
#if WITH_EDITOR
void AVoxelWorld::PostEditChangeProperty(FPropertyChangedEvent& E) { Super::PostEditChangeProperty(E); }
#endif
