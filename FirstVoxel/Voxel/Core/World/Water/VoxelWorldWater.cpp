// VoxelWorldWater.cpp
//
// FIX #32 — ProcessChunkWaterSources was calling ChunkCoordToWorld(Source)
//            where Source is a WORLD-VOXEL coord, not a chunk coord.
//            This produced positions 16× too large, so the bounds check
//            always failed and no per-chunk sources were ever registered.
//            Fix: convert world-voxel → world position as Source * VoxelSize.
//
// FIX #35 — WaterSources is now TSet<FIntVector> for O(1) Add/Remove.

#include "Voxel/Core/World/Water/VoxelWorldWater.h"
#include "Voxel/Core/World/VoxelWorld.h"
#include "Voxel/Core/VoxelChunk.h"
#include "Voxel/Water/VoxelWaterSimulator.h"
#include "Voxel/Water/VoxelWaterComponent.h"
#include "Engine/World.h"
#include "Voxel/VoxelLogger.h"

UVoxelWorldWaterComponent::UVoxelWorldWaterComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    bTickInEditor = false;
}

void UVoxelWorldWaterComponent::Initialize(TUniquePtr<FVoxelWaterSimulator> InSim,
                                            UVoxelWaterComponent*           InOcean)
{
    WaterSimulator = MoveTemp(InSim);
    OceanComponent = InOcean;
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorldWaterComponent: Initialized"));
}

void UVoxelWorldWaterComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                               FActorComponentTickFunction* Tick)
{
    Super::TickComponent(DeltaTime, TickType, Tick);
    if (!bWaterSimulationEnabled || !WaterSimulator.IsValid()
        || !GetWorld() || !GetWorld()->IsGameWorld()) return;

    WaterSimTimer += DeltaTime;
    if (WaterSimTimer < WaterSimInterval) return;
    WaterSimTimer = 0.f;
    UpdateWaterSimulation(DeltaTime);
}

void UVoxelWorldWaterComponent::UpdateWaterSimulation(float)
{
    if (!WaterSimulator.IsValid()) return;

    const TArray<FIntVector>& DirtyChunks = WaterSimulator->Step();

    if (AVoxelWorld* VW = Cast<AVoxelWorld>(GetOwner()))
    {
        const TMap<FIntVector, AVoxelChunk*>* LC = VW->GetLoadedChunks();
        if (LC)
        {
            for (const FIntVector& Coord : DirtyChunks)
            {
                if (AVoxelChunk* const* P = LC->Find(Coord))
                    if (*P && (*P)->WaterData.bMeshDirty)
                        RebuildWaterMeshForChunk(*P);
            }
        }
    }
}

void UVoxelWorldWaterComponent::InitChunkWater(AVoxelChunk* Chunk)
{
    if (!Chunk || !WaterSimulator.IsValid()) return;
    const FIntVector CC = Chunk->GetChunkCoord();
    WaterSimulator->RegisterChunk(CC, &Chunk->WaterData, Chunk->WaterGeneration);

    TWeakObjectPtr<UVoxelWorldWaterComponent> WeakThis(this);
    Chunk->OnChunkWaterReady = [WeakThis](const TArray<FIntVector>& Sources)
    {
        if (UVoxelWorldWaterComponent* S = WeakThis.Get())
            if (S->WaterSimulator.IsValid())
                for (const FIntVector& Src : Sources)
                    S->WaterSimulator->SetSource(Src);
    };

    ProcessChunkWaterSources(Chunk, CC);
}

void UVoxelWorldWaterComponent::RebuildWaterMeshForChunk(AVoxelChunk* Chunk)
{
    if (Chunk) Chunk->RebuildWaterMesh();
}

// FIX #35: TSet — O(1) Add with automatic deduplication
void UVoxelWorldWaterComponent::RegisterWaterSource(const FIntVector& WV)
{
    WaterSources.Add(WV);
    if (WaterSimulator.IsValid()) WaterSimulator->SetSource(WV);
}

void UVoxelWorldWaterComponent::UnregisterWaterSource(const FIntVector& WV)
{
    WaterSources.Remove(WV);
    if (WaterSimulator.IsValid()) WaterSimulator->ClearCell(WV);
}

void UVoxelWorldWaterComponent::ClearAllWaterSources()
{
    WaterSources.Empty();
    if (WaterSimulator.IsValid()) WaterSimulator->ClearAll();
}

// FIX #32: use VoxelSize to convert world-voxel coords → world positions
// Old: ChunkCoordToWorld(Source) treated Source as a chunk coord → 16× offset
// New: FVector(Source) * VoxelWorld->VoxelSize gives the actual world position
void UVoxelWorldWaterComponent::ProcessChunkWaterSources(AVoxelChunk* Chunk,
                                                           const FIntVector& CC)
{
    if (!Chunk) return;
    AVoxelWorld* VW = Cast<AVoxelWorld>(GetOwner());
    if (!VW) return;

    const float   ChunkWorldSize = VW->ChunkSize * VW->VoxelSize;
    const FVector ChunkOrigin    = VW->ChunkCoordToWorld(CC);

    TArray<FIntVector> ChunkSources;
    for (const FIntVector& Source : WaterSources)
    {
        // FIX #32: world-voxel → world position via VoxelSize multiplication
        const FVector SourceWorldPos = FVector(Source) * VW->VoxelSize;
        const FVector LocalPos       = SourceWorldPos - ChunkOrigin;
        if (LocalPos.X >= 0 && LocalPos.X < ChunkWorldSize &&
            LocalPos.Y >= 0 && LocalPos.Y < ChunkWorldSize &&
            LocalPos.Z >= 0 && LocalPos.Z < ChunkWorldSize)
        {
            ChunkSources.Add(Source);
        }
    }

    if (ChunkSources.Num() > 0)
        ChunkWaterSources.Add(CC, ChunkSources);
}

void UVoxelWorldWaterComponent::RemoveChunkFromWaterSimulation(const FIntVector& CC)
{
    ChunkWaterSources.Remove(CC);
}

bool UVoxelWorldWaterComponent::IsChunkWaterDirty(const FIntVector& CC) const
{
    return ChunkWaterSources.Contains(CC);
}

void UVoxelWorldWaterComponent::ResetWaterState()
{
    ClearAllWaterSources();
    ChunkWaterSources.Empty();
    if (WaterSimulator.IsValid()) WaterSimulator->ClearAll();
}

void UVoxelWorldWaterComponent::ClearChunkWaterData(const FIntVector& CC)
{
    RemoveChunkFromWaterSimulation(CC);
}
