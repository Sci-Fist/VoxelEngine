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
     * Performs GPU culling for a list of candidate chunk bounds.
     * Returns a bitmask or array of visible indices.
     */
    void PerformCulling_RenderThread(FRHICommandListImmediate& RHICmdList, const TArray<FBox>& Bounds, TArray<bool>& OutVisibility);

private:
    bool bInitialized = false;
};
