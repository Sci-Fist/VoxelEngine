// VoxelWorldWater.cpp
//
// Water simulation and registration manager for AVoxelWorld.
//
// FIX APPLIED: InitChunkWater() now passes Chunk->WaterGeneration to
// RegisterChunk() so FVoxelWaterSimulator::Step() can detect stale entries
// if UnregisterChunk was not called before a chunk was recycled.

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

void UVoxelWorldWaterComponent::Initialize(
    TUniquePtr<FVoxelWaterSimulator> InWaterSimulator,
    UVoxelWaterComponent*            InOceanComponent)
{
    WaterSimulator = MoveTemp(InWaterSimulator);
    OceanComponent = InOceanComponent;

    UE_LOG(LogVoxelWorld, Log,
        TEXT("VoxelWorldWaterComponent: Initialized — simulator=%s, OceanComponent=%s"),
        WaterSimulator ? TEXT("valid") : TEXT("null"),
        OceanComponent.IsValid() ? TEXT("valid") : TEXT("null"));
}

// ============================================================
//  Tick
// ============================================================

void UVoxelWorldWaterComponent::TickComponent(
    float                        DeltaTime,
    ELevelTick                   TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bWaterSimulationEnabled || !WaterSimulator.IsValid()
        || !GetWorld() || !GetWorld()->IsGameWorld())
        return;

    WaterSimTimer += DeltaTime;
    if (WaterSimTimer < WaterSimInterval) return;
    WaterSimTimer = 0.f;

    UpdateWaterSimulation(DeltaTime);
}

void UVoxelWorldWaterComponent::UpdateWaterSimulation(float /*DeltaTime*/)
{
    if (!WaterSimulator.IsValid()) return;

    TArray<FIntVector> DirtyChunks = WaterSimulator->Step();

    if (AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(GetOwner()))
    {
        const TMap<FIntVector, AVoxelChunk*>* LoadedChunks = VoxelWorld->GetLoadedChunks();
        if (LoadedChunks)
        {
            for (const FIntVector& Coord : DirtyChunks)
            {
                if (AVoxelChunk* const* ChunkPtr = LoadedChunks->Find(Coord))
                {
                    if (AVoxelChunk* Chunk = *ChunkPtr)
                    {
                        if (Chunk->WaterData.bMeshDirty)
                            RebuildWaterMeshForChunk(Chunk);
                    }
                }
            }
        }
    }
}

// ============================================================
//  Chunk Water Management
// ============================================================

void UVoxelWorldWaterComponent::InitChunkWater(AVoxelChunk* Chunk)
{
    if (!Chunk || !WaterSimulator.IsValid()) return;

    const FIntVector ChunkCoord = Chunk->ChunkCoord;

    // FIX: Pass Chunk->WaterGeneration so the simulator can detect stale
    // entries if this chunk is recycled without an UnregisterChunk call.
    // The generation counter is incremented in AVoxelChunk::ClearMesh() and
    // AVoxelChunk::CancelGeneration() — any mismatch in Step() means the chunk
    // was returned to the pool and this registration is no longer valid.
    WaterSimulator->RegisterChunk(ChunkCoord, &Chunk->WaterData, Chunk->WaterGeneration);

    TWeakObjectPtr<UVoxelWorldWaterComponent> WeakThis(this);
    Chunk->OnChunkWaterReady = [WeakThis](const TArray<FIntVector>& Sources)
    {
        if (UVoxelWorldWaterComponent* StrongThis = WeakThis.Get())
        {
            if (!StrongThis->WaterSimulator.IsValid()) return;
            for (const FIntVector& Src : Sources)
                StrongThis->WaterSimulator->SetSource(Src);
        }
    };

    ProcessChunkWaterSources(Chunk, ChunkCoord);

    UE_LOG(LogVoxelWorld, Verbose,
        TEXT("VoxelWorldWaterComponent: Initialized water for chunk (%d,%d,%d) gen=%d"),
        ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, Chunk->WaterGeneration);
}

void UVoxelWorldWaterComponent::RebuildWaterMeshForChunk(AVoxelChunk* Chunk)
{
    if (Chunk) Chunk->RebuildWaterMesh();
}

// ============================================================
//  Water Source Management
// ============================================================

void UVoxelWorldWaterComponent::RegisterWaterSource(const FIntVector& WorldVoxelCoord)
{
    WaterSources.AddUnique(WorldVoxelCoord);
    if (WaterSimulator.IsValid()) WaterSimulator->SetSource(WorldVoxelCoord);
}

void UVoxelWorldWaterComponent::UnregisterWaterSource(const FIntVector& WorldVoxelCoord)
{
    WaterSources.Remove(WorldVoxelCoord);
    if (WaterSimulator.IsValid()) WaterSimulator->ClearCell(WorldVoxelCoord);
}

void UVoxelWorldWaterComponent::ClearAllWaterSources()
{
    WaterSources.Empty();
    if (WaterSimulator.IsValid()) WaterSimulator->ClearAll();
}

// ============================================================
//  Chunk Source Processing
// ============================================================

void UVoxelWorldWaterComponent::ProcessChunkWaterSources(
    AVoxelChunk*         Chunk,
    const FIntVector&    ChunkCoord)
{
    if (!Chunk) return;

    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(GetOwner());
    if (!VoxelWorld) return;

    const float ChunkWorldSize = VoxelWorld->ChunkSize * VoxelWorld->VoxelSize;
    const FVector ChunkOrigin  = VoxelWorld->ChunkCoordToWorld(ChunkCoord);

    TArray<FIntVector> ChunkSources;
    for (const FIntVector& Source : WaterSources)
    {
        FVector SourceWorldPos = VoxelWorld->ChunkCoordToWorld(Source);
        FVector LocalPos       = SourceWorldPos - ChunkOrigin;
        if (LocalPos.X >= 0 && LocalPos.X < ChunkWorldSize &&
            LocalPos.Y >= 0 && LocalPos.Y < ChunkWorldSize &&
            LocalPos.Z >= 0 && LocalPos.Z < ChunkWorldSize)
        {
            ChunkSources.Add(Source);
        }
    }

    if (ChunkSources.Num() > 0)
        ChunkWaterSources.Add(ChunkCoord, ChunkSources);
}

void UVoxelWorldWaterComponent::RemoveChunkFromWaterSimulation(const FIntVector& ChunkCoord)
{
    ChunkWaterSources.Remove(ChunkCoord);
}

bool UVoxelWorldWaterComponent::IsChunkWaterDirty(const FIntVector& ChunkCoord) const
{
    return ChunkWaterSources.Contains(ChunkCoord);
}

// ============================================================
//  State Management
// ============================================================

void UVoxelWorldWaterComponent::ResetWaterState()
{
    ClearAllWaterSources();
    ChunkWaterSources.Empty();
    if (WaterSimulator.IsValid()) WaterSimulator->ClearAll();
}

void UVoxelWorldWaterComponent::ClearChunkWaterData(const FIntVector& ChunkCoord)
{
    RemoveChunkFromWaterSimulation(ChunkCoord);
}
