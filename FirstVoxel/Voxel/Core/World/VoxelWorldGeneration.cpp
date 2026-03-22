// VoxelWorld_Generation.cpp
// FIX #30 — GetEffectiveConfig() returns by value; all call sites store value copies.
// FIX N6  — DestroyChunk now calls ChunkManager.RemoveChunk(Coord) so the
//            DenseChunks TMap doesn't grow unbounded with session length.

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

    TWeakObjectPtr<AVoxelWorld> WeakThis(this);
    
    // ROOT FIX: Push candidate crater search (which samples many vertices) to Async ThreadPool
    // to avoid freezing the viewport before the load progress bar gets a chance to render.
    Async(EAsyncExecution::ThreadPool, [WeakThis, CandidatePos]()
    {
        AVoxelWorld* Self = WeakThis.Get();
        if (!Self || Self->bShutdown) return;

        FVoxelGenerationConfig Config = Self->GetEffectiveConfig();
        FVector CraterPos = Self->FindCraterSpawnLocation(CandidatePos, Config);

        // Relocation, configuration alignment, and conflict checks must remain on the GameThread
        AsyncTask(ENamedThreads::GameThread, [WeakThis, CraterPos, CandidatePos]()
        {
            AVoxelWorld* Self2 = WeakThis.Get();
            if (!Self2 || Self2->bShutdown) return;

            FVector FinalPos = CandidatePos;
            if (CraterPos != CandidatePos)
            {
                UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Natural crater at %s"), *CraterPos.ToString());
                FinalPos = CraterPos;
                Self2->GenerationConfig.Craters.ForcedCraterCenter = FVector2D(CraterPos.X, CraterPos.Y);
                if (Self2->BiomePreset != nullptr)
                {
                    Self2->BiomePreset->Config.Craters.ForcedCraterCenter = FVector2D(CraterPos.X, CraterPos.Y);
                }
            }

#if !WITH_EDITOR
            {
                bool bConflict = true; int32 MaxTry = 100;
                static constexpr float JumpStep = 200000.f;
                while (bConflict && MaxTry-- > 0)
                {
                    bConflict = false;
                    for (TActorIterator<AVoxelWorld> It(Self2->GetWorld()); It; ++It)
                    {
                        if (!*It || *It == Self2) continue;
                        if (FVector::Dist2D(FinalPos, (*It)->GetActorLocation()) < JumpStep)
                        {
                            FinalPos.X += JumpStep;
                            if (FMath::Abs(FinalPos.X) > 1000000.f) { FinalPos.X = 0.f; FinalPos.Y += JumpStep; }
                            bConflict = true; break;
                        }
                    }
                }
            }
#endif

            if (Self2->GetActorLocation() != FinalPos)
            {
                Self2->SetActorLocation(FinalPos);
                UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Relocated to %s"), *FinalPos.ToString());
            }
            Self2->SpawnTargetPos = FinalPos;

            // Continue column discovery on ThreadPool
            Async(EAsyncExecution::ThreadPool, [WeakThis]()
            {
                AVoxelWorld* Self3 = WeakThis.Get();
                if (!Self3 || Self3->bShutdown) return;

                Self3->PerformWorldDiscoveryAndBoundsCalculation();

                // Finalize on GameThread
                AsyncTask(ENamedThreads::GameThread, [WeakThis]()
                {
                    AVoxelWorld* Self4 = WeakThis.Get();
                    if (!Self4 || Self4->bShutdown) return;
                    Self4->FinalizeGenerationSetup();
                });
            });
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

    const FVector     Anchor   = SpawnTargetPos;
    const FIntVector  Origin   = WorldToChunkCoord(Anchor);
    const FIntVector  MinCoord = Origin - FIntVector(RenderDistanceXY, RenderDistanceXY, RenderDistanceZ);
    const FIntVector  MaxCoord = Origin + FIntVector(RenderDistanceXY, RenderDistanceXY, RenderDistanceZ);
    const FIntVector  Center   = MinCoord + (MaxCoord - MinCoord) / 2;

    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Chunks (%d,%d,%d)→(%d,%d,%d)"),
        MinCoord.X, MinCoord.Y, MinCoord.Z, MaxCoord.X, MaxCoord.Y, MaxCoord.Z);

    TArray<TPair<int32,FIntVector>> Sorted;
    const FVoxelGenerationConfig Cfg = GetEffectiveConfig();
    const float GridSize = ChunkSize * VoxelSize;

    for (int32 y=MinCoord.Y; y<=MaxCoord.Y; ++y)
    for (int32 x=MinCoord.X; x<=MaxCoord.X; ++x)
    {
        const float WX = (x + 0.5f) * GridSize;
        const float WY = (y + 0.5f) * GridSize;

        const auto Wh = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(WX, WY, Cfg);
        const float Surface = Wh.SurfaceHeight;
        const int32 GroundZ = FMath::FloorToInt(Surface / GridSize);

        const FSkylandsLayerConfig& SC = Cfg.SkylandsLayer;
        const float HN  = FMath::Clamp(Surface/SC.MaxTerrainReference,0.f,1.f);
        const float RN  = FMath::Clamp(Wh.Weights.GetRoughness()/SC.RoughnessReference,0.f,1.f);
        const float TS  = FMath::Clamp(HN*1.5f+RN*0.8f,0.f,1.f);
        
        const float SkyAlt = Surface + FMath::Lerp(SC.MinAltitudeAboveTerrain,SC.BaseAltitudeAboveTerrain,TS) + HN*SC.HeightAltitudeBonus + RN*SC.RoughnessAltitudeBonus;
        const float IHT    = (SC.BaseIslandSize+HN*SC.HeightSizeBonus+RN*SC.RoughnessSizeBonus)*SC.ThicknessRatio;

        const int32 SkyZ_Min = FMath::FloorToInt((SkyAlt - IHT - 1000.f) / GridSize);
        const int32 SkyZ_Max = FMath::FloorToInt((SkyAlt + IHT + 1000.f) / GridSize);

        for (int32 z=MinCoord.Z; z<=MaxCoord.Z; ++z)
        {
            const FIntVector C(x,y,z);
            if (LoadedChunks.Contains(C)) continue;

            bool bValid = false;
            // A) Zone A Ground: GroundZ - 2 to GroundZ + 12
            if (z >= GroundZ - 2 && z <= GroundZ + 12) bValid = true;
            // B) Skylands zone: SkyZ_Min to SkyZ_Max
            else if (z >= SkyZ_Min && z <= SkyZ_Max) bValid = true;

            if (bValid)
            {
                Sorted.Add({FMath::Max3(FMath::Abs(x-Center.X),FMath::Abs(y-Center.Y),FMath::Abs(z-Center.Z)),C});
            }
        }
    }
    Sorted.Sort([](const TPair<int32,FIntVector>& A, const TPair<int32,FIntVector>& B){ return A.Key<B.Key; });

    TSet<FIntVector> QSet;
    for (auto& P : Sorted) if (!QSet.Contains(P.Value)) { QSet.Add(P.Value); GenerationQueue.Add(P.Value); }

#if WITH_EDITOR
    if (!GetWorld()->IsGameWorld())
    {
        // Using outer scope Cfg
        FVector Pos = SnapToVoxelGrid(FVector(Anchor.X, Anchor.Y, 0.f));
        const auto Wh = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(Pos.X, Pos.Y, Cfg);
        const float Surface = Wh.SurfaceHeight;
        const FSkylandsLayerConfig& SC = Cfg.SkylandsLayer;
        const float HN  = FMath::Clamp(Surface/SC.MaxTerrainReference,0.f,1.f);
        const float RN  = FMath::Clamp(Wh.Weights.GetRoughness()/SC.RoughnessReference,0.f,1.f);
        const float TS  = FMath::Clamp(HN*1.5f+RN*0.8f,0.f,1.f);
        const float SkyAlt = Surface + FMath::Lerp(SC.MinAltitudeAboveTerrain,SC.BaseAltitudeAboveTerrain,TS) + HN*SC.HeightAltitudeBonus + RN*SC.RoughnessAltitudeBonus;
        const float IHT = (SC.BaseIslandSize+HN*SC.HeightSizeBonus+RN*SC.RoughnessSizeBonus)*SC.ThicknessRatio;
        static FVoxelDensityGenerator EdProbe;
        float TargetZ = Surface + GetSafeSpawnHeightOffset();
        bool bSky = false;
        if (SkyAlt > Surface+5000.f)
            for (float z2=SkyAlt+IHT; z2>=FMath::Max(SkyAlt-IHT,Surface+500.f); z2-=200.f)
                if (EdProbe.GetDensity(Pos.X,Pos.Y,z2,Cfg)>0.f) { TargetZ=z2+GetSafeSpawnHeightOffset(); bSky=true; break; }
        const FIntVector SC2 = WorldToChunkCoord(Anchor);
        const int32 SkyZ = FMath::FloorToInt(TargetZ/(ChunkSize*VoxelSize));
        for (int32 x=-1;x<=1;x++) for (int32 y=-1;y<=1;y++) if (bSky && SkyZ!=0)
        {
            FIntVector S(SC2.X+x,SC2.Y+y,SkyZ);  if (!QSet.Contains(S)){ QSet.Add(S); GenerationQueue.Add(S); }
            if (SkyZ>0) { FIntVector B(SC2.X+x,SC2.Y+y,SkyZ-1); if (!QSet.Contains(B)){ QSet.Add(B); GenerationQueue.Add(B); } }
        }
    }
#endif

    if (bWaitingForInitialSpawn)
    {
        TArray<FIntVector> TempQueue;
        TempQueue.Reserve(InitialSpawnCoords.Num() + InitialSpawnCoords_Visual.Num());
        
        for (const FIntVector& C : InitialSpawnCoords)
        {
            if (!LoadedChunks.Contains(C) && !QSet.Contains(C))
            {
                QSet.Add(C);
                TempQueue.Add(C);
            }
        }

        for (const FIntVector& C : InitialSpawnCoords_Visual)
        {
            if (!LoadedChunks.Contains(C) && !QSet.Contains(C))
            {
                QSet.Add(C);
                TempQueue.Add(C);
            }
        }

        if (TempQueue.Num() > 0)
        {
            GenerationQueue.Insert(TempQueue, 0); // O(N) single shift prepends efficiently!
        }
    }

    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Queued %d chunks."), GenerationQueue.Num());
}

// ============================================================
//  FinalizeGenerationSetup
// ============================================================
void AVoxelWorld::FinalizeGenerationSetup()
{
    if (!GetWorld() || bShutdown) return;
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this,0))
        if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
        { HUD->bShowLoadBar = true; HUD->LoadProgress = 0.1f; }

    if (!GetWorld()->IsGameWorld())
    {
#if WITH_EDITOR
        TWeakObjectPtr<AVoxelWorld> WeakThis(this);
        DrainTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda([WeakThis](float)->bool
            {
                AVoxelWorld* S = WeakThis.Get();
                if (!S || S->bShutdown) { if(S) S->DrainTickerHandle.Reset(); return false; }
                S->DrainGenerationQueue();
                if (S->QueueHead >= S->GenerationQueue.Num())
                { UE_LOG(LogVoxelWorld,Log,TEXT("VoxelWorld: Editor gen done. %d chunks."),S->LoadedChunks.Num()); S->DrainTickerHandle.Reset(); return false; }
                return true;
            }),0.1f);
#endif
    }
    else
    {
        if (APawn* Player = UGameplayStatics::GetPlayerPawn(this,0))
        {
            FVector ParkPos = Player->GetActorLocation(); ParkPos.Z = 100000.f;
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
#if WITH_EDITOR
    if (!GetWorld()->IsGameWorld()) // only label in editor viewport previews, NOT in PIE streaming
    {
        Chunk->SetActorLabel(FString::Printf(TEXT("Chunk_%d_%d_%d"), Coord.X, Coord.Y, Coord.Z));
        Chunk->SetFolderPath(FName(*FString::Printf(TEXT("g_VoxelChunks/Z%d"), Coord.Z)));
    }
#endif
    Chunk->ChunkCoord = Coord;
    Chunk->SetActorLocation(ChunkCoordToWorld(Coord));

    // FIX Distant Chunk Lag: Compute initial LOD BEFORE generating, instead of generating at LOD 0 and later downgrading.
    if (APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
    {
        const FVector PlayerPos = Player->GetActorLocation();
        const FVector ChunkPos = ChunkCoordToWorld(Coord) + FVector(ChunkSize * VoxelSize * 0.5f);
        const float DistSq = FVector::DistSquared(PlayerPos, ChunkPos);

        int32 TargetLOD = 0;
        if (DistSq > LOD2Distance * LOD2Distance) TargetLOD = 2;
        else if (DistSq > LOD1Distance * LOD1Distance) TargetLOD = 1;

        // Force maximum LOD 1 for high elevations so they retain 3D mesh overhangs instead of flat fits
        if (Coord.Z >= 4) TargetLOD = FMath::Min(TargetLOD, 1);

        if (DistSq < (ChunkSize * VoxelSize * 3.2f) * (ChunkSize * VoxelSize * 3.2f))
            TargetLOD = 0; // Safeguard

        Chunk->LOD = TargetLOD;
    }

    ConfigureChunk(Chunk);
    LoadedChunks.Add(Coord, Chunk);

    ActiveGenerations++;
    TWeakObjectPtr<AVoxelWorld> WeakThis(this);
    const FIntVector ChunkCoord = Coord; // capture by value for lambda
    Chunk->OnGenerationComplete = [WeakThis, ChunkCoord]()
    {
        if (AVoxelWorld* S = WeakThis.Get())
        {
            S->ActiveGenerations--;
            // FIX-1: Feed the pending-set so CheckCloseRangeVisibility only
            // iterates chunks that JUST became ready, not all loaded chunks.
            S->ChunksNeedingVisibilityCheck.Add(ChunkCoord);

            // PERF Fix #3: Increment readiness counters so Tick() can check
            // O(1) instead of scanning the entire InitialSpawnCoords array.
            if (S->bWaitingForInitialSpawn)
            {
                if (S->InitialSpawnCoords.Contains(ChunkCoord))
                    ++S->InitialSpawnCollisionReadyCount;
                else if (S->InitialSpawnCoords_Visual.Contains(ChunkCoord))
                    ++S->InitialSpawnVisualReadyCount;
            }
        }
    };
    if (WaterSystemComponent) WaterSystemComponent->InitChunkWater(Chunk);
    Chunk->GenerateAsync();
}

// ============================================================
//  DestroyChunk  — FIX N6: prunes ChunkManager
// ============================================================
void AVoxelWorld::DestroyChunk(const FIntVector& Coord)
{
    AVoxelChunk** P = LoadedChunks.Find(Coord);
    if (!P || !*P) return;
    AVoxelChunk* Chunk = *P;

    if (WaterSystemComponent)
    {
        if (WaterSystemComponent->GetSimulator())
            WaterSystemComponent->GetSimulator()->UnregisterChunk(Coord);
        WaterSystemComponent->RemoveChunkFromWaterSimulation(Coord);
    }
    if (Chunk->IsGenerating()) Chunk->CancelGeneration();

    LoadedChunks.Remove(Coord);
    // FIX N6: remove from DenseChunks so memory doesn't accumulate
    ChunkManager.RemoveChunk(Coord);
    ChunkPool.ReturnChunk(Chunk);
}

// ============================================================
//  DrainGenerationQueue
// ============================================================
void AVoxelWorld::DrainGenerationQueue()
{
    if (!GetWorld()) return;
    // PERF: Strict per-frame cap — SpawnChunk is expensive on the GameThread
    // (SpawnActor + configure + AsyncTask launch). 128/frame was consuming the
    // entire frame budget and causing 2.5 FPS during initial generation.
    // 8/frame keeps the GameThread fed without starving rendering.
    const bool bFastDrain = bWaitingForInitialSpawn;
    const int32 Limit = bFastDrain ? GenerationQueue.Num() : (!GetWorld()->IsGameWorld() ? 4 : 8);
    const int32 MaxConc = bFastDrain ? 2048 : MaxConcurrentGenerations;

    // PERF-4: compute once per drain cycle — ConfigureChunk reads by const-ref.
    CachedEffectiveConfig = GetEffectiveConfig();
    int32 N = 0;
    while (N < Limit && QueueHead < GenerationQueue.Num())
    {
        if ((int32)ActiveGenerations >= MaxConc) break;
        SpawnChunk(GenerationQueue[QueueHead++]);
        N++;
    }
    // PERF-5: Never RemoveAt(0,N) — that's an O(remaining) element shift.
    // Instead, only reset once the queue is fully consumed. The backing array
    // stays hot in cache during the fill phase with no shifting overhead.
    if (QueueHead >= GenerationQueue.Num())
    {
        GenerationQueue.Reset();
        QueueHead = 0;
    }
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
        if (*It && (*It)->GetOwner()==this) { LoadedChunks.Add((*It)->ChunkCoord,*It); N++; }
    UE_LOG(LogVoxelWorld,Log,TEXT("VoxelWorld: Discovered %d chunks"),N);
}

void AVoxelWorld::ConfigureChunk(AVoxelChunk* Chunk) const
{
    if (!Chunk) return;
    // PERF-4: DrainGenerationQueue computes GetEffectiveConfig() once per drain
    // cycle and passes it here so we don't deep-copy the large struct on every spawn.
    // CachedEffectiveConfig is set just before SpawnChunk is called.
    const FVoxelGenerationConfig& EffCfg = CachedEffectiveConfig;
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

    FVoxelGenerationConfig Config = GetEffectiveConfig(); // FIX #30
    if (!SpawnTargetPos.IsZero())
        Config.Craters.ForcedCraterCenter = FVector2D(SpawnTargetPos.X, SpawnTargetPos.Y);


    FVector Pos = SpawnTargetPos;
    if (Pos.IsZero())
    {
        TArray<AActor*> PS;
        UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), PS);
        if (PS.Num()>0&&PS[0]) Pos = PS[0]->GetActorLocation();
    }
    Pos.Z = 0.f; Pos = SnapToVoxelGrid(Pos);

    const auto Wh         = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(Pos.X, Pos.Y, Config);
    const float Surface   = Wh.SurfaceHeight;
    const float SafeOff   = GetSafeSpawnHeightOffset();
    float TargetZ         = Surface + SafeOff;
    const float CraterW   = Wh.Weights.GetWeight(EVoxelBiome::Craters);
    if (TargetZ > 100000.f || TargetZ < Surface - 1000.f) TargetZ = Surface + SafeOff;

    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
    const float HN  = FMath::Clamp(Surface/SC.MaxTerrainReference,0.f,1.f);
    const float RN  = FMath::Clamp(Wh.Weights.GetRoughness()/SC.RoughnessReference,0.f,1.f);
    const float TS  = FMath::Clamp(HN*1.5f+RN*0.8f,0.f,1.f);
    const float SkyAlt = FMath::Lerp(SC.MinAltitudeAboveTerrain,SC.BaseAltitudeAboveTerrain,TS) + HN*SC.HeightAltitudeBonus + RN*SC.RoughnessAltitudeBonus;
    const float IHT = (SC.BaseIslandSize+HN*SC.HeightSizeBonus+RN*SC.RoughnessSizeBonus)*SC.ThicknessRatio;

    bool bSky = false;
    if (SkyAlt > Surface+5000.f && Surface < 50000.f)
        for (float z2=SkyAlt+IHT; z2>=FMath::Max(SkyAlt-IHT,Surface+500.f); z2-=200.f)
            if (DensityGenerator && DensityGenerator->GetDensity(Pos.X,Pos.Y,z2,Config)>0.f)
            { TargetZ=z2+SafeOff; bSky=true; break; }

    if (TargetZ > 100000.f || CraterW > 0.3f) TargetZ = Surface + SafeOff;

    UE_LOG(LogVoxelWorld,Warning,TEXT("VoxelWorld: Spawn Pos=(%.0f,%.0f) Surface=%.0f Z=%.0f CraterW=%.2f Sky=%d CONFIG_DEPTH=%.0f CONFIG_RADIUS=%.0f"),
        Pos.X,Pos.Y,Surface,TargetZ,CraterW,bSky?1:0, Config.Craters.CentralCraterDepth, Config.Craters.CentralCraterRadius);

    Pos.Z = TargetZ; TargetCoordsZ = FMath::Max(TargetZ + 30000.f, 30000.f); CachedSurfaceHeight = Surface;

    Player->SetActorLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);

    if (bWaitingForInitialSpawn) return;
    InitialSpawnCoords.Empty();
    InitialSpawnCollisionReadyCount = 0; // PERF Fix #3: reset for new spawn cycle
    InitialSpawnVisualReadyCount    = 0;
    bWaitingForInitialSpawn = true;

    const FIntVector SpawnCoord  = WorldToChunkCoord(FVector(Pos.X,Pos.Y,TargetZ));
    const FIntVector GroundCoord = WorldToChunkCoord(FVector(Pos.X,Pos.Y,Surface));
    // Only lock the loading screen on the immediate spawn area + ground floor.
    // This prevents generating 150+ empty air chunks if spawning on a skyland.
    TSet<FIntVector> SpawnSet;
    TSet<FIntVector> VisualSet;
    
    auto AddToCollision = [&](const FIntVector& C) { SpawnSet.Add(C); };
    auto AddToVisual    = [&](const FIntVector& C) { VisualSet.Add(C); };

    // 1. Immediate Crater Zone depth volume sizing (COLLISION REQUIRED)
    // Inner radius uses shallow loads to prevent CPU overload; deep layers load async later.
    for (int32 x=-8; x<=8; x++) for (int32 y2=-8; y2<=8; y2++) for (int32 z2=-4; z2<=2; z2++)
    {
         AddToCollision(FIntVector(SpawnCoord.X + x, SpawnCoord.Y + y2, SpawnCoord.Z + z2));
    }
 
    // 2. Wide Visual Zone Radius sizing (VISUAL HORIZON)
    for (int32 x=-40; x<=40; x++) for (int32 y2=-40; y2<=40; y2++)
    {
         // Exclude inner collision core
         const bool bIsCenter = (FMath::Abs(x) <= 8 && FMath::Abs(y2) <= 8);
         if (bIsCenter) continue;
 
         for (int32 z2=-4; z2<=4; z2++)
         {
              AddToVisual(FIntVector(SpawnCoord.X + x, SpawnCoord.Y + y2, SpawnCoord.Z + z2));
         }
    }

    // 2. Add ground layer strictly beneath player if they spawn in the sky
    if (FMath::Abs(SpawnCoord.Z - GroundCoord.Z) > 1)
    {
        for (int32 x=-25; x<=25; x++) for (int32 y2=-25; y2<=25; y2++)
        {
            const bool bInner = (FMath::Abs(x) <= 10 && FMath::Abs(y2) <= 10);
            if (bInner)
            {
                AddToCollision(FIntVector(SpawnCoord.X + x, SpawnCoord.Y + y2, GroundCoord.Z));
                AddToCollision(FIntVector(SpawnCoord.X + x, SpawnCoord.Y + y2, GroundCoord.Z + 1));
                AddToCollision(FIntVector(SpawnCoord.X + x, SpawnCoord.Y + y2, GroundCoord.Z - 1));
            }
            else
            {
                AddToVisual(FIntVector(SpawnCoord.X + x, SpawnCoord.Y + y2, GroundCoord.Z));
            }
        }
    }

    TArray<FIntVector> SpawnCoords  = SpawnSet.Array();
    TArray<FIntVector> VisualCoords = VisualSet.Array();

    auto SortCoords = [SpawnCoord](const FIntVector& A, const FIntVector& B)
    {
        const bool bBA = (A.X==SpawnCoord.X && A.Y==SpawnCoord.Y && A.Z <= SpawnCoord.Z);
        const bool bBB = (B.X==SpawnCoord.X && B.Y==SpawnCoord.Y && B.Z <= SpawnCoord.Z);
        if (bBA && !bBB) return true; 
        if (!bBA && bBB) return false;
        return (FMath::Abs(A.X-SpawnCoord.X) + FMath::Abs(A.Y-SpawnCoord.Y) + FMath::Abs(A.Z-SpawnCoord.Z))
             < (FMath::Abs(B.X-SpawnCoord.X) + FMath::Abs(B.Y-SpawnCoord.Y) + FMath::Abs(B.Z-SpawnCoord.Z));
    };

    SpawnCoords.Sort(SortCoords);
    VisualCoords.Sort(SortCoords);

    InitialSpawnCoords_Visual.Empty();

    for (const FIntVector& C : SpawnCoords)
    { 
        InitialSpawnCoords.Add(C); 
        if (!LoadedChunks.Contains(C)) 
        {
            const bool bSync = (C.X == GroundCoord.X && C.Y == GroundCoord.Y && C.Z <= GroundCoord.Z && C.Z >= GroundCoord.Z - 2);
            if (bSync)
            {
                SpawnChunk(C, true); 
            }
        }
    }

    for (const FIntVector& C : VisualCoords)
    {
        InitialSpawnCoords_Visual.Add(C);
        // Defer spawning to normal tick flow to keep the game thread fully responsive
    }

    UE_LOG(LogVoxelWorld,Log,TEXT("VoxelWorld: Waiting for %d spawn chunks."),InitialSpawnCoords.Num());
}
