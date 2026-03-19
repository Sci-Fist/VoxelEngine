// VoxelWorld_Streaming.cpp
//
// Chunk streaming and LOD management for AVoxelWorld.
//
// FIXES APPLIED IN THIS REVISION
// ───────────────────────────────
// FIX-LOD-BFS: Replaced the O(N × 6 × MaxPasses) multi-pass LOD consistency
//   loop with a BFS dirty-queue seeded from chunks whose LOD changed in Pass 1.
//   The BFS propagates higher detail (lower LOD number) outward to neighbours,
//   then stops. Complexity is O(changed_chunks × 6) amortised — typically near
//   zero when the player is stationary, vs. up to 52,000 TMap lookups per
//   streaming tick for the old approach at render distance 8.
//
// FIX-DEAD-CODE: Removed ApplyMeshToChunk() and EnforceLODConsistency().
//   These functions were defined here but never called from any code path.
//   Their declarations have been removed from VoxelWorld.h as well.
//   CheckCloseRangeVisibility() is kept — it IS called from VoxelWorld.cpp Tick.

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Voxel/Biomes/VoxelBiomeManager.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "Voxel/Core/VoxelChunk.h"
#include "Voxel/Core/World/VoxelWorld.h"
#include "Voxel/VoxelLogger.h"

// ============================================================
//  Chunk Streaming
// ============================================================

void AVoxelWorld::UpdateChunkStreaming()
{
    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!Player || !GetWorld()->IsGameWorld()) return;

    StreamingTimer = 0.f;

    const FVector CurrentPos = Player->GetActorLocation();
    const float MinStep = ChunkSize * VoxelSize * 0.4f;
    if (FVector::DistSquared(CurrentPos, LastStreamedPos) < MinStep * MinStep)
        return;
    LastStreamedPos = CurrentPos;

    FVector PlayerPos = Player->GetActorLocation();
    FIntVector PlayerCoord = WorldToChunkCoord(PlayerPos);

    const FVoxelGenerationConfig& Config = GetEffectiveConfig();
    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
    const float ChunkWorldSize = ChunkSize * VoxelSize;

    // Cache sky altitude (recompute only when player moves > SkyAltSnapDist).
    if (FVector::DistSquared(PlayerPos, LastSkyAltPos) > SkyAltSnapDist * SkyAltSnapDist)
    {
        LastSkyAltPos = PlayerPos;

        const FVoxelBiomeManager::FWeightsAndHeight Wh =
            FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(PlayerPos.X, PlayerPos.Y, Config);
        const float PlayerSurfH = Wh.SurfaceHeight;

        const float HeightNormSky    = FMath::Clamp(PlayerSurfH / SC.MaxTerrainReference, 0.f, 1.f);
        const float RoughnessNormSky = FMath::Clamp(Wh.Weights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);
        const float TerrainStrSky    = FMath::Clamp(HeightNormSky * 1.5f + RoughnessNormSky * 0.8f, 0.f, 1.f);
        CachedCurvedH = FMath::Pow(HeightNormSky, 2.5f);
        CachedCurvedR = FMath::Pow(RoughnessNormSky, 2.0f);
        const float AltBase = FMath::Lerp(SC.MinAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, TerrainStrSky);

        static constexpr float AbsoluteSkyAnchor = 15000.f;
        const float ShardT      = FMath::SmoothStep(0.f, SC.ShardTransitionStrength, TerrainStrSky);
        const float DecoupledH  = FMath::Lerp(AbsoluteSkyAnchor, PlayerSurfH, ShardT);
        CachedSkyAltWorld = DecoupledH + AltBase +
                            ShardT * (CachedCurvedH * SC.HeightAltitudeBonus +
                                      CachedCurvedR * SC.RoughnessAltitudeBonus);
    }

    const float SkyAltWorld = CachedSkyAltWorld;

    const float IslandSize =
        FMath::Max(SC.BaseIslandSize, SC.BaseIslandSize +
                                          CachedCurvedH * SC.HeightSizeBonus +
                                          CachedCurvedR * SC.RoughnessSizeBonus);
    const float HalfThickCm = FMath::Max(200.f, IslandSize * SC.ThicknessRatio);

    // ── Build desired chunk set ────────────────────────────────────────────

    TSet<FIntVector> Desired;

    // 1. Ground area — per-column heightmap profiling
    const int32 GridDim  = 2 * RenderDistanceXY + 1;
    const int32 NumCols  = GridDim * GridDim;

    TArray<int32> GroundZCenters;
    GroundZCenters.SetNumZeroed(NumCols);

    ParallelFor(NumCols, [&](int32 Index)
    {
        const int32 x = -RenderDistanceXY + (Index % GridDim);
        const int32 y = -RenderDistanceXY + (Index / GridDim);
        const float ColX = (PlayerCoord.X + x + 0.5f) * ChunkWorldSize;
        const float ColY = (PlayerCoord.Y + y + 0.5f) * ChunkWorldSize;

        const FVoxelBiomeManager::FWeightsAndHeight Wh =
            FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(ColX, ColY, Config);
        GroundZCenters[Index] = FMath::RoundToInt(Wh.SurfaceHeight / ChunkWorldSize);
    });

    for (int32 Index = 0; Index < NumCols; ++Index)
    {
        const int32 x = -RenderDistanceXY + (Index % GridDim);
        const int32 y = -RenderDistanceXY + (Index / GridDim);
        const int32 GroundZCenter = GroundZCenters[Index];

        for (int32 z = -RenderDistanceZ; z <= RenderDistanceZ; ++z)
        {
            Desired.Add(FIntVector(PlayerCoord.X + x, PlayerCoord.Y + y, GroundZCenter + z));
        }
    }

    // 2. Play-space fallback centred on player altitude (cylindrical)
    for (int32 z = -RenderDistanceZ; z <= RenderDistanceZ; ++z)
    for (int32 y = -RenderDistanceXY; y <= RenderDistanceXY; ++y)
    for (int32 x = -RenderDistanceXY; x <= RenderDistanceXY; ++x)
    {
        if (x * x + y * y <= RenderDistanceXY * RenderDistanceXY)
            Desired.Add(PlayerCoord + FIntVector(x, y, z));
    }

    // 3. Skylands volume
    const float HalfThickWorld = HalfThickCm;
    const int32 SkyZMin = FMath::FloorToInt((SkyAltWorld - HalfThickWorld) / ChunkWorldSize);
    const int32 SkyZMax = FMath::CeilToInt ((SkyAltWorld + HalfThickWorld) / ChunkWorldSize);

    for (int32 z = SkyZMin; z <= SkyZMax; ++z)
    for (int32 y = -SkylandsRenderDistanceXY; y <= SkylandsRenderDistanceXY; ++y)
    for (int32 x = -SkylandsRenderDistanceXY; x <= SkylandsRenderDistanceXY; ++x)
    {
        if (x * x + y * y <= SkylandsRenderDistanceXY * SkylandsRenderDistanceXY)
            Desired.Add(FIntVector(PlayerCoord.X + x, PlayerCoord.Y + y, z));
    }

    // ── Destroy out-of-range chunks ────────────────────────────────────────
    TArray<FIntVector> ToRemove;
    for (auto& It : LoadedChunks)
    {
        if (!Desired.Contains(It.Key)) ToRemove.Add(It.Key);
    }
    for (const FIntVector& C : ToRemove)
    {
        DestroyChunk(C);
        EmptyChunks.Remove(C);
    }

    // ── PASS 1: Compute desired LOD per chunk ──────────────────────────────
    TMap<FIntVector, int32> DesiredLODs;
    DesiredLODs.Reserve(LoadedChunks.Num());

    for (auto& It : LoadedChunks)
    {
        AVoxelChunk* Chunk = It.Value;
        if (!IsValid(Chunk)) continue;

        const FVector ChunkPos =
            ChunkCoordToWorld(It.Key) + FVector(ChunkSize * VoxelSize * 0.5f);
        const float DistSq = FVector::DistSquared(PlayerPos, ChunkPos);

        // Hysteresis bands prevent LOD flip-flopping at distance thresholds.
        static constexpr float HysteresisFactor = 1.10f;
        const float L1ISq = LOD1Distance * LOD1Distance;
        const float L1OSq = LOD1Distance * LOD1Distance * HysteresisFactor * HysteresisFactor;
        const float L2ISq = LOD2Distance * LOD2Distance;
        const float L2OSq = LOD2Distance * LOD2Distance * HysteresisFactor * HysteresisFactor;

        int32 TargetLOD = Chunk->LOD;
        if (Chunk->LOD < 2 && DistSq > L2OSq) TargetLOD = 2;
        else if (Chunk->LOD > 1 && DistSq < L2ISq) TargetLOD = 1;
        else if (Chunk->LOD < 1 && DistSq > L1OSq) TargetLOD = 1;
        else if (Chunk->LOD > 0 && DistSq < L1ISq) TargetLOD = 0;

        // Force LOD 0 for spawn-area chunks during initial spawn lock.
        if (bWaitingForInitialSpawn && InitialSpawnCoords.Contains(It.Key))
            TargetLOD = 0;

        // Close-range safety: always LOD 0 within 3.2 chunks.
        static constexpr float DetailSafeguardRange = 3.2f;
        const float SafeRangeDistSq =
            (ChunkSize * VoxelSize * DetailSafeguardRange) *
            (ChunkSize * VoxelSize * DetailSafeguardRange);
        if (DistSq < SafeRangeDistSq) TargetLOD = 0;

        DesiredLODs.Add(It.Key, TargetLOD);
    }

    // ── PASS 2: BFS LOD consistency propagation ────────────────────────────
    //
    // Replaces the old O(N × 6 × MaxPasses) multi-pass loop.
    //
    // Seeds the BFS queue with every chunk whose desired LOD differs from its
    // current LOD (i.e. chunks that just changed). For each dequeued chunk we
    // check its 6 face-neighbours: if a neighbour's desired LOD is higher
    // (coarser) than ours, we upgrade it and enqueue it for further
    // propagation. This spreads higher detail (lower LOD number) outward from
    // the player until no more upgrades are needed.
    //
    // Complexity: O(changed_chunks × 6) per streaming tick, amortised near
    // zero when the player is stationary.
    {
        TArray<FIntVector> BfsQueue;
        TSet<FIntVector>   Visited;

        // Seed: chunks whose desired LOD differs from current LOD.
        for (auto& It : DesiredLODs)
        {
            AVoxelChunk** CPtr = LoadedChunks.Find(It.Key);
            if (CPtr && IsValid(*CPtr) && It.Value != (*CPtr)->LOD)
                BfsQueue.Add(It.Key);
        }

        const FIntVector Offsets[6] = {
            {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
        };

        // BFS: propagate lower LOD number (higher detail) to neighbours.
        for (int32 Head = 0; Head < BfsQueue.Num(); ++Head)
        {
            const FIntVector Coord = BfsQueue[Head];
            if (Visited.Contains(Coord)) continue;
            Visited.Add(Coord);

            const int32* MyLODPtr = DesiredLODs.Find(Coord);
            if (!MyLODPtr) continue;

            for (const FIntVector& Off : Offsets)
            {
                const FIntVector NCoord = Coord + Off;
                int32* NLODPtr = DesiredLODs.Find(NCoord);
                if (!NLODPtr) continue;

                if (*NLODPtr > *MyLODPtr)
                {
                    // Neighbour is coarser than us — upgrade it.
                    *NLODPtr = *MyLODPtr;
                    if (!Visited.Contains(NCoord))
                        BfsQueue.Add(NCoord);
                }
            }
        }
    }

    // ── PASS 3: Apply LOD transitions ─────────────────────────────────────
    for (auto& It : LoadedChunks)
    {
        const FIntVector& Coord = It.Key;
        AVoxelChunk* Chunk = It.Value;
        if (!IsValid(Chunk)) continue;

        const int32* FinalLODPtr = DesiredLODs.Find(Coord);
        if (!FinalLODPtr) continue;

        const int32 FinalLOD = *FinalLODPtr;
        if (FinalLOD != Chunk->LOD)
        {
            UE_LOG(LogVoxelWorld, Log,
                TEXT("VoxelWorld: Chunk (%d,%d,%d) LOD %d -> %d"),
                Coord.X, Coord.Y, Coord.Z, Chunk->LOD, FinalLOD);

            if (Chunk->IsReady() && !Chunk->IsGenerating())
                Chunk->TransitionToLOD(FinalLOD);
            else
            {
                Chunk->bPendingLODTransition = true;
                Chunk->PendingLOD = FinalLOD;
            }
        }
    }

    // ── Merge & re-sort generation queue ──────────────────────────────────
    TSet<FIntVector> UniqueMerged;
    UniqueMerged.Reserve(Desired.Num() + (GenerationQueue.Num() - QueueHead));

    for (const FIntVector& C : Desired)
    {
        if (!LoadedChunks.Contains(C) && !EmptyChunks.Contains(C))
            UniqueMerged.Add(C);
    }
    for (int32 i = QueueHead; i < GenerationQueue.Num(); ++i)
        UniqueMerged.Add(GenerationQueue[i]);

    TArray<TPair<int32, FIntVector>> SortedQueue;
    SortedQueue.Reserve(UniqueMerged.Num());
    for (const FIntVector& C : UniqueMerged)
    {
        const FIntVector Local = C - PlayerCoord;
        const int32 DistSq = Local.X * Local.X + Local.Y * Local.Y + Local.Z * Local.Z;
        SortedQueue.Add(TPair<int32, FIntVector>(DistSq, C));
    }
    SortedQueue.Sort([](const TPair<int32,FIntVector>& A, const TPair<int32,FIntVector>& B)
    {
        return A.Key < B.Key;
    });

    GenerationQueue.Reset();
    GenerationQueue.Reserve(SortedQueue.Num());
    for (const auto& Pair : SortedQueue)
        GenerationQueue.Add(Pair.Value);
    QueueHead = 0;

    if (SortedQueue.Num() > 0)
    {
        UE_LOG(LogVoxelWorld, Verbose,
            TEXT("VoxelWorld: Streaming re-sorted %d chunks in generation queue"),
            SortedQueue.Num());
    }
}

// ============================================================
//  Close-range visibility health check
//  Called every Tick from VoxelWorld.cpp.
//  Ensures chunks within 3-chunk radius that are Ready keep their mesh visible.
// ============================================================
void AVoxelWorld::CheckCloseRangeVisibility()
{
    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!Player) return;

    const FVector PlayerPos          = Player->GetActorLocation();
    const float   CloseRangeThreshold = ChunkSize * VoxelSize * 3.0f;

    for (auto& ChunkPair : LoadedChunks)
    {
        AVoxelChunk* Chunk = ChunkPair.Value;
        if (!Chunk) continue;

        const float Distance = FVector::Dist(Chunk->GetActorLocation(), PlayerPos);
        if (Distance >= CloseRangeThreshold) continue;

        // Only act on chunks that are ready and not currently generating.
        if (Chunk->IsReady() && !Chunk->IsGenerating())
        {
            UProceduralMeshComponent* PM = Chunk->GetProceduralMesh();
            if (PM && !PM->IsVisible())
                PM->SetVisibility(true);

            // Clear stale dirty flag — the mesh is already uploaded.
            if (Chunk->bMeshDirty)
                Chunk->bMeshDirty = false;
        }
    }
}
