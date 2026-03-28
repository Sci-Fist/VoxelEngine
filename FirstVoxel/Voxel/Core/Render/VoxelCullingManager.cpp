// VoxelCullingManager.cpp
#include "VoxelCullingManager.h"
#include "RHIGPUReadback.h"
#include "RenderGraphUtils.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "RHICommandList.h"

UVoxelCullingManager::UVoxelCullingManager() {}
void UVoxelCullingManager::Initialize() { bInitialized = true; }
void UVoxelCullingManager::Shutdown() { ClearState(); bInitialized = false; }

int32 UVoxelCullingManager::RequestCulling_RenderThread(
    FRHICommandListImmediate& RHICmdList, 
    const TArray<FBox>& Bounds, 
    const FMatrix& ViewProjection, 
    const FVector& CameraPos,
    const FRHITexture* HZBTexture,
    const FRHITexture* SkyViewLUT,
    const FRHITexture* TransmittanceLUT)
{
    if (!bInitialized || Bounds.Num() == 0) return -1;
    
    int32 ReqID = NextRequestID++;
    
    const uint32 NumBytes = Bounds.Num() * sizeof(uint32);
    FRHIResourceCreateInfo CreateInfo(TEXT("VoxelCullingResults"));
    FBufferRHIRef ResultsBuffer = RHICmdList.CreateBuffer(NumBytes, BUF_UnorderedAccess | BUF_SourceCopy, 0, ERHIAccess::UAVMask, CreateInfo);

    // [Compute Shader Dispatch logic goes here...]
    // For now, bypass dispatch and set up the Readback API so the engine links correctly.

    // UE 5.7 GPU Readback Implementation
    FRHIGPUBufferReadback* Readback = new FRHIGPUBufferReadback(TEXT("VoxelCullingReadback"));
    Readback->EnqueueCopy(RHICmdList, ResultsBuffer, NumBytes);

    TArray<bool> InitialVis; 
    InitialVis.Init(true, Bounds.Num());
    
    FScopeLock Lock(&RequestsLock);
    PendingRequests.Add({ReqID, Bounds.Num(), InitialVis, true});
    
    // Memory Cleanup placeholder
    delete Readback;

    return ReqID;
}

bool UVoxelCullingManager::GetResults(int32 RequestID, TArray<bool>& OutVisibility)
{
    FScopeLock Lock(&RequestsLock);
    for (int32 i = 0; i < PendingRequests.Num(); ++i)
    {
        if (PendingRequests[i].RequestID == RequestID && PendingRequests[i].bReady)
        {
            OutVisibility = PendingRequests[i].Results;
            PendingRequests.RemoveAtSwap(i);
            return true;
        }
    }
    return false;
}

void UVoxelCullingManager::ClearState()
{
    FScopeLock Lock(&RequestsLock);
    PendingRequests.Empty();
}
