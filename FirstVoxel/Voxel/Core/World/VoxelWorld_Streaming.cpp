// VoxelWorld_Streaming.cpp — Valheim-scale render distance
//
// THREE-ZONE STREAMING:
//
//  ZONE A  dist ≤ RenderDistanceXY (14)     LOD 0   Z ± RenderDistanceZ (10)
//           224m radius full-detail playspace — every voxel sharp
//
//  ZONE B  dist ≤ MidRenderDistanceXY (40)  LOD 1   Z ± MidRenderDistanceZ (4)
//           640m radius mid-ground — hills, forests readable at half-resolution
//
//  ZONE C  dist ≤ DistantRenderDistanceXY (80) LOD 2  Z ± 1
//           1280m radius silhouette horizon — distant landmasses visible
//           Each LOD2 chunk = 4×4 voxel heightmap, essentially free to generate
//
// SKYLANDS: separate volume centred on the island altitude band.

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Async/ParallelFor.h"
#include "Voxel/Biomes/VoxelBiomeManager.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "Voxel/Core/VoxelChunk.h"
#include "Voxel/Core/World/VoxelWorld.h"
#include "Voxel/VoxelLogger.h"

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

    const FVector     PlayerPos   = Player->GetActorLocation();
    const FIntVector  PlayerCoord = WorldToChunkCoord(PlayerPos);

    // FIX: GetEffectiveConfig() returns VALUE — store as value, not reference.
    // Old: const FVoxelGenerationConfig& Config = GetEffectiveConfig();
    //      → dangling reference to a temporary, undefined behaviour.
    const FVoxelGenerationConfig Config = GetEffectiveConfig();
    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
    const float ChunkWorldSize = ChunkSize * VoxelSize;

    // ── Cached sky altitude ───────────────────────────────────────────────
    if (FVector::DistSquared(PlayerPos, LastSkyAltPos) > SkyAltSnapDist * SkyAltSnapDist)
    {
        LastSkyAltPos = PlayerPos;
        const FVoxelBiomeWeightMap W = FVoxelBiomeManager::GetBiomeWeightsStatic(PlayerPos.X, PlayerPos.Y, Config);
        const float NeutralSH = FVoxelBiomeManager::GetNeutralSurfaceHeightStatic(PlayerPos.X, PlayerPos.Y, Config);
        const float SH = NeutralSH;
        const float HN = FMath::Clamp(SH / SC.MaxTerrainReference, 0.f, 1.f);
        const float RN = FMath::Clamp(W.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);
        const float TS = FMath::Clamp(HN*1.5f + RN*0.8f, 0.f, 1.f);
        CachedCurvedH = FMath::Pow(HN, 2.5f);
        CachedCurvedR = FMath::Pow(RN, 2.0f);
        const float AltBase = FMath::Lerp(SC.MinAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, TS);
        const float ShardT  = FMath::SmoothStep(0.f, SC.ShardTransitionStrength, TS);
        const float DecoupledH = FMath::Lerp(15000.f, SH, ShardT);
        CachedSkyAltWorld = DecoupledH + AltBase
            + ShardT * (CachedCurvedH * SC.HeightAltitudeBonus + CachedCurvedR * SC.RoughnessAltitudeBonus);
    }

    const float SkyAltWorld = CachedSkyAltWorld;
    const float IslandSize  = FMath::Max(SC.BaseIslandSize,
                               SC.BaseIslandSize + CachedCurvedH*SC.HeightSizeBonus
                                                  + CachedCurvedR*SC.RoughnessSizeBonus);
    const float HalfThickCm = FMath::Max(200.f, IslandSize * SC.ThicknessRatio);

    // ── Sample ground heights for all columns (parallel) ──────────────────
    const int32 MaxRad  = DistantRenderDistanceXY;
    const int32 GridDim = 2 * MaxRad + 1;
    const int32 NumCols = GridDim * GridDim;

    TArray<int32> GroundZCenters;
    GroundZCenters.SetNumZeroed(NumCols);

    ParallelFor(NumCols, [&](int32 Index)
    {
        const int32 x = -MaxRad + (Index % GridDim);
        const int32 y = -MaxRad + (Index / GridDim);
        const float ColX = (PlayerCoord.X + x + 0.5f) * ChunkWorldSize;
        const float ColY = (PlayerCoord.Y + y + 0.5f) * ChunkWorldSize;
        const FVoxelBiomeManager::FWeightsAndHeight Wh =
            FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(ColX, ColY, Config);
        GroundZCenters[Index] = FMath::RoundToInt(Wh.SurfaceHeight / ChunkWorldSize);
    });

    // ── Build desired chunk set ───────────────────────────────────────────
    TSet<FIntVector> Desired;
    Desired.Reserve(NumCols * 3);

    for (int32 Index = 0; Index < NumCols; ++Index)
    {
        const int32 x     = -MaxRad + (Index % GridDim);
        const int32 y     = -MaxRad + (Index / GridDim);
        const int32 radSq = x*x + y*y;
        const int32 GZ    = GroundZCenters[Index];

        if (radSq <= RenderDistanceXY * RenderDistanceXY)
        {
            // Zone A: full detail, full vertical range (caves + sky)
            for (int32 z = -RenderDistanceZ; z <= RenderDistanceZ; ++z)
                Desired.Add(FIntVector(PlayerCoord.X+x, PlayerCoord.Y+y, GZ+z));
        }
        else if (radSq <= MidRenderDistanceXY * MidRenderDistanceXY)
        {
            // Zone B: LOD 1, thin vertical slice centred on terrain
            for (int32 z = -MidRenderDistanceZ; z <= MidRenderDistanceZ; ++z)
                Desired.Add(FIntVector(PlayerCoord.X+x, PlayerCoord.Y+y, GZ+z));
        }
        else if (radSq <= MaxRad * MaxRad)
        {
            // Zone C: LOD 2, surface ± 1 chunk (horizon silhouette only)
            Desired.Add(FIntVector(PlayerCoord.X+x, PlayerCoord.Y+y, GZ - 1));
            Desired.Add(FIntVector(PlayerCoord.X+x, PlayerCoord.Y+y, GZ));
            Desired.Add(FIntVector(PlayerCoord.X+x, PlayerCoord.Y+y, GZ + 1));
        }
    }

    // Guarantee walkable ground centred on player altitude
    for (int32 z = -RenderDistanceZ; z <= RenderDistanceZ; ++z)
    for (int32 y = -RenderDistanceXY; y <= RenderDistanceXY; ++y)
    for (int32 x = -RenderDistanceXY; x <= RenderDistanceXY; ++x)
        if (x*x + y*y <= RenderDistanceXY*RenderDistanceXY)
            Desired.Add(PlayerCoord + FIntVector(x, y, z));

    // Skylands band — cylinder covering the full island altitude
    const int32 SkyZMin = FMath::FloorToInt((SkyAltWorld - HalfThickCm * 2.f) / ChunkWorldSize);
    const int32 SkyZMax = FMath::CeilToInt ((SkyAltWorld + HalfThickCm * 2.f) / ChunkWorldSize);
    for (int32 z = SkyZMin; z <= SkyZMax + SkylandsRenderDistanceZ; ++z)
    for (int32 y = -SkylandsRenderDistanceXY; y <= SkylandsRenderDistanceXY; ++y)
    for (int32 x = -SkylandsRenderDistanceXY; x <= SkylandsRenderDistanceXY; ++x)
        if (x*x + y*y <= SkylandsRenderDistanceXY*SkylandsRenderDistanceXY)
            Desired.Add(FIntVector(PlayerCoord.X+x, PlayerCoord.Y+y, z));

    // ── Destroy out-of-range chunks ───────────────────────────────────────
    TArray<FIntVector> ToRemove;
    for (auto& It : LoadedChunks)
        if (!Desired.Contains(It.Key)) ToRemove.Add(It.Key);
    for (const FIntVector& C : ToRemove) { DestroyChunk(C); EmptyChunks.Remove(C); }

    // ── PASS 1: Desired LOD per chunk ─────────────────────────────────────
    TMap<FIntVector, int32> DesiredLODs;
    DesiredLODs.Reserve(LoadedChunks.Num());

    for (auto& It : LoadedChunks)
    {
        AVoxelChunk* Chunk = It.Value;
        if (!IsValid(Chunk)) continue;

        const FVector ChunkPos = ChunkCoordToWorld(It.Key) + FVector(ChunkSize*VoxelSize*0.5f);
        const float   DistSq   = FVector::DistSquared(PlayerPos, ChunkPos);

        static constexpr float HOut = 1.10f, HIn = 0.90f;
        const float L1ISq = LOD1Distance*LOD1Distance*HIn*HIn;
        const float L1OSq = LOD1Distance*LOD1Distance*HOut*HOut;
        const float L2ISq = LOD2Distance*LOD2Distance*HIn*HIn;
        const float L2OSq = LOD2Distance*LOD2Distance*HOut*HOut;

        int32 LOD = Chunk->LOD;
        if      (LOD < 2 && DistSq > L2OSq) LOD = 2;
        else if (LOD > 1 && DistSq < L2ISq) LOD = 1;
        else if (LOD < 1 && DistSq > L1OSq) LOD = 1;
        else if (LOD > 0 && DistSq < L1ISq) LOD = 0;

        if (bWaitingForInitialSpawn && InitialSpawnCoords.Contains(It.Key))
            LOD = 0;

        // Close-range always full detail
        const float SafeDistSq = (ChunkSize*VoxelSize*3.2f)*(ChunkSize*VoxelSize*3.2f);
        if (DistSq < SafeDistSq) LOD = 0;

        // Zone floor — can only upgrade LOD, never downgrade close chunks
        const int32 dx = FMath::Abs(It.Key.X - PlayerCoord.X);
        const int32 dy = FMath::Abs(It.Key.Y - PlayerCoord.Y);
        const int32 rSq = dx*dx + dy*dy;
        if      (rSq > MidRenderDistanceXY * MidRenderDistanceXY) LOD = FMath::Max(LOD, 2);
        else if (rSq > RenderDistanceXY    * RenderDistanceXY)    LOD = FMath::Max(LOD, 1);

        DesiredLODs.Add(It.Key, LOD);
    }

    // ── PASS 2: BFS LOD consistency (O(changed × 6)) ─────────────────────
    {
        TArray<FIntVector> Queue; TSet<FIntVector> Visited;
        for (auto& It : DesiredLODs)
        {
            AVoxelChunk** CP = LoadedChunks.Find(It.Key);
            if (CP && IsValid(*CP) && It.Value != (*CP)->LOD)
                Queue.Add(It.Key);
        }
        const FIntVector Adj[6]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (int32 H = 0; H < Queue.Num(); ++H)
        {
            const FIntVector C = Queue[H];
            if (Visited.Contains(C)) continue;
            Visited.Add(C);
            const int32* ML = DesiredLODs.Find(C);
            if (!ML) continue;
            for (const FIntVector& A : Adj)
            {
                int32* NL = DesiredLODs.Find(C+A);
                if (NL && *NL > *ML) { *NL = *ML; if (!Visited.Contains(C+A)) Queue.Add(C+A); }
            }
        }
    }

    // ── PASS 3: Apply transitions ─────────────────────────────────────────
    for (auto& It : LoadedChunks)
    {
        AVoxelChunk* Chunk = It.Value;
        if (!IsValid(Chunk)) continue;
        const int32* FL = DesiredLODs.Find(It.Key);
        if (!FL || *FL == Chunk->LOD) continue;
        if (Chunk->IsReady() && !Chunk->IsGenerating())
            Chunk->TransitionToLOD(*FL);
        else { Chunk->bPendingLODTransition = true; Chunk->PendingLOD = *FL; }
    }

    // ── Merge & sort generation queue by distance ─────────────────────────
    TSet<FIntVector> Merged;
    Merged.Reserve(Desired.Num() + (GenerationQueue.Num()-QueueHead));
    for (const FIntVector& C : Desired)
        if (!LoadedChunks.Contains(C) && !EmptyChunks.Contains(C)) Merged.Add(C);
    for (int32 i=QueueHead; i<GenerationQueue.Num(); ++i) Merged.Add(GenerationQueue[i]);

    TArray<TPair<int32,FIntVector>> Sorted;
    Sorted.Reserve(Merged.Num());
    for (const FIntVector& C : Merged)
    {
        const FIntVector L = C - PlayerCoord;
        Sorted.Add({L.X*L.X + L.Y*L.Y + L.Z*L.Z, C});
    }
    Sorted.Sort([](const TPair<int32,FIntVector>& A, const TPair<int32,FIntVector>& B){ return A.Key<B.Key; });

    GenerationQueue.Reset();
    GenerationQueue.Reserve(Sorted.Num());
    for (const auto& P : Sorted) GenerationQueue.Add(P.Value);
    QueueHead = 0;
}

void AVoxelWorld::CheckCloseRangeVisibility()
{
    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!Player) return;
    const FVector Pos       = Player->GetActorLocation();
    const float   Threshold = ChunkSize * VoxelSize * 3.0f;
    for (auto& P : LoadedChunks)
    {
        AVoxelChunk* Chunk = P.Value;
        if (!Chunk || FVector::Dist(Chunk->GetActorLocation(), Pos) >= Threshold) continue;
        if (Chunk->IsReady() && !Chunk->IsGenerating())
        {
            if (UProceduralMeshComponent* PM = Chunk->GetProceduralMesh())
                if (!PM->IsVisible()) PM->SetVisibility(true);
            Chunk->bMeshDirty = false;
        }
    }
}
