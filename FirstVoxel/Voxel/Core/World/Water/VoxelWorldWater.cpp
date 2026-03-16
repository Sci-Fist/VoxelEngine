// VoxelWorldWater.cpp
// Implementation of UVoxelWorldWaterComponent managing water sim and sources.
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

void UVoxelWorldWaterComponent::Initialize(TUniquePtr<FVoxelWaterSimulator> InWaterSimulator, UVoxelWaterComponent* InOceanComponent)
{
    WaterSimulator = MoveTemp(InWaterSimulator);
    OceanComponent = InOceanComponent;

    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorldWaterComponent: Initialized with simulator=%s, OceanComponent=%s"),
           WaterSimulator ? TEXT("valid") : TEXT("null"),
           OceanComponent.IsValid() ? TEXT("valid") : TEXT("null"));
}

// ============================================================
//  Water Simulation Tick
// ============================================================

void UVoxelWorldWaterComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bWaterSimulationEnabled || !WaterSimulator.IsValid() || !GetWorld() || !GetWorld()->IsGameWorld())
        return;

    WaterSimTimer += DeltaTime;
    if (WaterSimTimer < WaterSimInterval)
        return;

    WaterSimTimer = 0.f;
    UpdateWaterSimulation(DeltaTime);
}

void UVoxelWorldWaterComponent::UpdateWaterSimulation(float DeltaTime)
{
    if (!WaterSimulator.IsValid()) return;

    // Run water simulation step
    // Step() returns coordinates of chunks whose water data changed.
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

    // Register the chunk with the water simulator
    WaterSimulator->RegisterChunk(ChunkCoord, &Chunk->WaterData);

    // Bind safe weak-lambda callback to feed runtime edits/sources into simulator
    TWeakObjectPtr<UVoxelWorldWaterComponent> WeakThis(this);
    Chunk->OnChunkWaterReady = [WeakThis](const TArray<FIntVector>& Sources)
    {
        if (UVoxelWorldWaterComponent* StrongThis = WeakThis.Get())
        {
            if (!StrongThis->WaterSimulator.IsValid()) return;
            for (const FIntVector& Src : Sources)
            {
                StrongThis->WaterSimulator->SetSource(Src);
            }
        }
    };

    // Process any pre-registered water sources for this chunk
    ProcessChunkWaterSources(Chunk, ChunkCoord);

    UE_LOG(LogVoxelWorld, Verbose, TEXT("VoxelWorldWaterComponent: Initialized water for chunk (%d,%d,%d)"),
           ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z);
}

void UVoxelWorldWaterComponent::RebuildWaterMeshForChunk(AVoxelChunk* Chunk)
{
    if (Chunk)
    {
        // Rebuild the water mesh for this chunk
        Chunk->RebuildWaterMesh();
    }
}

// ============================================================
//  Water Source Management
// ============================================================

void UVoxelWorldWaterComponent::RegisterWaterSource(const FIntVector& WorldVoxelCoord)
{
    WaterSources.AddUnique(WorldVoxelCoord);

    // If water simulator exists, register the source with it
    if (WaterSimulator.IsValid())
    {
        WaterSimulator->SetSource(WorldVoxelCoord);
    }
}

void UVoxelWorldWaterComponent::UnregisterWaterSource(const FIntVector& WorldVoxelCoord)
{
    WaterSources.Remove(WorldVoxelCoord);

    if (WaterSimulator.IsValid())
    {
        WaterSimulator->ClearCell(WorldVoxelCoord);
    }
}

void UVoxelWorldWaterComponent::ClearAllWaterSources()
{
    WaterSources.Empty();

    if (WaterSimulator.IsValid())
    {
        WaterSimulator->ClearAll();
    }
}

// ============================================================
//  Chunk Water Source Processing
// ============================================================

void UVoxelWorldWaterComponent::ProcessChunkWaterSources(AVoxelChunk* Chunk, const FIntVector& ChunkCoord)
{
    if (!Chunk) return;

    // Find water sources that belong to this chunk
    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(GetOwner());
    if (!VoxelWorld) return;

    const float ChunkWorldSize = VoxelWorld->ChunkSize * VoxelWorld->VoxelSize;
    const FVector ChunkOrigin = VoxelWorld->ChunkCoordToWorld(ChunkCoord);

    TArray<FIntVector> ChunkSources;

    for (const FIntVector& Source : WaterSources)
    {
        // Convert world voxel to chunk-local voxel coordinates
        FVector SourceWorldPos = VoxelWorld->ChunkCoordToWorld(Source);

        FVector LocalPos = SourceWorldPos - ChunkOrigin;

        // Check if source is within this chunk's bounds
        if (LocalPos.X >= 0 && LocalPos.X < ChunkWorldSize &&
            LocalPos.Y >= 0 && LocalPos.Y < ChunkWorldSize &&
            LocalPos.Z >= 0 && LocalPos.Z < ChunkWorldSize)
        {
            ChunkSources.Add(Source);
        }
    }

    if (ChunkSources.Num() > 0)
    {
        ChunkWaterSources.Add(ChunkCoord, ChunkSources);
    }
}

void UVoxelWorldWaterComponent::RemoveChunkFromWaterSimulation(const FIntVector& ChunkCoord)
{
    // Remove from chunk water sources tracking
    ChunkWaterSources.Remove(ChunkCoord);
}

bool UVoxelWorldWaterComponent::IsChunkWaterDirty(const FIntVector& ChunkCoord) const
{
    // For now, we consider a chunk's water dirty if it has registered sources
    return ChunkWaterSources.Contains(ChunkCoord);
}

// ============================================================
//  State Management
// ============================================================

void UVoxelWorldWaterComponent::ResetWaterState()
{
    ClearAllWaterSources();
    ChunkWaterSources.Empty();

    if (WaterSimulator.IsValid())
    {
        WaterSimulator->ClearAll();
    }
}

void UVoxelWorldWaterComponent::ClearChunkWaterData(const FIntVector& ChunkCoord)
{
    RemoveChunkFromWaterSimulation(ChunkCoord);
}
