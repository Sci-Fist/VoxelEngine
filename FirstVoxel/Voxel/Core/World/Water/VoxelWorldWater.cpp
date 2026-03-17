// VoxelWorldWater.cpp
// 
// Implementation of UVoxelWorldWaterComponent managing water simulation and sources.
// This component serves as the central coordinator for all water-related functionality
// in the voxel world, including simulation, source management, and chunk integration.
//
// ARCHITECTURE OVERVIEW:
// This component manages the global water simulation system, coordinating between
// the cellular automata water simulator, chunk-based water data, and the ocean
// surface rendering system. It provides a clean interface for water management
// while handling the complexity of distributed water simulation.
//
// KEY RESPONSIBILITIES:
// - Water simulation lifecycle management (tick, step, update)
// - Water source registration and management
// - Chunk water data coordination and mesh rebuilding
// - Integration with ocean surface rendering
// - Water state persistence and cleanup
//
// PERFORMANCE CHARACTERISTICS:
// - Throttled simulation updates to prevent excessive CPU usage
// - Efficient chunk dirty tracking for selective mesh updates
// - Weak pointer callbacks to prevent memory leaks
// - Batch processing of water sources for optimal performance

#include "Voxel/Core/World/Water/VoxelWorldWater.h"
#include "Voxel/Core/World/VoxelWorld.h"
#include "Voxel/Core/VoxelChunk.h"
#include "Voxel/Water/VoxelWaterSimulator.h"
#include "Voxel/Water/VoxelWaterComponent.h"
#include "Engine/World.h"
#include "Voxel/VoxelLogger.h"

UVoxelWorldWaterComponent::UVoxelWorldWaterComponent()
{
    // Configure component tick settings for water simulation
    PrimaryComponentTick.bCanEverTick = true;
    bTickInEditor = false;
}

void UVoxelWorldWaterComponent::Initialize(TUniquePtr<FVoxelWaterSimulator> InWaterSimulator, UVoxelWaterComponent* InOceanComponent)
{
    // Initialize water simulation system with provided components
    WaterSimulator = MoveTemp(InWaterSimulator);
    OceanComponent = InOceanComponent;

    // Log initialization status for debugging
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

    // Safety checks: ensure water simulation is enabled and components are valid
    if (!bWaterSimulationEnabled || !WaterSimulator.IsValid() || !GetWorld() || !GetWorld()->IsGameWorld())
        return;

    // Throttle water simulation updates to prevent excessive CPU usage
    WaterSimTimer += DeltaTime;
    if (WaterSimTimer < WaterSimInterval)
        return;

    // Reset timer and update water simulation
    WaterSimTimer = 0.f;
    UpdateWaterSimulation(DeltaTime);
}

void UVoxelWorldWaterComponent::UpdateWaterSimulation(float DeltaTime)
{
    // Safety check: ensure water simulator is valid
    if (!WaterSimulator.IsValid()) return;

    // Run water simulation step
    // Step() returns coordinates of chunks whose water data changed.
    TArray<FIntVector> DirtyChunks = WaterSimulator->Step();

    // Update water meshes for dirty chunks
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
    // Safety checks: ensure chunk and water simulator are valid
    if (!Chunk || !WaterSimulator.IsValid()) return;

    const FIntVector ChunkCoord = Chunk->ChunkCoord;

    // Register the chunk with the water simulator
    // This enables the simulator to track water data for this chunk
    WaterSimulator->RegisterChunk(ChunkCoord, &Chunk->WaterData);

    // Bind safe weak-lambda callback to feed runtime edits/sources into simulator
    // Using weak pointers prevents memory leaks and dangling references
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

    // Log chunk water initialization for debugging
    UE_LOG(LogVoxelWorld, Verbose, TEXT("VoxelWorldWaterComponent: Initialized water for chunk (%d,%d,%d)"),
           ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z);
}

void UVoxelWorldWaterComponent::RebuildWaterMeshForChunk(AVoxelChunk* Chunk)
{
    // Rebuild the water mesh for this chunk
    // This updates the visual representation of water in the chunk
    if (Chunk)
    {
        Chunk->RebuildWaterMesh();
    }
}

// ============================================================
//  Water Source Management
// ============================================================

void UVoxelWorldWaterComponent::RegisterWaterSource(const FIntVector& WorldVoxelCoord)
{
    // Add water source to tracking list
    WaterSources.AddUnique(WorldVoxelCoord);

    // If water simulator exists, register the source with it
    // This enables the cellular automata simulation to treat this as a water source
    if (WaterSimulator.IsValid())
    {
        WaterSimulator->SetSource(WorldVoxelCoord);
    }
}

void UVoxelWorldWaterComponent::UnregisterWaterSource(const FIntVector& WorldVoxelCoord)
{
    // Remove water source from tracking list
    WaterSources.Remove(WorldVoxelCoord);

    // Clear the source from the water simulator
    if (WaterSimulator.IsValid())
    {
        WaterSimulator->ClearCell(WorldVoxelCoord);
    }
}

void UVoxelWorldWaterComponent::ClearAllWaterSources()
{
    // Clear all registered water sources
    WaterSources.Empty();

    // Clear all sources from the water simulator
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
    // Safety check: ensure chunk is valid
    if (!Chunk) return;

    // Find water sources that belong to this chunk
    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(GetOwner());
    if (!VoxelWorld) return;

    // Calculate chunk boundaries for source filtering
    const float ChunkWorldSize = VoxelWorld->ChunkSize * VoxelWorld->VoxelSize;
    const FVector ChunkOrigin = VoxelWorld->ChunkCoordToWorld(ChunkCoord);

    TArray<FIntVector> ChunkSources;

    // Filter water sources to find those within this chunk's bounds
    for (const FIntVector& Source : WaterSources)
    {
        // Convert world voxel to world position
        FVector SourceWorldPos = VoxelWorld->ChunkCoordToWorld(Source);

        // Calculate local position relative to chunk origin
        FVector LocalPos = SourceWorldPos - ChunkOrigin;

        // Check if source is within this chunk's bounds
        if (LocalPos.X >= 0 && LocalPos.X < ChunkWorldSize &&
            LocalPos.Y >= 0 && LocalPos.Y < ChunkWorldSize &&
            LocalPos.Z >= 0 && LocalPos.Z < ChunkWorldSize)
        {
            ChunkSources.Add(Source);
        }
    }

    // Store chunk sources if any were found
    if (ChunkSources.Num() > 0)
    {
        ChunkWaterSources.Add(ChunkCoord, ChunkSources);
    }
}

void UVoxelWorldWaterComponent::RemoveChunkFromWaterSimulation(const FIntVector& ChunkCoord)
{
    // Remove chunk from water sources tracking
    // This cleans up memory when chunks are unloaded
    ChunkWaterSources.Remove(ChunkCoord);
}

bool UVoxelWorldWaterComponent::IsChunkWaterDirty(const FIntVector& ChunkCoord) const
{
    // Determine if chunk water data needs updating
    // For now, we consider a chunk's water dirty if it has registered sources
    return ChunkWaterSources.Contains(ChunkCoord);
}

// ============================================================
//  State Management
// ============================================================

void UVoxelWorldWaterComponent::ResetWaterState()
{
    // Reset all water simulation state
    // This is useful for world resets or configuration changes
    ClearAllWaterSources();
    ChunkWaterSources.Empty();

    if (WaterSimulator.IsValid())
    {
        WaterSimulator->ClearAll();
    }
}

void UVoxelWorldWaterComponent::ClearChunkWaterData(const FIntVector& ChunkCoord)
{
    // Clear water data for a specific chunk
    // This is called when chunks are unloaded or destroyed
    RemoveChunkFromWaterSimulation(ChunkCoord);
}
