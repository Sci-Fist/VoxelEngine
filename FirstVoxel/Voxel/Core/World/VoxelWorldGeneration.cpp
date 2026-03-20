// VoxelWorld_Generation.cpp — updated for FIX #30 (GetEffectiveConfig by value)
// All `const FVoxelGenerationConfig& Config = GetEffectiveConfig()` replaced with
// `const FVoxelGenerationConfig Config = GetEffectiveConfig()` to avoid dangling
// references now that GetEffectiveConfig() returns by value.
// Also FIX #41: bForceCraterSpawn → bSpawnInNaturalCrater usage removed from here
// since the field is now named in VoxelWorld.h and read via GetEffectiveConfig().

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
//  GenerateWorldDeferred
// ============================================================
void AVoxelWorld::GenerateWorldDeferred()
{
    if (!GetWorld() || bShutdown) return;

    UVoxelLogger::LogVoxelEvent(TEXT("VoxelWorld: GenerateWorldDeferred started."));
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: GenerateWorldDeferred started at %s"), *GetActorLocation().ToString());

    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
        { HUD->bShowLoadBar = true; HUD->LoadProgress = 0.05f; }

    DiscoverExistingChunks();

    FVector CandidatePos = GetActorLocation();

    TArray<AActor*> PlayerStarts;
    UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), PlayerStarts);
    if (PlayerStarts.Num() > 0 && PlayerStarts[0])
    {
        CandidatePos = PlayerStarts[0]->GetActorLocation();
        UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Centering on PlayerStart %s"), *CandidatePos.ToString());
    }

    // FIX #30: return by value — store as value, not const reference
    const FVoxelGenerationConfig Config = GetEffectiveConfig();
    FVector CraterPos = FindCraterSpawnLocation(CandidatePos, Config);
    if (CraterPos != CandidatePos)
    {
        UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Natural crater at %s (from %s)"),
            *CraterPos.ToString(), *CandidatePos.ToString());
        CandidatePos = CraterPos;
    }

#if WITH_EDITOR
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Editor — using location %s"), *CandidatePos.ToString());
#else
    bool bFoundConflict = true; int32 MaxAttempts = 100;
    while (bFoundConflict && MaxAttempts-- > 0)
    {
        bFoundConflict = false;
        for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
        {
            if (!*It || *It == this) continue;
            if (FVector::Dist2D(CandidatePos, (*It)->GetActorLocation()) < JumpStep)
            {
                CandidatePos.X += JumpStep;
                if (FMath::Abs(CandidatePos.X) > 1000000.f) { CandidatePos.X = 0.f; CandidatePos.Y += JumpStep; }
                bFoundConflict = true; break;
            }
        }
    }
#endif

    if (GetActorLocation() != CandidatePos)
    {
        SetActorLocation(CandidatePos);
        UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Relocated to %s"), *CandidatePos.ToString());
    }
    SpawnTargetPos = CandidatePos;

    TWeakObjectPtr<AVoxelWorld> WeakThis(this);
    Async(EAsyncExecution::ThreadPool, [WeakThis]()
    {
        AVoxelWorld* Self = WeakThis.Get();
        if (!Self || Self->bShutdown) return;
        Self->PerformWorldDiscoveryAndBoundsCalculation();
        AsyncTask(ENamedThreads::GameThread, [WeakThis]()
        {
            AVoxelWorld* Self = WeakThis.Get();
            if (!Self || Self->bShutdown) return;
            Self->FinalizeGenerationSetup();
        });
    });
}

// ============================================================
//  PerformWorldDiscoveryAndBoundsCalculation
// ============================================================
void AVoxelWorld::PerformWorldDiscoveryAndBoundsCalculation()
{
    if (!GetWorld() || bShutdown) return;

    if (!bInitialized) { DataMap.Init(ChunkSize); bInitialized = true; }

    ActiveGenerations = 0;
    GenerationQueue.Empty();
    EmptyChunks.Empty();
    QueueHead = 0;

    const FVector Anchor = SpawnTargetPos;
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Centering on SpawnTargetPos %s"), *Anchor.ToString());

    const FIntVector Origin   = WorldToChunkCoord(Anchor);
    const FIntVector MinCoord = Origin - FIntVector(RenderDistanceXY, RenderDistanceXY, RenderDistanceZ);
    const FIntVector MaxCoord = Origin + FIntVector(RenderDistanceXY, RenderDistanceXY, RenderDistanceZ);

    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Chunks (%d,%d,%d) to (%d,%d,%d)"),
        MinCoord.X, MinCoord.Y, MinCoord.Z, MaxCoord.X, MaxCoord.Y, MaxCoord.Z);

    const FIntVector Center = MinCoord + (MaxCoord - MinCoord) / 2;

    TArray<TPair<int32, FIntVector>> Sorted;
    for (int32 z = MinCoord.Z; z <= MaxCoord.Z; ++z)
    for (int32 y = MinCoord.Y; y <= MaxCoord.Y; ++y)
    for (int32 x = MinCoord.X; x <= MaxCoord.X; ++x)
    {
        const FIntVector C(x, y, z);
        if (LoadedChunks.Contains(C)) continue;
        Sorted.Add({ FMath::Max3(FMath::Abs(x-Center.X), FMath::Abs(y-Center.Y), FMath::Abs(z-Center.Z)), C });
    }
    Sorted.Sort([](const TPair<int32,FIntVector>& A, const TPair<int32,FIntVector>& B){ return A.Key < B.Key; });

    TSet<FIntVector> QueueSet;
    for (auto& P : Sorted)
        if (!QueueSet.Contains(P.Value)) { QueueSet.Add(P.Value); GenerationQueue.Add(P.Value); }

#if WITH_EDITOR
    if (!GetWorld()->IsGameWorld())
    {
        // FIX #30: value copy
        const FVoxelGenerationConfig Cfg = GetEffectiveConfig();
        FVector Pos = SnapToVoxelGrid(FVector(Anchor.X, Anchor.Y, 0.f));
        const auto Wh       = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(Pos.X, Pos.Y, Cfg);
        const float Surface = Wh.SurfaceHeight;
        const float Safe    = GetSafeSpawnHeightOffset();
        float TargetZ       = Surface + Safe;

        const FSkylandsLayerConfig& SC = Cfg.SkylandsLayer;
        const float HN  = FMath::Clamp(Surface/SC.MaxTerrainReference,0.f,1.f);
        const float RN  = FMath::Clamp(Wh.Weights.GetRoughness()/SC.RoughnessReference,0.f,1.f);
        const float TS  = FMath::Clamp(HN*1.5f+RN*0.8f,0.f,1.f);
        const float SkyAlt = Surface + FMath::Lerp(SC.MinAltitudeAboveTerrain,SC.BaseAltitudeAboveTerrain,TS)
                           + HN*SC.HeightAltitudeBonus + RN*SC.RoughnessAltitudeBonus;
        const float IHT = (SC.BaseIslandSize+HN*SC.HeightSizeBonus+RN*SC.RoughnessSizeBonus)*SC.ThicknessRatio;

        static FVoxelDensityGenerator EdProbe;
        bool bSky = false;
        if (SkyAlt > Surface+5000.f)
            for (float z=SkyAlt+IHT; z>=FMath::Max(SkyAlt-IHT,Surface+500.f); z-=200.f)
                if (EdProbe.GetDensity(Pos.X,Pos.Y,z,Cfg)>0.f) { TargetZ=z+Safe; bSky=true; break; }

        const int32 SkyZ = FMath::FloorToInt(TargetZ/(ChunkSize*VoxelSize));
        const FIntVector SC2 = WorldToChunkCoord(Anchor);
        for (int32 x=-1;x<=1;x++) for (int32 y=-1;y<=1;y++)
        {
            if (bSky && SkyZ!=0)
            {
                FIntVector S(SC2.X+x,SC2.Y+y,SkyZ);
                if (!QueueSet.Contains(S)){ QueueSet.Add(S); GenerationQueue.Add(S); }
                if (SkyZ>0)
                {
                    FIntVector B(SC2.X+x,SC2.Y+y,SkyZ-1);
                    if (!QueueSet.Contains(B)){ QueueSet.Add(B); GenerationQueue.Add(B); }
                }
            }
        }
    }
#endif

    if (bWaitingForInitialSpawn && !InitialSpawnCoords.IsEmpty())
        for (const FIntVector& C : InitialSpawnCoords)
            if (!LoadedChunks.Contains(C) && !QueueSet.Contains(C))
            { QueueSet.Add(C); GenerationQueue.Insert(C, 0); }

    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Queued %d chunks."), GenerationQueue.Num());
    UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelWorld: Queued %d chunks."), GenerationQueue.Num()));
}

// ============================================================
//  FinalizeGenerationSetup
// ============================================================
void AVoxelWorld::FinalizeGenerationSetup()
{
    if (!GetWorld() || bShutdown) return;

    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
        { HUD->bShowLoadBar = true; HUD->LoadProgress = 0.1f; }

    if (!GetWorld()->IsGameWorld())
    {
#if WITH_EDITOR
        TWeakObjectPtr<AVoxelWorld> WeakThis(this);
        DrainTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda([WeakThis](float) -> bool
            {
                AVoxelWorld* Self = WeakThis.Get();
                if (!Self || Self->bShutdown) { if (Self) Self->DrainTickerHandle.Reset(); return false; }
                Self->DrainGenerationQueue();
                if (Self->QueueHead >= Self->GenerationQueue.Num())
                {
                    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Editor gen complete. %d chunks."), Self->LoadedChunks.Num());
                    Self->DrainTickerHandle.Reset();
                    return false;
                }
                return true;
            }), 0.1f);
#endif
    }

    if (GetWorld()->IsGameWorld())
    {
        APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
        if (Player)
        {
            FVector ParkPos = Player->GetActorLocation();
            ParkPos.Z = 100000.f;
            Player->SetActorLocation(ParkPos, false, nullptr, ETeleportType::TeleportPhysics);
        }
        ProcessInitialPlayerSpawn();
    }
}

// ============================================================
//  SpawnChunk
// ============================================================
void AVoxelWorld::SpawnChunk(const FIntVector& Coord, bool bSyncCollision)
{
    if (LoadedChunks.Contains(Coord)) return;

    AVoxelChunk* Chunk = ChunkPool.RetrieveOrCreateChunk(GetWorld(), ChunkCoordToWorld(Coord), this);
    if (!Chunk) return;

    Chunk->SetActorHiddenInGame(false);
    if (Chunk->GetProceduralMesh())
    {
        Chunk->GetProceduralMesh()->SetVisibility(false);
        Chunk->GetProceduralMesh()->bUseAsyncCooking = !bSyncCollision;
    }
    Chunk->SetOwner(this);
    Chunk->SetActorLabel(FString::Printf(TEXT("Chunk_%d_%d_%d"), Coord.X, Coord.Y, Coord.Z));
    Chunk->SetFolderPath(FName(*FString::Printf(TEXT("g_VoxelChunks/Z%d"), Coord.Z)));
    Chunk->ChunkCoord = Coord;
    Chunk->SetActorLocation(ChunkCoordToWorld(Coord));
    ConfigureChunk(Chunk);
    LoadedChunks.Add(Coord, Chunk);

    ActiveGenerations++;
    TWeakObjectPtr<AVoxelWorld> WeakThis(this);
    Chunk->OnGenerationComplete = [WeakThis](){ if (AVoxelWorld* S = WeakThis.Get()) S->ActiveGenerations--; };

    if (WaterSystemComponent) WaterSystemComponent->InitChunkWater(Chunk);
    Chunk->GenerateAsync();
}

// ============================================================
//  DestroyChunk
// ============================================================
void AVoxelWorld::DestroyChunk(const FIntVector& Coord)
{
    AVoxelChunk** P = LoadedChunks.Find(Coord);
    if (!P || !*P) return;
    AVoxelChunk* Chunk = *P;
    if (WaterSystemComponent)
    {
        if (WaterSystemComponent->GetSimulator()) WaterSystemComponent->GetSimulator()->UnregisterChunk(Coord);
        WaterSystemComponent->RemoveChunkFromWaterSimulation(Coord);
    }
    if (Chunk->IsGenerating()) Chunk->CancelGeneration();
    LoadedChunks.Remove(Coord);
    ChunkPool.ReturnChunk(Chunk);
}

// ============================================================
//  DrainGenerationQueue
// ============================================================
void AVoxelWorld::DrainGenerationQueue()
{
    if (!GetWorld()) return;
    const int32 Limit = !GetWorld()->IsGameWorld() ? 2 : (bWaitingForInitialSpawn ? 24 : 6);

    int32 N = 0;
    while (N < Limit && QueueHead < GenerationQueue.Num())
    {
        if (ActiveGenerations >= MaxConcurrentGenerations) break;
        SpawnChunk(GenerationQueue[QueueHead++]);
        N++;
    }
    if (QueueHead > 256) { GenerationQueue.RemoveAt(0, QueueHead); QueueHead = 0; }
}

void AVoxelWorld::RebuildChunk(const FIntVector& Coord)
{
    if (AVoxelChunk** P = LoadedChunks.Find(Coord))
        if (*P && !(*P)->IsGenerating()) (*P)->GenerateAsync();
}

void AVoxelWorld::DiscoverExistingChunks()
{
    if (!GetWorld()) return;
    int32 N = 0;
    for (TActorIterator<AVoxelChunk> It(GetWorld()); It; ++It)
        if (*It && (*It)->GetOwner()==this) { LoadedChunks.Add((*It)->ChunkCoord, *It); N++; }
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Discovered %d chunks"), N);
}

void AVoxelWorld::ConfigureChunk(AVoxelChunk* Chunk) const
{
    if (!Chunk) return;
    // FIX #30: value copy — no dangling reference
    const FVoxelGenerationConfig EffCfg = GetEffectiveConfig();

    Chunk->ChunkSize           = ChunkSize;
    Chunk->VoxelSize           = VoxelSize;
    Chunk->MasterFlatMaterial  = MasterFlatMaterial;
    Chunk->MasterSlopeMaterial = MasterSlopeMaterial;
    Chunk->SlopeThreshold      = SlopeThreshold;
    Chunk->GenerationConfig    = EffCfg;
    Chunk->TreeMesh            = TreeMesh;
    Chunk->GrassMesh           = GrassMesh;
    Chunk->FoliageDensity      = FoliageDensity;
    Chunk->MaxFoliageSlope     = MaxFoliageSlope;
    Chunk->DensityGenerator    = DensityGenerator.Get();
    Chunk->GenerationConfig.Craters.ForcedCraterCenter = FVector2D(SpawnTargetPos.X, SpawnTargetPos.Y);
    Chunk->GenerationConfig.SlopeThreshold = SlopeThreshold;
    Chunk->WaterMaterial = GenerationConfig.Water.OceanMaterial.Get();
    Chunk->DenseChunk = const_cast<AVoxelWorld*>(this)->ChunkManager.GetOrCreateChunk(Chunk->ChunkCoord, Chunk->ChunkSize);
}

// ============================================================
//  ProcessInitialPlayerSpawn
// ============================================================
void AVoxelWorld::ProcessInitialPlayerSpawn()
{
    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!Player) return;

    if (!SpawnTargetPos.IsZero())
        GenerationConfig.Craters.ForcedCraterCenter = FVector2D(SpawnTargetPos.X, SpawnTargetPos.Y);

    // FIX #30: value copy
    const FVoxelGenerationConfig Config = GetEffectiveConfig();

    FVector Pos = SpawnTargetPos;
    if (Pos.IsZero())
    {
        TArray<AActor*> PS;
        UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), PS);
        if (PS.Num() > 0 && PS[0]) Pos = PS[0]->GetActorLocation();
    }
    Pos.Z = 0.f;
    Pos = SnapToVoxelGrid(Pos);

    const auto Wh = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(Pos.X, Pos.Y, Config);
    const float Surface     = Wh.SurfaceHeight;
    const float SafeOffset  = GetSafeSpawnHeightOffset();
    float TargetZ           = Surface + SafeOffset;

    const float CraterW = Wh.Weights.GetWeight(EVoxelBiome::Craters);
    if (CraterW > 0.3f) TargetZ = Surface + SafeOffset;
    if (TargetZ > 100000.f || TargetZ < Surface - 1000.f) TargetZ = Surface + SafeOffset;

    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
    const float HN  = FMath::Clamp(Surface/SC.MaxTerrainReference,0.f,1.f);
    const float RN  = FMath::Clamp(Wh.Weights.GetRoughness()/SC.RoughnessReference,0.f,1.f);
    const float TS  = FMath::Clamp(HN*1.5f+RN*0.8f,0.f,1.f);
    const float SkyAlt = Surface + FMath::Lerp(SC.MinAltitudeAboveTerrain,SC.BaseAltitudeAboveTerrain,TS)
                       + HN*SC.HeightAltitudeBonus + RN*SC.RoughnessAltitudeBonus;
    const float IHT = (SC.BaseIslandSize+HN*SC.HeightSizeBonus+RN*SC.RoughnessSizeBonus)*SC.ThicknessRatio;

    static FVoxelDensityGenerator SpawnProbe;
    bool bSky = false;
    if (SkyAlt > Surface+5000.f && Surface < 50000.f)
        for (float z = SkyAlt+IHT; z >= FMath::Max(SkyAlt-IHT,Surface+500.f); z-=200.f)
            if (DensityGenerator && DensityGenerator->GetDensity(Pos.X,Pos.Y,z,Config)>0.f)
            { TargetZ=z+SafeOffset; bSky=true; break; }

    if (TargetZ > 100000.f) TargetZ = Surface + SafeOffset;
    if (CraterW > 0.3f) TargetZ = Surface + SafeOffset;

    UE_LOG(LogVoxelWorld, Warning,
        TEXT("VoxelWorld: Spawn Pos=(%.0f,%.0f) Surface=%.0f Z=%.0f CraterW=%.2f Sky=%d"),
        Pos.X, Pos.Y, Surface, TargetZ, CraterW, bSky ? 1 : 0);

    Pos.Z = TargetZ;
    TargetCoordsZ = TargetZ;
    CachedSurfaceHeight = Surface;
    Player->SetActorLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);

    if (bWaitingForInitialSpawn) return;
    InitialSpawnCoords.Empty();
    bWaitingForInitialSpawn = true;

    const FIntVector SpawnCoord = WorldToChunkCoord(FVector(Pos.X, Pos.Y, TargetZ));
    const FIntVector GroundCoord = WorldToChunkCoord(FVector(Pos.X, Pos.Y, Surface));
    const int32 SkyChunkZ = SpawnCoord.Z;
    const int32 MinZ = FMath::Min(SpawnCoord.Z, GroundCoord.Z) - 1;
    const int32 MaxZ = FMath::Max(SpawnCoord.Z, GroundCoord.Z) + 1;

    TArray<FIntVector> SpawnCoords;
    for (int32 x=-1;x<=1;x++) for (int32 y2=-1;y2<=1;y2++) for (int32 z=MinZ;z<=MaxZ;z++)
    {
        FIntVector C = SpawnCoord; C.X+=x; C.Y+=y2; C.Z=z;
        SpawnCoords.Add(C);
    }

    SpawnCoords.Sort([SpawnCoord,this](const FIntVector& A, const FIntVector& B)
    {
        const bool bBA=(A.X==SpawnCoord.X&&A.Y==SpawnCoord.Y&&A.Z<SpawnCoord.Z);
        const bool bBB=(B.X==SpawnCoord.X&&B.Y==SpawnCoord.Y&&B.Z<SpawnCoord.Z);
        if (bBA&&!bBB) return true; if (!bBA&&bBB) return false;
        // FIX #30: value copy in lambda
        const FVoxelGenerationConfig Cfg = GetEffectiveConfig();
        const FVector WA=ChunkCoordToWorld(A), WB=ChunkCoordToWorld(B);
        const bool bCA=FVoxelBiomeManager::GetBiomeWeightsStatic(WA.X,WA.Y,Cfg).GetWeight(EVoxelBiome::Craters)>0.1f;
        const bool bCB=FVoxelBiomeManager::GetBiomeWeightsStatic(WB.X,WB.Y,Cfg).GetWeight(EVoxelBiome::Craters)>0.1f;
        if (bCA&&!bCB) return true; if (!bCA&&bCB) return false;
        return (FMath::Abs(A.X-SpawnCoord.X)+FMath::Abs(A.Y-SpawnCoord.Y)+FMath::Abs(A.Z-SpawnCoord.Z))
             < (FMath::Abs(B.X-SpawnCoord.X)+FMath::Abs(B.Y-SpawnCoord.Y)+FMath::Abs(B.Z-SpawnCoord.Z));
    });

    for (const FIntVector& C : SpawnCoords)
    {
        InitialSpawnCoords.Add(C);
        if (!LoadedChunks.Contains(C)) SpawnChunk(C, true);
    }

    auto EnsureBelow = [&](FIntVector C)
    {
        if (!LoadedChunks.Contains(C)) SpawnChunk(C, true);
        InitialSpawnCoords.Add(C);
    };
    EnsureBelow(SpawnCoord + FIntVector(0,0,-1));
    EnsureBelow(SpawnCoord + FIntVector(0,0,-2));

    if (bSky && SkyChunkZ != SpawnCoord.Z)
        for (int32 x=-1;x<=1;x++) for (int32 y2=-1;y2<=1;y2++)
        {
            auto Add=[&](FIntVector C){ if(!LoadedChunks.Contains(C)){SpawnChunk(C);InitialSpawnCoords.Add(C);} };
            Add(FIntVector(SpawnCoord.X+x, SpawnCoord.Y+y2, SkyChunkZ));
            if (SkyChunkZ>0) Add(FIntVector(SpawnCoord.X+x, SpawnCoord.Y+y2, SkyChunkZ-1));
        }

    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Waiting for %d spawn chunks."), InitialSpawnCoords.Num());
}
