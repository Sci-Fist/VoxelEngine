// VoxelWorld_Streaming.cpp — Delegated to VoxelStreamingComponent
#include "Voxel/Core/World/VoxelWorld.h"
#include "Voxel/Core/World/Streaming/VoxelStreamingComponent.h"
#include "Engine/World.h"

void AVoxelWorld::UpdateChunkStreaming()
{
    if (StreamingComponent)
    {
        StreamingComponent->UpdateStreaming();
    }
}

void AVoxelWorld::CheckCloseRangeVisibility()
{
    if (StreamingComponent)
    {
        StreamingComponent->CheckCloseRangeVisibility();
    }
}
