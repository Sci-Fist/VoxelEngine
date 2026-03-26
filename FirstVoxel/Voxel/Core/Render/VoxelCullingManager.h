// VoxelCullingManager.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "GlobalShader.h"
#include "VoxelCullingManager.generated.h"

/** 
 * Manager for GPU-driven occlusion culling and horizon plane testing.
 * Uses Compute Shaders to evaluate chunk visibility against HZB.
 */
UCLASS()
class FIRSTVOXEL_API UVoxelCullingManager : public UObject
{
    GENERATED_BODY()

public:
    UVoxelCullingManager();

    void Initialize();
    void Shutdown();

    /** 
     * Schedules a GPU culling pass. Results will be available in 1-2 frames.
     * returns a RequestID that can be checked later.
     */
    int32 RequestCulling_RenderThread(
        FRHICommandListImmediate& RHICmdList, 
        const TArray<FBox>& Bounds, 
        const FMatrix& ViewProjection, 
        const FVector& CameraPos,
        const FRHITexture* HZBTexture = nullptr,
        const FRHITexture* SkyViewLUT = nullptr,
        const FRHITexture* TransmittanceLUT = nullptr);

    /** 
     * Retrieves results for a previous request. Returns true if ready.
     */
    bool GetResults(int32 RequestID, TArray<bool>& OutVisibility);

private:
    struct FCullingRequest
    {
        int32 RequestID;
        int32 NumChunks;
        TSharedPtr<FRHIGPUBufferReadback> Readback;
        TArray<bool> Results;
        bool bReady = false;
    };

    TArray<FCullingRequest> PendingRequests;
    TArray<TSharedPtr<FRHIGPUBufferReadback>> ReadbackPool;
    int32 NextRequestID = 1;

    bool bInitialized = false;

    TSharedPtr<FRHIGPUBufferReadback> GetOrCreateReadback();
};
