// VoxelStreamingComponent.cpp
#include "VoxelStreamingComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Async/ParallelFor.h"
#include <atomic>
#include "Voxel/Core/World/VoxelWorld.h"
#include "Voxel/Biomes/VoxelBiomeManager.h"
#include "Voxel/Core/VoxelChunk.h"
#include "Voxel/VoxelLogger.h"
#include "ProceduralMeshComponent.h"

UVoxelStreamingComponent::UVoxelStreamingComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UVoxelStreamingComponent::BeginPlay()
{
    Super::BeginPlay();
    WorldOwner = Cast<AVoxelWorld>(GetOwner());
}

void UVoxelStreamingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    AVoxelWorld* World = WorldOwner.Get();
    if (!World || !GetWorld()->IsGameWorld()) return;

    // Check if waiting for initial spawn — if so, streaming updates are paused
    if (World->IsWaitingForInitialSpawn()) return;

    StreamingTimer += DeltaTime;
    if (StreamingTimer >= StreamingInterval)
    {
        StreamingTimer = 0.f;
        UpdateStreaming();
    }

    CheckCloseRangeVisibility();
}

void UVoxelStreamingComponent::UpdateStreaming()
{
    AVoxelWorld* World = WorldOwner.Get();
    if (!World) return;

    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!Player) return;

    const FVector CurrentPos = Player->GetActorLocation();
    const float MinStep = World->ChunkSize * World->VoxelSize * 0.4f;
    if (FVector::DistSquared(CurrentPos, LastStreamedPos) < MinStep * MinStep)
        return;
    LastStreamedPos = CurrentPos;

    const FVector PlayerPos = CurrentPos;
    const FIntVector PlayerCoord = World->WorldToChunkCoord(PlayerPos);
    const FVoxelGenerationConfig Config = World->GetEffectiveConfig();
    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
    const float ChunkWorldSize = World->ChunkSize * World->VoxelSize;

    // 1. Sky Altitude
    CalculateSkyAltitude(PlayerPos, Config);

    // 2. Column Heights
    TArray<FVoxelBiomeManager::FWeightsAndHeight> CachedColumns;
    GatherColumnHeights(PlayerCoord, ChunkWorldSize, Config, CachedColumns);

    // 3. Island Thickness Math
    const float IslandSize    = FMath::Max(SC.BaseIslandSize, SC.BaseIslandSize + CachedCurvedH * SC.HeightSizeBonus + CachedCurvedR * SC.RoughnessSizeBonus);
    const float HalfThickCm   = FMath::Max(200.f, IslandSize * SC.ThicknessRatio);

    int32 SkyZMin = 0;
    int32 SkyZMax = 0;

    // 4. Build Desired Set
    TSet<FIntVector> Desired;
    BuildDesiredChunkSet(PlayerCoord, CachedColumns, ChunkWorldSize, CachedSkyAltWorld, HalfThickCm, SkyZMin, SkyZMax, Desired);

    // 5. Unload Out-of-Range
    TArray<FIntVector> ToRemove;
    const TMap<FIntVector, AVoxelChunk*>* LoadedChunks = World->GetLoadedChunks();
    for (auto& It : *LoadedChunks)
    {
        if (!Desired.Contains(It.Key))
            ToRemove.Add(It.Key);
    }
    for (const FIntVector& C : ToRemove)
        World->DestroyChunk(C);

    // 6. Self-healing sky chunks
    World->ClearEmptyChunksInRange(SkyZMin, SkyZMax);

    // 7. Update LODs
    UpdateLODs(PlayerPos, PlayerCoord, SkyZMin, SkyZMax, Desired);

    // 8. Sort Queue
    RebuildGenerationQueue(PlayerPos, Player->GetActorForwardVector(), Desired);
}


void UVoxelStreamingComponent::CheckCloseRangeVisibility()
{
    AVoxelWorld* World = WorldOwner.Get();
    if (!World || ChunksNeedingVisibilityCheck.IsEmpty()) return;

    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!Player) return;
    const FVector Pos = Player->GetActorLocation();
    const float Threshold = World->ChunkSize * World->VoxelSize * 3.0f;

    const TMap<FIntVector, AVoxelChunk*>* LoadedChunks = World->GetLoadedChunks();
    TArray<FIntVector> ToRemove;

    for (const FIntVector& Coord : ChunksNeedingVisibilityCheck)
    {
        AVoxelChunk*const* P = LoadedChunks->Find(Coord);
        if (!P || !*P) { ToRemove.Add(Coord); continue; }
        AVoxelChunk* Chunk = *P;
        if (FVector::Dist(Chunk->GetActorLocation(), Pos) >= Threshold) continue;
        if (Chunk->IsReady() && !Chunk->IsGenerating())
        {
            if (UProceduralMeshComponent* PM = Chunk->GetProceduralMesh())
                if (!PM->IsVisible()) PM->SetVisibility(true);
            Chunk->MarkMeshDirty(false);
            ToRemove.Add(Coord);
        }
    }
    for (const FIntVector& C : ToRemove) ChunksNeedingVisibilityCheck.Remove(C);
}

void UVoxelStreamingComponent::AddChunkNeedingVisibilityCheck(const FIntVector& Coord)
{
    ChunksNeedingVisibilityCheck.Add(Coord);
}

void UVoxelStreamingComponent::ClearVisibilityChecks()
{
    ChunksNeedingVisibilityCheck.Empty();
}

// ── Refactored Helpers ───────────────────────────────────────────────────────

void UVoxelStreamingComponent::CalculateSkyAltitude(const FVector& PlayerPos, const FVoxelGenerationConfig& Config)
{
    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
    if (FVector::DistSquared(PlayerPos, LastSkyAltPos) > 1000.f * 1000.f)
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
        const float DecoupledH = SH;
        CachedSkyAltWorld = DecoupledH + AltBase
            + ShardT * (CachedCurvedH * SC.HeightAltitudeBonus + CachedCurvedR * SC.RoughnessAltitudeBonus);
    }
}

void UVoxelStreamingComponent::GatherColumnHeights(const FIntVector& PlayerCoord, float ChunkWorldSize, const FVoxelGenerationConfig& Config, TArray<FVoxelBiomeManager::FWeightsAndHeight>& OutColumns)
{
    AVoxelWorld* World = WorldOwner.Get();
    if (!World) return;

    const int32 MaxRad  = World->DistantRenderDistanceXY;
    const int32 GridDim = 2 * MaxRad + 1;
    const int32 NumCols = GridDim * GridDim;

    const int32 SubStep   = 4;
    const int32 CoarseRad = FMath::Max(1, MaxRad / SubStep);
    const int32 CoarseDim = 2 * CoarseRad + 1;
    const int32 NumCoarse = CoarseDim * CoarseDim;

    TArray<FVoxelBiomeManager::FWeightsAndHeight> CoarseGrid;
    CoarseGrid.SetNumUninitialized(NumCoarse);

    ParallelFor(NumCoarse, [&](int32 Index)
    {
        const int32 cx = -CoarseRad + (Index % CoarseDim);
        const int32 cy = -CoarseRad + (Index / CoarseDim);
        const float ColX = (PlayerCoord.X + cx * SubStep + 0.5f) * ChunkWorldSize;
        const float ColY = (PlayerCoord.Y + cy * SubStep + 0.5f) * ChunkWorldSize;
        CoarseGrid[Index] = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(ColX, ColY, Config);
    });

    OutColumns.SetNumUninitialized(NumCols);

    ParallelFor(NumCols, [&](int32 Index)
    {
        const int32 x = -MaxRad + (Index % GridDim);
        const int32 y = -MaxRad + (Index / GridDim);
        const int32 cx = FMath::Clamp(FMath::RoundToInt((float)x / (float)SubStep), -CoarseRad, CoarseRad);
        const int32 cy = FMath::Clamp(FMath::RoundToInt((float)y / (float)SubStep), -CoarseRad, CoarseRad);
        const int32 CoarseIndex = (cx + CoarseRad) + (cy + CoarseRad) * CoarseDim;
        OutColumns[Index] = CoarseGrid[CoarseIndex];
    });
}

void UVoxelStreamingComponent::BuildDesiredChunkSet(const FIntVector& PlayerCoord, const TArray<FVoxelBiomeManager::FWeightsAndHeight>& CachedColumns, float ChunkWorldSize, float SkyAltWorld, float HalfThickCm, int32& OutSkyZMin, int32& OutSkyZMax, TSet<FIntVector>& OutDesired)
{
    AVoxelWorld* World = WorldOwner.Get();
    if (!World) return;

    const int32 MaxRad  = World->DistantRenderDistanceXY;
    const int32 GridDim = 2 * MaxRad + 1;
    const int32 NumCols = GridDim * GridDim;

    const int32 RenderDistanceXY = World->RenderDistanceXY;
    const int32 MidRenderDistanceXY = World->MidRenderDistanceXY;
    const int32 RenderDistanceZ = World->RenderDistanceZ;
    const int32 MidRenderDistanceZ = World->MidRenderDistanceZ;

    // ── Pre-pass or use Drafts? ──────────────────────────────────────────
    std::atomic<float> GlobalSkyAltMin(1000000.f);
    std::atomic<float> GlobalSkyAltMax(-1000000.f);
    const FVoxelGenerationConfig& Config = World->GetEffectiveConfig();
    const int32 SkylandsRenderDistanceXY = World->SkylandsRenderDistanceXY;

    ParallelFor(NumCols, [&](int32 Index)
    {
        const int32 x = -MaxRad + (Index % GridDim);
        const int32 y = -MaxRad + (Index / GridDim);
        if (x*x + y*y > SkylandsRenderDistanceXY*SkylandsRenderDistanceXY) return;

        const FVoxelBiomeManager::FWeightsAndHeight& Wh = CachedColumns[Index];
        float MinSkyAlt, MaxSkyAlt;
        FVoxelBiomeGenerators::GetSkylandAltitudeBounds(Wh.SurfaceHeight, Wh.Weights.GetRoughness(), Config, MinSkyAlt, MaxSkyAlt);

        float CurrentMin = GlobalSkyAltMin.load();
        while (MinSkyAlt < CurrentMin && !GlobalSkyAltMin.compare_exchange_weak(CurrentMin, MinSkyAlt));
        float CurrentMax = GlobalSkyAltMax.load();
        while (MaxSkyAlt > CurrentMax && !GlobalSkyAltMax.compare_exchange_weak(CurrentMax, MaxSkyAlt));
    });

    const float FilteredSkyMin = FMath::Min(SkyAltWorld - HalfThickCm*2.f, GlobalSkyAltMin.load());
    const float FilteredSkyMax = FMath::Max(SkyAltWorld + HalfThickCm*2.f, GlobalSkyAltMax.load());

    OutSkyZMin = FMath::FloorToInt(FilteredSkyMin / ChunkWorldSize);
    OutSkyZMax = FMath::CeilToInt (FilteredSkyMax / ChunkWorldSize);

    const int32 SkyZMin = OutSkyZMin;
    const int32 SkyZMax = OutSkyZMax;

    // ── Standard Terrain ─────────────────────────────────────────────────
    for (int32 Index = 0; Index < NumCols; ++Index)
    {
        const int32 x     = -MaxRad + (Index % GridDim);
        const int32 y     = -MaxRad + (Index / GridDim);
        const int32 radSq = x*x + y*y;
        const int32 GZ    = FMath::RoundToInt(CachedColumns[Index].SurfaceHeight / ChunkWorldSize);

        if (radSq <= RenderDistanceXY * RenderDistanceXY)
        {
            for (int32 z = -RenderDistanceZ; z <= 2; ++z)
                OutDesired.Add(FIntVector(PlayerCoord.X+x, PlayerCoord.Y+y, GZ+z));
        }
        else if (radSq <= MidRenderDistanceXY * MidRenderDistanceXY)
        {
            for (int32 z = -MidRenderDistanceZ; z <= MidRenderDistanceZ; ++z)
                OutDesired.Add(FIntVector(PlayerCoord.X+x, PlayerCoord.Y+y, GZ+z));
        }
        else if (radSq <= MaxRad * MaxRad)
        {
            OutDesired.Add(FIntVector(PlayerCoord.X+x, PlayerCoord.Y+y, GZ));
        }
    }

    for (int32 z = -2; z <= 2; ++z)
    for (int32 y = -RenderDistanceXY; y <= RenderDistanceXY; ++y)
    for (int32 x = -RenderDistanceXY; x <= RenderDistanceXY; ++x)
        if (x*x + y*y <= RenderDistanceXY*RenderDistanceXY)
            OutDesired.Add(PlayerCoord + FIntVector(x, y, z));

    // ── Skylands ─────────────────────────────────────────────────────────
    const int32 SkylandsRenderDistanceZ = World->SkylandsRenderDistanceZ;

    for (int32 z = SkyZMin; z <= SkyZMax + SkylandsRenderDistanceZ; ++z)
    for (int32 y = -SkylandsRenderDistanceXY; y <= SkylandsRenderDistanceXY; ++y)
    for (int32 x = -SkylandsRenderDistanceXY; x <= SkylandsRenderDistanceXY; ++x)
        if (x*x + y*y <= SkylandsRenderDistanceXY*SkylandsRenderDistanceXY)
            OutDesired.Add(FIntVector(PlayerCoord.X+x, PlayerCoord.Y+y, z));
}

void UVoxelStreamingComponent::UpdateLODs(const FVector& PlayerPos, const FIntVector& PlayerCoord, int32 SkyZMin, int32 SkyZMax, const TSet<FIntVector>& Desired)
{
    AVoxelWorld* World = WorldOwner.Get();
    if (!World) return;

    const TMap<FIntVector, AVoxelChunk*>* LoadedChunks = World->GetLoadedChunks();
    const int32 RenderDistanceXY = World->RenderDistanceXY;

    TMap<FIntVector, int32> DesiredLODs;
    DesiredLODs.Reserve(LoadedChunks->Num());

    for (auto& It : *LoadedChunks)
    {
        AVoxelChunk* Chunk = It.Value;
        if (!IsValid(Chunk)) continue;

        const FVector ChunkPos = World->ChunkCoordToWorld(It.Key) + FVector(World->ChunkSize * World->VoxelSize * 0.5f);
        const float   DistSq   = FVector::DistSquared(PlayerPos, ChunkPos);

        static constexpr float HOut = 1.10f, HIn = 0.90f;
        const float L1ISq = World->LOD1Distance * World->LOD1Distance * HIn * HIn;
        const float L1OSq = World->LOD1Distance * World->LOD1Distance * HOut * HOut;
        const float L2ISq = World->LOD2Distance * World->LOD2Distance * HIn * HIn;
        const float L2OSq = World->LOD2Distance * World->LOD2Distance * HOut * HOut;

        int32 LOD = Chunk->GetLOD();
        if      (LOD < 2 && DistSq > L2OSq) LOD = 2;
        else if (LOD > 1 && DistSq < L2ISq) LOD = 1;
        else if (LOD < 1 && DistSq > L1OSq) LOD = 1;
        else if (LOD > 0 && DistSq < L1ISq) LOD = 0;

        if (World->IsWaitingForInitialSpawn()) LOD = 0;

        const float SafeDistSq = (World->ChunkSize * World->VoxelSize * 3.2f) * (World->ChunkSize * World->VoxelSize * 3.2f);
        if (DistSq < SafeDistSq) LOD = 0;

        const int32 dx2 = FMath::Abs(It.Key.X - PlayerCoord.X);
        const int32 dy2 = FMath::Abs(It.Key.Y - PlayerCoord.Y);
        const int32 rSq = dx2*dx2 + dy2*dy2;
        const bool bIsSkylandZ = (It.Key.Z >= SkyZMin && It.Key.Z <= SkyZMax);
        
        if (bIsSkylandZ)
        {
            if (rSq > RenderDistanceXY * RenderDistanceXY) LOD = FMath::Max(LOD, 1);
            LOD = FMath::Min(LOD, 1);
        }
        else
        {
            if      (rSq > World->MidRenderDistanceXY * World->MidRenderDistanceXY) LOD = FMath::Max(LOD, 2);
            else if (rSq > RenderDistanceXY    * RenderDistanceXY)    LOD = FMath::Max(LOD, 1);
        }

        DesiredLODs.Add(It.Key, LOD);
    }

    // BFS Consistency
    {
        TArray<FIntVector> Queue; TSet<FIntVector> Visited;
        for (auto& It : DesiredLODs)
        {
            AVoxelChunk*const* CP = LoadedChunks->Find(It.Key);
            if (CP && IsValid(*CP) && It.Value != (*CP)->GetLOD())
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

    for (auto& It : *LoadedChunks)
    {
        AVoxelChunk* Chunk = It.Value;
        if (!IsValid(Chunk)) continue;
        const int32* FL = DesiredLODs.Find(It.Key);
        if (!FL || *FL == Chunk->GetLOD()) continue;
        if (Chunk->IsReady() && !Chunk->IsGenerating())
            Chunk->TransitionToLOD(*FL);
        else { Chunk->bPendingLODTransition = true; Chunk->PendingLOD = *FL; }
    }
}

void UVoxelStreamingComponent::RebuildGenerationQueue(const FVector& PlayerPos, const FVector& PlayerForward, const TSet<FIntVector>& Desired)
{
    AVoxelWorld* World = WorldOwner.Get();
    if (!World) return;

    const TMap<FIntVector, AVoxelChunk*>* LoadedChunks = World->GetLoadedChunks();

    TSet<FIntVector> Merged;
    const TArray<FIntVector>& WorldQueue = World->GetGenerationQueue();
    Merged.Reserve(Desired.Num() + (WorldQueue.Num() - World->GetQueueHead()));
    for (const FIntVector& C : Desired)
        if (!LoadedChunks->Contains(C) && !World->ContainsEmptyChunk(C)) Merged.Add(C);
    for (int32 i = World->GetQueueHead(); i < WorldQueue.Num(); ++i) Merged.Add(WorldQueue[i]);

    TArray<TPair<float,FIntVector>> Sorted;
    Sorted.Reserve(Merged.Num());

    for (const FIntVector& C : Merged)
    {
        const FVector ChunkWorld = World->ChunkCoordToWorld(C) + FVector(World->ChunkSize * World->VoxelSize * 0.5f);
        const float DistSq = FVector::DistSquared(PlayerPos, ChunkWorld);
        const float Dot = FVector::DotProduct(PlayerForward, (ChunkWorld - PlayerPos).GetSafeNormal());
        
        const float Key = DistSq / (1.0f + FMath::Max(0.f, Dot) * 2.0f);
        Sorted.Add({Key, C});
    }
    Sorted.Sort([](const TPair<float,FIntVector>& A, const TPair<float,FIntVector>& B){ return A.Key<B.Key; });

    TArray<FIntVector> NewQueue;
    NewQueue.Reserve(Sorted.Num());
    for (const auto& P : Sorted) NewQueue.Add(P.Value);
    World->SetGenerationQueue(NewQueue);
}
