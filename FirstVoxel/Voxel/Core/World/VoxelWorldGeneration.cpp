// VoxelWorld_Generation.cpp
// FIX #30 — GetEffectiveConfig() returns by value; all call sites store value copies.
// FIX N6  — DestroyChunk now calls ChunkManager.RemoveChunk(Coord) so the
//            DenseChunks TMap doesn't grow unbounded with session length.
// FIX RIM-1 — MaxRadius extended from MidRenderDistanceXY to DistantRenderDistanceXY
//             so Zone C (distant horizon / crater rim) is actually queued for generation.
// FIX RIM-2 — Zone C Z-selector added in both AVX2 and scalar discovery loops;
//             previously fell through with EffMinZ=1/EffMaxZ=1 (1-chunk window)
//             which silently skipped the rim surface.
// FIX RIM-3 — LOD 2 heightmap path disabled in VoxelChunk.cpp; distant chunks now
//             use Surface Nets at StepSize=4 so vertical walls (rim, cliffs) render.

#include "VoxelWorld.h"
#include "Async/ParallelFor.h"
#include "Voxel/Generation/VoxelNoiseSIMD.h"
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
#include "Voxel/Core/World/Spawn/VoxelSpawnHandlerComponent.h"
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
    if (bGenerationActive.Load()) return; // Prevent overlapping Async chains
    bGenerationActive.Store(true);

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

        // ── RELOCATION LOGIC ────────────────────────────────────────────────
        // v18.1: Only relocate if the actor is at (0,0,0). This allows the user 
        // to manually position the AVoxelWorld while still having a natural 
        // crater spawn by default.
        const bool bIsAtOrigin = CandidatePos.IsNearlyZero(10.f);
        FVector FinalPos = CandidatePos;
        if (bIsAtOrigin && CraterPos != CandidatePos)
        {
            UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Natural crater at %s"), *CraterPos.ToString());
            FinalPos = CraterPos;
            Self2->GenerationConfig.Craters.ForcedCraterCenter = FVector2D(CraterPos.X, CraterPos.Y);
            if (Self2->BiomePreset != nullptr)
            {
                Self2->BiomePreset->Config.Craters.ForcedCraterCenter = FVector2D(CraterPos.X, CraterPos.Y);
            }
        }
        else if (!bIsAtOrigin)
        {
            // If moved manually, force the local center to the current actor pos
            Self2->GenerationConfig.Craters.ForcedCraterCenter = FVector2D(CandidatePos.X, CandidatePos.Y);
            UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Manual position detected, forcer crater set to %s"), *CandidatePos.ToString());
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

    // FIX Threading Crash: Array mutations deferred until discovery completes.
    // ActiveGenerations stays untouched to prevent active tasks from being lost.

    const FVector     Anchor   = SpawnTargetPos;
    const FIntVector  Origin   = WorldToChunkCoord(Anchor);
    // FIX RIM-1: lowered to 64 to prevent 6M chunk explosions while still providing 1km radius
    // HARD-LIMIT: only scan 512m vertically (±16 chunks). This is the permanent fix 
    // for the 2.1-million-chunk explosion seen in V13.0-V13.3.
    // FIX CHUNK-EXPLOSION: DistantRenderDistanceXY values > 128 create 1M+ chunk
    // queues that produce extreme load times (the 1.8M chunk queue in the screenshot).
    // Cap at 128 and warn so the designer can fix the level actor value.
    static constexpr int32 MaxSafeDiscoveryRadius = 128;
    const int32 MaxRadius = FMath::Min(DistantRenderDistanceXY, MaxSafeDiscoveryRadius);
    if (DistantRenderDistanceXY > MaxSafeDiscoveryRadius)
    {
        UE_LOG(LogVoxelWorld, Error,
            TEXT("VoxelWorld: DistantRenderDistanceXY=%d exceeds safe cap %d — clamped to %d. "
                 "Reduce DistantRenderDistanceXY on the VoxelWorld Details panel to fix loading time."),
            DistantRenderDistanceXY, MaxSafeDiscoveryRadius, MaxSafeDiscoveryRadius);
    }
    const int32 VerticalDiscoveryRange = 16; 
    const FIntVector  MinCoord = Origin - FIntVector(MaxRadius, MaxRadius, VerticalDiscoveryRange);
    const FIntVector  MaxCoord = Origin + FIntVector(MaxRadius, MaxRadius, VerticalDiscoveryRange);
    const FIntVector  Center   = Origin; 

    UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: Discovery Area (%d,%d,%d)→(%d,%d,%d) MaxRadius=%d Z-HardCap=%d"),
        MinCoord.X, MinCoord.Y, MinCoord.Z, MaxCoord.X, MaxCoord.Y, MaxCoord.Z, MaxRadius, VerticalDiscoveryRange);

    TArray<TPair<int32,FIntVector>> Sorted;
    const FVoxelGenerationConfig Cfg = GetEffectiveConfig();
    const float GridSize = ChunkSize * VoxelSize;

    const int32 NumX = MaxCoord.X - MinCoord.X + 1;
    TArray<TArray<TPair<int32, FIntVector>>> ThreadResults;
    ThreadResults.SetNum(NumX);

    const float DiscCenterH = FVoxelBiomeManager::GetNeutralSurfaceHeightStatic(
        Cfg.Craters.ForcedCraterCenter.X, Cfg.Craters.ForcedCraterCenter.Y, Cfg);
    const int32* DiscPermTable = FVoxelNoiseSIMD::GetPermutationTable();
    const int32 NumY = MaxCoord.Y - MinCoord.Y + 1;

    ParallelFor(NumX, [&](int32 x_idx)
    {
        int32 x = MinCoord.X + x_idx;
        TArray<TPair<int32, FIntVector>>& LocalSorted = ThreadResults[x_idx];
        const float WX = (x + 0.5f) * GridSize;

        // ── AVX2 batch: 8 Y-columns at a time ────────────────────────────────
        int32 y_off = 0;
        for (; y_off <= NumY - 8; y_off += 8)
        {
            float WYs[8];
            for (int32 b = 0; b < 8; ++b)
                WYs[b] = (MinCoord.Y + y_off + b + 0.5f) * GridSize;

            __m256 WX_v = _mm256_set1_ps(WX);
            __m256 WY_v = _mm256_loadu_ps(WYs);

            FVoxelNoiseSIMD::FBiomeWeights_AVX2 Wts_v;
            __m256 Temp_v, Eros_v;
            FVoxelNoiseSIMD::EvaluateColumn_BiomeWeights_AVX2(WX_v, WY_v, Cfg, DiscPermTable, Wts_v, Temp_v, Eros_v);

            __m256 SurfH_v;
            FVoxelNoiseSIMD::EvaluateColumn_SurfaceHeight_AVX2(WX_v, WY_v, Wts_v, Cfg, DiscPermTable, Temp_v, Eros_v, SurfH_v, DiscCenterH);

            float SurfHs[8], PeaksW[8], CliffsW[8];
            _mm256_storeu_ps(SurfHs, SurfH_v);
            _mm256_storeu_ps(PeaksW, Wts_v.Peaks);
            _mm256_storeu_ps(CliffsW, Wts_v.Cliffs);

            for (int32 b = 0; b < 8; ++b)
            {
                const int32 y = MinCoord.Y + y_off + b;
                const float SurfacePos = SurfHs[b];
                const int32 GroundZ = FMath::FloorToInt(SurfacePos / GridSize);
                const float Roughness = FMath::Clamp(PeaksW[b] * 2.f + CliffsW[b], 0.f, 1.f);
                const float CraterRad = Cfg.Craters.CentralCraterRadius;

                const int32 dx_c = x - Center.X;
                const int32 dy_c = y - Center.Y;
                const int32 DistSq_C = dx_c * dx_c + dy_c * dy_c;

                // ── Ground Slab ──────────────────────────────────────────────────
                // Zone-based vertical thickness for efficiency
                int32 G_Reach = (DistSq_C <= RenderDistanceXY * RenderDistanceXY) ? 3 : 2;
                if (CraterRad > 0.f)
                {
                    const float dx_cr = WX - Cfg.Craters.ForcedCraterCenter.X;
                    const float dy_cr = WYs[b] - Cfg.Craters.ForcedCraterCenter.Y;
                    if (dx_cr*dx_cr + dy_cr*dy_cr < CraterRad * CraterRad * 2.25f) G_Reach = 18; // Crater clearance
                }

                {
                    FReadScopeLock ReadLock(LoadedChunksLock);
                    for (int32 z = GroundZ - G_Reach; z <= GroundZ + G_Reach; ++z)
                    {
                        if (z < -VerticalDiscoveryRange || z > VerticalDiscoveryRange) continue;
                        FIntVector C(x, y, z);
                        if (!LoadedChunks.Contains(C))
                        {
                            const int32 D = FMath::Max3(FMath::Abs(x - Center.X), FMath::Abs(y - Center.Y), FMath::Abs(z - Center.Z));
                            LocalSorted.Add({D, C});
                        }
                    }

                    // ── Skyland Shells ───────────────────────────────────────────────
                    // Only discover skyland 'surfaces' to save 1M+ chunks of air/rock
                    if (DistSq_C <= SkylandsRenderDistanceXY * SkylandsRenderDistanceXY)
                    {
                        float MinSkyAlt, MaxSkyAlt;
                        FVoxelBiomeGenerators::GetSkylandAltitudeBounds(SurfacePos, Roughness, Cfg, MinSkyAlt, MaxSkyAlt);
                        const int32 SkyZ_Min = FMath::FloorToInt(MinSkyAlt / GridSize);
                        const int32 SkyZ_Max = FMath::FloorToInt(MaxSkyAlt / GridSize);

                        auto AddZSafe = [&](int32 z) {
                            if (z < -VerticalDiscoveryRange || z > VerticalDiscoveryRange) return;
                            FIntVector C(x,y,z);
                            if (!LoadedChunks.Contains(C)) {
                                const int32 D = FMath::Max3(FMath::Abs(x-Center.X), FMath::Abs(y-Center.Y), FMath::Abs(z-Center.Z));
                                LocalSorted.Add({D, C});
                            }
                        };
                        
                        for (int32 sz = SkyZ_Min - 2; sz <= SkyZ_Min + 2; ++sz) AddZSafe(sz);
                        for (int32 sz = SkyZ_Max - 2; sz <= SkyZ_Max + 2; ++sz) AddZSafe(sz);
                    }
                }
            }
        }

        // ── Scalar remainder ─────────────────────────────────────────────────
        for (; y_off < NumY; ++y_off)
        {
            const int32 y = MinCoord.Y + y_off;
            const float WY = (y + 0.5f) * GridSize;
            const auto Wh = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(WX, WY, Cfg);
            const float Surface = Wh.SurfaceHeight;
            const int32 GroundZ = FMath::FloorToInt(Surface / GridSize);

            float MinSkyAlt, MaxSkyAlt;
            FVoxelBiomeGenerators::GetSkylandAltitudeBounds(Surface, Wh.Weights.GetRoughness(), Cfg, MinSkyAlt, MaxSkyAlt);
            const int32 SkyZ_Min = FMath::FloorToInt(MinSkyAlt / GridSize);
            const int32 SkyZ_Max = FMath::FloorToInt(MaxSkyAlt / GridSize);

            int32 ExtraMinZ = 0, ExtraMaxZ = 0;
            const float dx = WX - Cfg.Craters.ForcedCraterCenter.X;
            const float dy = WY - Cfg.Craters.ForcedCraterCenter.Y;
            const float DistSq = dx * dx + dy * dy;
            const float CraterRad = Cfg.Craters.CentralCraterRadius;

            if (CraterRad > 0.f && DistSq < CraterRad * CraterRad * 2.25f)
            {
                ExtraMinZ = FMath::CeilToInt(FMath::Abs(Cfg.Craters.CentralCraterDepth) / GridSize) + 3;
                ExtraMaxZ = FMath::CeilToInt(Cfg.Craters.CentralCraterRimHeight    / GridSize) + 4;
            }

            const int32 dx_c = x - Center.X;
            const int32 dy_c = y - Center.Y;
            const int32 DistSq_C = dx_c * dx_c + dy_c * dy_c;

            const int32 EffMinZ = (DistSq_C <= RenderDistanceXY * RenderDistanceXY) ? (2 + ExtraMinZ) : 
                                 (DistSq_C <= MidRenderDistanceXY * MidRenderDistanceXY ? (MidRenderDistanceZ + ExtraMinZ) : (4 + ExtraMinZ));
            const int32 EffMaxZ = (DistSq_C <= RenderDistanceXY * RenderDistanceXY) ? (12 + ExtraMaxZ) : 
                                 (DistSq_C <= MidRenderDistanceXY * MidRenderDistanceXY ? (MidRenderDistanceZ + ExtraMaxZ) : (4 + ExtraMaxZ));

            {
                FReadScopeLock ReadLock(LoadedChunksLock);
                for (int32 z = GroundZ - EffMinZ; z <= GroundZ + EffMaxZ; ++z)
                {
                    const FIntVector C(x, y, z);
                    if (!LoadedChunks.Contains(C))
                    {
                        const int32 Dist = FMath::Max3(FMath::Abs(x - Center.X), FMath::Abs(y - Center.Y), FMath::Abs(z - Center.Z));
                        LocalSorted.Add({Dist, C});
                    }
                }

                if (DistSq_C <= SkylandsRenderDistanceXY * SkylandsRenderDistanceXY)
                {
                    for (int32 z = SkyZ_Min; z <= SkyZ_Max; ++z)
                    {
                        const FIntVector C(x, y, z);
                        if ((z < GroundZ - EffMinZ || z > GroundZ + EffMaxZ) && !LoadedChunks.Contains(C))
                        {
                            const int32 Dist = FMath::Max3(FMath::Abs(x - Center.X), FMath::Abs(y - Center.Y), FMath::Abs(z - Center.Z));
                            LocalSorted.Add({Dist, C});
                        }
                    }
                }
            }
        }
    });

    for (const auto& LocalSorted : ThreadResults) { Sorted.Append(LocalSorted); }
    Sorted.Sort([](const TPair<int32,FIntVector>& A, const TPair<int32,FIntVector>& B){ return A.Key < B.Key; });

    TSet<FIntVector> QSet;
    TArray<FVoxelGenerationQueueEntry> NewQueue;
    for (auto& P : Sorted) if (!QSet.Contains(P.Value)) { QSet.Add(P.Value); NewQueue.Add(FVoxelGenerationQueueEntry(P.Value, 0.f)); }

#if WITH_EDITOR
    if (!GetWorld()->IsGameWorld())
    {
        FVector Pos = SnapToVoxelGrid(FVector(Anchor.X, Anchor.Y, 0.f));
        const auto Wh = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(Pos.X, Pos.Y, Cfg);
        const float Surface = Wh.SurfaceHeight;
        float MinSkyAlt, MaxSkyAlt;
        FVoxelBiomeGenerators::GetSkylandAltitudeBounds(Surface, Wh.Weights.GetRoughness(), Cfg, MinSkyAlt, MaxSkyAlt);
        static FVoxelDensityGenerator EdProbe;
        float TargetZ = Surface + GetSafeSpawnHeightOffset();
        bool bSky = false;
        const float SkyAlt = (MinSkyAlt + MaxSkyAlt) * 0.5f;
        const float IHT = (MaxSkyAlt - MinSkyAlt) * 0.5f;
        if (SkyAlt > Surface + 5000.f)
            for (float z2 = SkyAlt + IHT; z2 >= FMath::Max(SkyAlt - IHT, Surface + 500.f); z2 -= 200.f)
                if (EdProbe.GetDensity(Pos.X, Pos.Y, z2, Cfg) > 0.f) { TargetZ = z2 + GetSafeSpawnHeightOffset(); bSky = true; break; }
        const FIntVector SC2 = WorldToChunkCoord(Anchor);
        const int32 SkyZ = FMath::FloorToInt(TargetZ / GridSize);
        for (int32 x = -1; x <= 1; x++) for (int32 y = -1; y <= 1; y++) if (bSky && SkyZ != 0)
        {
            FIntVector S(SC2.X+x, SC2.Y+y, SkyZ); if (!QSet.Contains(S)) { QSet.Add(S); NewQueue.Add(FVoxelGenerationQueueEntry(S,0.f)); }
        }
    }
#endif

    if (IsWaitingForInitialSpawn() && SpawnHandlerComponent)
    {
        TArray<FVoxelGenerationQueueEntry> TempQueue;
        for (const FIntVector& C : SpawnHandlerComponent->GetInitialSpawnCoords())
            if (!LoadedChunks.Contains(C) && !QSet.Contains(C))
            { QSet.Add(C); TempQueue.Add(FVoxelGenerationQueueEntry(C, 0.f)); }

        for (const FIntVector& C : SpawnHandlerComponent->GetInitialSpawnCoordsVisual())
            if (!LoadedChunks.Contains(C) && !QSet.Contains(C))
            { QSet.Add(C); TempQueue.Add(FVoxelGenerationQueueEntry(C, 0.f)); }

        if (TempQueue.Num() > 0) NewQueue.Insert(MoveTemp(TempQueue), 0);
    }

    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Queued %d chunks."), NewQueue.Num());
    {
        FScopeLock Lock(&GenerationQueueLock);
        GenerationQueue = MoveTemp(NewQueue);
        QueueHead = 0;
        EmptyChunks.Empty(); 
    }
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
                if (!IsValid(S) || S->bShutdown) { if(S) S->DrainTickerHandle.Reset(); return false; }
                S->DrainGenerationQueue();
                if (S->QueueHead >= S->GenerationQueue.Num())
                { 
                    UE_LOG(LogVoxelWorld,Log,TEXT("VoxelWorld: Editor gen done. %d chunks."),S->LoadedChunks.Num()); 
                    S->bGenerationActive.Store(false); // Unlock
                    S->DrainTickerHandle.Reset(); 
                    return false; 
                }
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
void AVoxelWorld::SpawnChunk(FIntVector Coord, bool bSyncCollision)
{
    {
        FReadScopeLock ReadLock(LoadedChunksLock);
        if (LoadedChunks.Contains(Coord)) return;
    }
    AVoxelChunk* Chunk = ChunkPool.RetrieveOrCreateChunk(GetWorld(), ChunkCoordToWorld(Coord), this);
    if (!Chunk) return;

    Chunk->SetActorHiddenInGame(false);
    if (Chunk->GetProceduralMesh())
    {
        Chunk->GetProceduralMesh()->SetVisibility(false);
        // FORCE-FIX: always false to prevent falling through terrain while cooking.
        Chunk->GetProceduralMesh()->bUseAsyncCooking = false; 
    }
    Chunk->SetOwner(this);
#if WITH_EDITOR
    if (!GetWorld()->IsGameWorld()) // only label in editor viewport previews, NOT in PIE streaming
    {
        Chunk->SetActorLabel(FString::Printf(TEXT("Chunk_%d_%d_%d"), Coord.X, Coord.Y, Coord.Z));
        Chunk->SetFolderPath(FName(*FString::Printf(TEXT("g_VoxelChunks/Z%d"), Coord.Z)));
    }
#endif
    Chunk->SetChunkCoord(Coord);
    Chunk->SetActorLocation(ChunkCoordToWorld(Coord));

    // FIX Distant Chunk Lag: Compute initial LOD BEFORE generating.
    // FIX SPAWN-LOD: During initial spawn the player is parked at Z=100000 so
    // Player->GetActorLocation() gives an astronomically large DistSq for every
    // chunk, forcing them all to LOD 2 (StepSize=4). At StepSize=4 the density
    // grid is 7×7×7 — thin rim walls (1-2 voxels) fall below the Nyquist limit
    // and are completely invisible. Fix: use SpawnTargetPos (the real geographic
    // spawn centre) during bWaitingForInitialSpawn so each chunk gets the correct
    // geographic LOD assignment. Zone C (rim) gets LOD 1 not LOD 2 → StepSize=2
    // → 11×11×11 grid → rim walls reliably captured.
    {
        // Cache pawn pointer ONCE — calling GetPlayerPawn twice is a TOCTOU:
        // the pawn can become pending-kill between the null-check and the
        // dereference, causing the ACCESS_VIOLATION crash in DrainGenerationQueue.
        APawn* CachedPawn = UGameplayStatics::GetPlayerPawn(this, 0);
        const FVector RefPos = IsWaitingForInitialSpawn()
            ? SpawnTargetPos
            : (IsValid(CachedPawn) ? CachedPawn->GetActorLocation() : SpawnTargetPos);

        const FVector ChunkPos = ChunkCoordToWorld(Coord) + FVector(ChunkSize * VoxelSize * 0.5f);
        const float DistSq = FVector::DistSquared2D(RefPos, ChunkPos);

        int32 TargetLOD = 0;
        if      (DistSq > LOD2Distance * LOD2Distance) TargetLOD = 2;
        else if (DistSq > LOD1Distance * LOD1Distance) TargetLOD = 1;

        // High elevation chunks retain 3-D mesh overhangs
        if (Coord.Z >= 4) TargetLOD = FMath::Min(TargetLOD, 1);

        // Immediate-vicinity safeguard: always full detail within 3 chunk radii
        const float SafeDist = Chunk->ChunkSize * Chunk->VoxelSize * 3.2f;
        if (DistSq < SafeDist * SafeDist) TargetLOD = 0;

        Chunk->SetLOD(TargetLOD);
    }

    ConfigureChunk(Chunk);
    {
        FWriteScopeLock WriteLock(LoadedChunksLock);
        LoadedChunks.Add(Coord, Chunk);
    }

    ActiveGenerations++;
    TWeakObjectPtr<AVoxelWorld> WeakThis(this);
    const FIntVector ChunkCoord = Coord; // capture by value for lambda
    Chunk->OnGenerationComplete = [WeakThis, ChunkCoord]()
    {
        if (AVoxelWorld* S = WeakThis.Get())
        {
            S->ActiveGenerations -= 1;
            // FIX-1: Feed the pending-set so CheckCloseRange visibility only
            // iterates chunks that JUST became ready, not all loaded chunks.
            S->ChunksNeedingVisibilityCheck.Add(ChunkCoord);
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
    AVoxelChunk* Chunk = nullptr;
    {
        FWriteScopeLock WriteLock(LoadedChunksLock);
        AVoxelChunk** P = LoadedChunks.Find(Coord);
        if (!P || !*P) return;
        Chunk = *P;
        LoadedChunks.Remove(Coord);
    }

    if (WaterSystemComponent)
    {
        if (WaterSystemComponent->GetSimulator())
            WaterSystemComponent->GetSimulator()->UnregisterChunk(Coord);
        WaterSystemComponent->RemoveChunkFromWaterSimulation(Coord);
    }
    if (Chunk->IsGenerating()) Chunk->CancelGeneration();

    // LoadedChunks.Remove(Coord); // Removed here, now handled in atomic block above
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
    const bool bFastDrain = IsWaitingForInitialSpawn();
    // PERF: Capping fast drain to 256 per frame protects the GameThread while fast-forwarding initial queue
    const int32 Limit = bFastDrain ? 256 : (!GetWorld()->IsGameWorld() ? 4 : 8);
    const int32 MaxConc = bFastDrain ? 2048 : MaxConcurrentGenerations;

    // PERF-4: compute once per drain cycle — ConfigureChunk reads by const-ref.
    CachedEffectiveConfig = GetEffectiveConfig();
    int32 N = 0;
    while (N < Limit)
    {
        if ((int32)ActiveGenerations >= MaxConc) break;

        FIntVector NextCoord(0);
        bool bHaveCoord = false;
        {
            FScopeLock Lock(&GenerationQueueLock);
            if (QueueHead < GenerationQueue.Num() && GenerationQueue.IsValidIndex(QueueHead))
            {
                NextCoord = GenerationQueue[QueueHead++].Coord;
                bHaveCoord = true; // Lock secured extraction
            }
        }
        
        if (!bHaveCoord) break;

        // Isolate dereference into local variable to separate instruction trace branches
        SpawnChunk(NextCoord);
        N++;
    }
    // PERF-5: Never RemoveAt(0,N) — that's an O(remaining) element shift.
    // Instead, only reset once the queue is fully consumed. The backing array
    // stays hot in cache during the fill phase with no shifting overhead.
    {
        FScopeLock Lock(&GenerationQueueLock);
        if (QueueHead >= GenerationQueue.Num() && GenerationQueue.Num() > 0)
        {
            GenerationQueue.Reset();
            QueueHead = 0;
        }
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
        if (*It && (*It)->GetOwner()==this) 
        { 
            FWriteScopeLock WriteLock(LoadedChunksLock);
            LoadedChunks.Add((*It)->GetChunkCoord(),*It); 
            N++; 
        }
    UE_LOG(LogVoxelWorld,Log,TEXT("VoxelWorld: Discovered %d chunks"),N);
}

void AVoxelWorld::ConfigureChunk(AVoxelChunk* Chunk) const
{
    if (!Chunk) return;
    // PERF-4: DrainGenerationQueue computes GetEffectiveConfig() once per drain
    // cycle and passes it here so we don't deep-copy the large struct on every spawn.
    // CachedEffectiveConfig is set just before SpawnChunk is called.
    FVoxelGenerationConfig LocalCfg = CachedEffectiveConfig;
    LocalCfg.Craters.ForcedCraterCenter = FVector2D(SpawnTargetPos.X, SpawnTargetPos.Y);
    LocalCfg.SlopeThreshold = SlopeThreshold;

    Chunk->ChunkSize = ChunkSize;
    Chunk->VoxelSize = VoxelSize;
    Chunk->MasterFlatMaterial = MasterFlatMaterial;
    Chunk->MasterSlopeMaterial = MasterSlopeMaterial;
    Chunk->SlopeThreshold = SlopeThreshold;
    Chunk->SetGenerationConfig(LocalCfg);
    Chunk->TreeMesh = TreeMesh;
    Chunk->GrassMesh = GrassMesh;
    Chunk->FoliageDensity = FoliageDensity;
    Chunk->MaxFoliageSlope = MaxFoliageSlope;
    Chunk->SetDensityGenerator(DensityGenerator.Get());
    Chunk->WaterMaterial = GenerationConfig.Water.OceanMaterial.Get();
    Chunk->DenseChunk = const_cast<AVoxelWorld*>(this)->ChunkManager.GetOrCreateChunk(Chunk->GetChunkCoord(), Chunk->ChunkSize);
}

// ============================================================
//  ProcessInitialPlayerSpawn
// ============================================================
void AVoxelWorld::ProcessInitialPlayerSpawn()
{
    if (SpawnHandlerComponent)
    {
        SpawnHandlerComponent->ProcessInitialPlayerSpawn();
    }
}

