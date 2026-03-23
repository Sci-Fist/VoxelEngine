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

    // ── Cached sky altitude ───────────────────────────────────────────────
    if (FVector::DistSquared(PlayerPos, LastSkyAltPos) > 1000.f * 1000.f) // SkyAltSnapDist = 1000.f
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

    const float SkyAltWorld = CachedSkyAltWorld;
    const float IslandSize  = FMath::Max(SC.BaseIslandSize,
                               SC.BaseIslandSize + CachedCurvedH*SC.HeightSizeBonus
                                                  + CachedCurvedR*SC.RoughnessSizeBonus);
    const float HalfThickCm = FMath::Max(200.f, IslandSize * SC.ThicknessRatio);

    // ── Sample ground heights for all columns (parallel) ──────────────────
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

    TArray<FVoxelBiomeManager::FWeightsAndHeight> CachedColumns;
    CachedColumns.SetNumUninitialized(NumCols);

    ParallelFor(NumCols, [&](int32 Index)
    {
        const int32 x = -MaxRad + (Index % GridDim);
        const int32 y = -MaxRad + (Index / GridDim);
        const int32 cx = FMath::Clamp(FMath::RoundToInt((float)x / (float)SubStep), -CoarseRad, CoarseRad);
        const int32 cy = FMath::Clamp(FMath::RoundToInt((float)y / (float)SubStep), -CoarseRad, CoarseRad);
        const int32 CoarseIndex = (cx + CoarseRad) + (cy + CoarseRad) * CoarseDim;
        CachedColumns[Index] = CoarseGrid[CoarseIndex];
    });

    // ── Build desired chunk set ───────────────────────────────────────────
    TSet<FIntVector> Desired;
    Desired.Reserve(NumCols * 3);

    const int32 RenderDistanceXY = World->RenderDistanceXY;
    const int32 MidRenderDistanceXY = World->MidRenderDistanceXY;
    const int32 RenderDistanceZ = World->RenderDistanceZ;
    const int32 MidRenderDistanceZ = World->MidRenderDistanceZ;

    for (int32 Index = 0; Index < NumCols; ++Index)
    {
        const int32 x     = -MaxRad + (Index % GridDim);
        const int32 y     = -MaxRad + (Index / GridDim);
        const int32 radSq = x*x + y*y;
        const int32 GZ    = FMath::RoundToInt(CachedColumns[Index].SurfaceHeight / ChunkWorldSize);

        if (radSq <= RenderDistanceXY * RenderDistanceXY)
        {
            for (int32 z = -RenderDistanceZ; z <= 2; ++z)
                Desired.Add(FIntVector(PlayerCoord.X+x, PlayerCoord.Y+y, GZ+z));
        }
        else if (radSq <= MidRenderDistanceXY * MidRenderDistanceXY)
        {
            for (int32 z = -MidRenderDistanceZ; z <= MidRenderDistanceZ; ++z)
                Desired.Add(FIntVector(PlayerCoord.X+x, PlayerCoord.Y+y, GZ+z));
        }
        else if (radSq <= MaxRad * MaxRad)
        {
            Desired.Add(FIntVector(PlayerCoord.X+x, PlayerCoord.Y+y, GZ - 1));
            Desired.Add(FIntVector(PlayerCoord.X+x, PlayerCoord.Y+y, GZ));
            Desired.Add(FIntVector(PlayerCoord.X+x, PlayerCoord.Y+y, GZ + 1));
        }
    }

    for (int32 z = -2; z <= 2; ++z)
    for (int32 y = -RenderDistanceXY; y <= RenderDistanceXY; ++y)
    for (int32 x = -RenderDistanceXY; x <= RenderDistanceXY; ++x)
        if (x*x + y*y <= RenderDistanceXY*RenderDistanceXY)
            Desired.Add(PlayerCoord + FIntVector(x, y, z));

    // Skylands pass
    std::atomic<float> GlobalSkyAltMin(1000000.f);
    std::atomic<float> GlobalSkyAltMax(-1000000.f);

    const int32 SkylandsRenderDistanceXY = World->SkylandsRenderDistanceXY;
    const int32 SkylandsRenderDistanceZ = World->SkylandsRenderDistanceZ;

    ParallelFor(NumCols, [&](int32 Index)
    {
        const int32 x = -MaxRad + (Index % GridDim);
        const int32 y = -MaxRad + (Index / GridDim);
        if (x*x + y*y > SkylandsRenderDistanceXY*SkylandsRenderDistanceXY) return;

        const float ColX = (PlayerCoord.X + x + 0.5f) * ChunkWorldSize;
        const float ColY = (PlayerCoord.Y + y + 0.5f) * ChunkWorldSize;
        const FVoxelBiomeManager::FWeightsAndHeight& Wh = CachedColumns[Index];

        const float CH     = Wh.SurfaceHeight;
        const float HN     = FMath::Clamp(CH/SC.MaxTerrainReference, 0.f, 1.f);
        const float RN     = FMath::Clamp(Wh.Weights.GetRoughness()/SC.RoughnessReference, 0.f, 1.f);
        const float TS     = FMath::Clamp(HN*1.5f + RN*0.8f, 0.f, 1.f);
        const float ShardT = FMath::SmoothStep(0.f, SC.ShardTransitionStrength, TS);
        const float AltBase = FMath::Lerp(SC.MinAltitudeAboveTerrain * 0.25f, SC.BaseAltitudeAboveTerrain, TS);
        
        float SkyAlt = CH + AltBase + ShardT*(FMath::Pow(HN,2.5f)*SC.HeightAltitudeBonus + FMath::Pow(RN,2.f)*SC.RoughnessAltitudeBonus);
        SkyAlt = FMath::Max(SkyAlt, CH + SC.MinAltitudeAboveTerrain + HalfThickCm);

        float CurrentMin = GlobalSkyAltMin.load();
        while (SkyAlt < CurrentMin && !GlobalSkyAltMin.compare_exchange_weak(CurrentMin, SkyAlt));
        float CurrentMax = GlobalSkyAltMax.load();
        while (SkyAlt > CurrentMax && !GlobalSkyAltMax.compare_exchange_weak(CurrentMax, SkyAlt));
    });

    const float FilteredSkyMin = FMath::Min(SkyAltWorld - HalfThickCm*2.f, GlobalSkyAltMin.load() - HalfThickCm*2.f);
    const float FilteredSkyMax = FMath::Max(SkyAltWorld + HalfThickCm*2.f, GlobalSkyAltMax.load() + HalfThickCm*2.f);

    const int32 SkyZMin = FMath::FloorToInt(FilteredSkyMin / ChunkWorldSize);
    const int32 SkyZMax = FMath::CeilToInt (FilteredSkyMax / ChunkWorldSize);

    for (int32 z = SkyZMin; z <= SkyZMax + SkylandsRenderDistanceZ; ++z)
    for (int32 y = -SkylandsRenderDistanceXY; y <= SkylandsRenderDistanceXY; ++y)
    for (int32 x = -SkylandsRenderDistanceXY; x <= SkylandsRenderDistanceXY; ++x)
        if (x*x + y*y <= SkylandsRenderDistanceXY*SkylandsRenderDistanceXY)
            Desired.Add(FIntVector(PlayerCoord.X+x, PlayerCoord.Y+y, z));

    // ── Destroy out-of-range chunks ───────────────────────────────────────
    const TMap<FIntVector, AVoxelChunk*>* LoadedChunks = World->GetLoadedChunks();
    TArray<FIntVector> ToRemove;
    for (auto& It : *LoadedChunks)
        if (!Desired.Contains(It.Key)) ToRemove.Add(It.Key);
    
    for (const FIntVector& C : ToRemove) 
    { 
        World->DestroyChunk(C); 
    }

    // ── PASS 1: Desired LOD per chunk ─────────────────────────────────────
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

        if (World->IsWaitingForInitialSpawn()) 
            LOD = 0;

        // Close-range always full detail
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

    // ── PASS 2: BFS LOD consistency ─────────────────────
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

    // ── Self-healing: clear sky coordinates from EmptyChunks ──
    TArray<FIntVector> EmptySkyToRemove;
    for (const FIntVector& C : World->EmptyChunks)
        if (C.Z >= SkyZMin && C.Z <= SkyZMax) EmptySkyToRemove.Add(C);
    for (const FIntVector& C : EmptySkyToRemove) World->EmptyChunks.Remove(C);

    // ── PASS 3: Apply transitions ─────────────────────────────────────────
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

    // ── Merge & sort generation queue ─────────────────────────
    TSet<FIntVector> Merged;
    Merged.Reserve(Desired.Num() + (World->GenerationQueue.Num() - World->QueueHead));
    for (const FIntVector& C : Desired)
        if (!LoadedChunks->Contains(C) && !World->EmptyChunks.Contains(C)) Merged.Add(C);
    for (int32 i = World->QueueHead; i < World->GenerationQueue.Num(); ++i) Merged.Add(World->GenerationQueue[i]);

    TArray<TPair<float,FIntVector>> Sorted;
    Sorted.Reserve(Merged.Num());

    const FVector Fwd = Player->GetActorForwardVector();

    for (const FIntVector& C : Merged)
    {
        const FVector ChunkWorld = World->ChunkCoordToWorld(C) + FVector(World->ChunkSize * World->VoxelSize * 0.5f);
        const float DistSq = FVector::DistSquared(PlayerPos, ChunkWorld);
        const float Dot = FVector::DotProduct(Fwd, (ChunkWorld - PlayerPos).GetSafeNormal());
        
        const float Key = DistSq / (1.0f + FMath::Max(0.f, Dot) * 2.0f);
        Sorted.Add({Key, C});
    }
    Sorted.Sort([](const TPair<float,FIntVector>& A, const TPair<float,FIntVector>& B){ return A.Key<B.Key; });

    World->GenerationQueue.Reset();
    World->GenerationQueue.Reserve(Sorted.Num());
    for (const auto& P : Sorted) World->GenerationQueue.Add(P.Value);
    World->QueueHead = 0;
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
