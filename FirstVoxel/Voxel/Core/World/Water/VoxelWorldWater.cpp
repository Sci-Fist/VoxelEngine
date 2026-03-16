// VoxelWorldWater.cpp
// Implementation of water simulation and rendering management for AVoxelWorld.
// This class handles water simulation, component management, and water mesh updates.

#include "Voxel/Core/World/Water/VoxelWorldWater.h"
#include "Voxel/Core/World/VoxelWorld.h"
#include "Voxel/Core/VoxelChunk.h"
#include "Voxel/Water/VoxelWaterSimulator.h"
#include "Voxel/Water/VoxelWaterComponent.h"
#include "Engine/World.h"
#include "Voxel/VoxelLogger.h"


FVoxelWorldWater::FVoxelWorldWater(AActor* InWorldOwner)
    : WorldOwner(InWorldOwner)
{
}

void FVoxelWorldWater::Initialize(class FVoxelWaterSimulator* InWaterSimulator, UVoxelWaterComponent* InWaterComponent)

{
    WaterSimulator = InWaterSimulator;
    WaterComponent = InWaterComponent;
    
    
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorldWater: Initialized with simulator=%s, component=%s"),
           WaterSimulator ? TEXT("valid") : TEXT("null"),
           WaterComponent.IsValid() ? TEXT("valid") : TEXT("null"));

}

// ============================================================
//  Water Simulation Tick
// ============================================================

void FVoxelWorldWater::TickWater(float DeltaTime)
{
    if (!bWaterSimulationEnabled || !WaterSimulator || !WorldOwner || !WorldOwner->GetWorld())
        return;

    WaterSimTimer += DeltaTime;
    if (WaterSimTimer < WaterSimInterval)
        return;
    
    WaterSimTimer = 0.f;
    UpdateWaterSimulation(DeltaTime);
}

void FVoxelWorldWater::UpdateWaterSimulation(float DeltaTime)
{
    if (!WaterSimulator || !WorldOwner) return;

    
    // Run water simulation step
    // Step() returns coordinates of chunks whose water data changed.
    TArray<FIntVector> DirtyChunks = WaterSimulator->Step();
    
    class AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(WorldOwner);
    if (VoxelWorld)
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
                        RebuildWaterMeshForChunk(Chunk);
                    }
                }
            }
        }
    }

    // Log simulation step (verbose level)
    LogWaterSimulationStep();
}


// ============================================================
//  Chunk Water Management
// ============================================================

void FVoxelWorldWater::InitChunkWater(AVoxelChunk* Chunk)
{
    if (!Chunk || !WaterSimulator) return;
    
    const FIntVector ChunkCoord = Chunk->ChunkCoord;
    
    // Register the chunk with the water simulator
    WaterSimulator->RegisterChunk(ChunkCoord, &Chunk->WaterData);

    
    // Process any pre-registered water sources for this chunk
    ProcessChunkWaterSources(Chunk, ChunkCoord);
    
    LogChunkWaterInit(ChunkCoord);
}

void FVoxelWorldWater::RebuildWaterMeshForChunk(AVoxelChunk* Chunk)
{
    if (!Chunk) return;
    
    // Rebuild the water mesh for this chunk
    Chunk->RebuildWaterMesh();
    
    UE_LOG(LogVoxelWorld, Verbose, TEXT("VoxelWorldWater: Rebuilt water mesh for chunk (%d,%d,%d)"),
           Chunk->ChunkCoord.X, Chunk->ChunkCoord.Y, Chunk->ChunkCoord.Z);
}

// ============================================================
//  Water Source Management
// ============================================================

void FVoxelWorldWater::RegisterWaterSource(const FIntVector& WorldVoxelCoord)
{
    WaterSources.Add(WorldVoxelCoord);
    LogWaterSourceRegistration(WorldVoxelCoord);
    
    // If water simulator exists, register the source with it
    if (WaterSimulator)
    {
        WaterSimulator->SetSource(WorldVoxelCoord);

    }
}

void FVoxelWorldWater::UnregisterWaterSource(const FIntVector& WorldVoxelCoord)
{
    WaterSources.Remove(WorldVoxelCoord);
    
    if (WaterSimulator)
    {
        WaterSimulator->ClearCell(WorldVoxelCoord);

    }
}

void FVoxelWorldWater::ClearAllWaterSources()
{
    WaterSources.Empty();
    
    if (WaterSimulator)
    {
        WaterSimulator->ClearAll();

    }
    
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorldWater: Cleared all water sources"));
}

// ============================================================
//  Chunk Water Source Processing
// ============================================================

void FVoxelWorldWater::ProcessChunkWaterSources(AVoxelChunk* Chunk, const FIntVector& ChunkCoord)
{
    if (!Chunk) return;
    
    // Find water sources that belong to this chunk
    class AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(WorldOwner);
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
        UE_LOG(LogVoxelWorld, Verbose, TEXT("VoxelWorldWater: Chunk (%d,%d,%d) has %d water sources"),
               ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, ChunkSources.Num());
    }
}

void FVoxelWorldWater::RemoveChunkFromWaterSimulation(const FIntVector& ChunkCoord)
{
    // Remove from chunk water sources tracking
    ChunkWaterSources.Remove(ChunkCoord);
    
    // Note: The water simulator maintains its own internal state and will
    // continue to simulate water that flows across chunk boundaries.
    // Chunk removal is handled by the simulator's internal cleanup.
}

bool FVoxelWorldWater::IsChunkWaterDirty(const FIntVector& ChunkCoord) const
{
    // For now, we consider a chunk's water dirty if it has registered sources
    // In a more sophisticated system, we'd track actual water state changes
    return ChunkWaterSources.Contains(ChunkCoord);
}

// ============================================================
//  State Management
// ============================================================

void FVoxelWorldWater::ResetWaterState()
{
    ClearAllWaterSources();
    ChunkWaterSources.Empty();
    
    if (WaterSimulator)
    {
        WaterSimulator->ClearAll();

    }
    
    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorldWater: Water state reset"));
}

void FVoxelWorldWater::ClearChunkWaterData(const FIntVector& ChunkCoord)
{
    RemoveChunkFromWaterSimulation(ChunkCoord);
    
    UE_LOG(LogVoxelWorld, Verbose, TEXT("VoxelWorldWater: Cleared water data for chunk (%d,%d,%d)"),
           ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z);
}

// ============================================================
//  Logging Helpers
// ============================================================

void FVoxelWorldWater::LogWaterSimulationStep() const
{
    // Only log at verbose level to avoid spam
    UE_LOG(LogVoxelWorld, Verbose, TEXT("VoxelWorldWater: Simulation step completed"));
}

void FVoxelWorldWater::LogChunkWaterInit(const FIntVector& ChunkCoord) const
{
    UE_LOG(LogVoxelWorld, Verbose, TEXT("VoxelWorldWater: Initialized water for chunk (%d,%d,%d)"),
           ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z);
}

void FVoxelWorldWater::LogWaterSourceRegistration(const FIntVector& WorldVoxelCoord) const
{
    UE_LOG(LogVoxelWorld, Verbose, TEXT("VoxelWorldWater: Registered water source at (%d,%d,%d)"),
           WorldVoxelCoord.X, WorldVoxelCoord.Y, WorldVoxelCoord.Z);
}
