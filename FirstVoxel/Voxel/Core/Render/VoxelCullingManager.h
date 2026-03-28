// VoxelCullingManager.h
#pragma once

#include "CoreMinimal.h"
#include "VoxelCullingManager.generated.h"

/** 
 * Manager for GPU-driven occlusion culling and horizon plane testing.
 * Uses Compute Shaders to evaluate chunk visibility against HZB.
 * NOTE: GPU culling path disabled until RHIReadback API is updated for UE 5.7.
 */
UCLASS()
class FIRSTVOXEL_API UVoxelCullingManager : public UObject
{
    GENERATED_BODY()

public:
    UVoxelCullingManager();

    void Initialize();
    void Shutdown();

    int32 RequestCulling_RenderThread(
        FRHICommandListImmediate& RHICmdList, 
        const TArray<FBox>& Bounds, 
        const FMatrix& ViewProjection, 
        const FVector& CameraPos,
        const FRHITexture* HZBTexture = nullptr,
        const FRHITexture* SkyViewLUT = nullptr,
        const FRHITexture* TransmittanceLUT = nullptr);

    bool GetResults(int32 RequestID, TArray<bool>& OutVisibility);
    void ClearState();

private:
    struct FCullingRequest
    {
        int32 RequestID;
        int32 NumChunks;
        TArray<bool> Results;
        bool bReady = false;
    };

    TArray<FCullingRequest> PendingRequests;
    TAtomic<int32> NextRequestID{1};

    bool bInitialized = false;

    FCriticalSection RequestsLock;
};
