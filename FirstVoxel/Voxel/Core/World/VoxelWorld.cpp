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
    
    // Spinlock the Game Thread during exit to prevent DLL unload while AsyncTasks are still active.
    // Limits wait to 2 seconds to avoid softlocking the editor exit process.
    const double WaitStart = FPlatformTime::Seconds();
    while (ActiveGenerations > 0 && (FPlatformTime::Seconds() - WaitStart) < 2.0)
    {
        FPlatformProcess::Sleep(0.01f);
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
        ActiveGenerations += 1;
        ActiveChunkGenerations.Add(Chunk);

        TWeakObjectPtr<AVoxelWorld> W(this);
        TWeakObjectPtr<AVoxelChunk> C(Chunk);
        Chunk->OnGenerationComplete = [W, C](){ 
            if (AVoxelWorld* S=W.Get()) {
                S->ActiveGenerations -= 1;
                if (C.IsValid()) S->ActiveChunkGenerations.Remove(C.Get());
            } 
        };
        Chunk->GenerateAsync();
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

void AVoxelWorld::OnConstruction(const FTransform& T) { Super::OnConstruction(T); if (!bInitialized && ChunkSize > 0) { DataMap.Init(ChunkSize); bInitialized = true; } }
void AVoxelWorld::MarkChunkDirty(const FIntVector& C) {}

void AVoxelWorld::OnInitialSpawnComplete()
{
    bWaitingForInitialSpawn = false;
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Initial spawn complete."));
}

bool AVoxelWorld::IsWaitingForInitialSpawn() const { return bWaitingForInitialSpawn; }
void AVoxelWorld::SaveCurrentToPreset() {}
void AVoxelWorld::LoadFromPreset() {}

#if WITH_EDITOR
void AVoxelWorld::PostEditChangeProperty(FPropertyChangedEvent& E) { Super::PostEditChangeProperty(E); }
#endif
