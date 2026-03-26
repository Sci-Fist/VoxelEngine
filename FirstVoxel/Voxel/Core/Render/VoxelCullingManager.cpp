// VoxelCullingManager.cpp
#include "VoxelCullingManager.h"
#include "RHI.h"
#include "RenderResource.h"
#include "ShaderParameterUtils.h"
#include "RHIStaticStates.h"
#include "RendererInterface.h"
#include "GlobalShader.h"

/** 
 * Shader implementation class for VoxelCulling.usf
 */
class FVoxelCullingCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FVoxelCullingCS);
    SHADER_USE_PARAMETER_STRUCT(FVoxelCullingCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_SRV(StructuredBuffer<FBox>, InputBounds)
        SHADER_PARAMETER_UAV(RWStructuredBuffer<uint32>, OutputVisibility)
        SHADER_PARAMETER(FMatrix, ViewProjectionMatrix)
        SHADER_PARAMETER(FVector, CameraPos)
        SHADER_PARAMETER(FVector4, HorizonPlane)
    END_SHADER_PARAMETER_STRUCT()

public:
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};

IMPLEMENT_GLOBAL_SHADER(FVoxelCullingCS, "/Voxel/VoxelCulling.usf", "MainCS", SF_Compute);

UVoxelCullingManager::UVoxelCullingManager()
{
}

void UVoxelCullingManager::Initialize()
{
    bInitialized = true;
}

void UVoxelCullingManager::Shutdown()
{
    bInitialized = false;
}

void UVoxelCullingManager::PerformCulling_RenderThread(FRHICommandListImmediate& RHICmdList, const TArray<FBox>& Bounds, TArray<bool>& OutVisibility)
{
    if (!bInitialized || Bounds.Num() == 0) return;

    int32 NumChunks = Bounds.Num();
    OutVisibility.SetNum(NumChunks);

    // 1. Create Input Buffer (Bounds)
    TResourceArray<FBox> BoundsData;
    BoundsData.Append(Bounds);
    
    FIntVector InputStride(sizeof(FBox), 0, 0);
    FStructuredBufferRHIRef InputBuffer = RHICreateStructuredBuffer(sizeof(FBox), BoundsData.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, BoundsData);
    FShaderResourceViewRHIRef InputSRV = RHICreateShaderResourceView(InputBuffer);

    // 2. Create Output Buffer (Visibility)
    FStructuredBufferRHIRef OutputBuffer = RHICreateStructuredBuffer(sizeof(uint32), NumChunks * sizeof(uint32), BUF_UnorderedAccess | BUF_SourceCopy, nullptr);
    FUnorderedAccessViewRHIRef OutputUAV = RHICreateUnorderedAccessView(OutputBuffer);

    // 3. Set Parameters & Dispatch
    TShaderMapRef<FVoxelCullingCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    RHICmdList.SetComputeShader(ComputeShader.GetComputeShader());

    FVoxelCullingCS::FParameters Params;
    Params.InputBounds = InputSRV;
    Params.OutputVisibility = OutputUAV;
    
    // Placeholder ViewProjection and Horizon Plane - in real use these come from FSceneView
    Params.ViewProjectionMatrix = FMatrix::Identity; 
    Params.CameraPos = FVector::ZeroVector;
    Params.HorizonPlane = FVector4(0, 0, 1, 0); // Flat ground plane

    SetShaderParameters(RHICmdList, ComputeShader, ComputeShader.GetComputeShader(), Params);

    uint32 GroupCount = FMath::DivideAndRoundUp((uint32)NumChunks, 64u);
    RHICmdList.DispatchComputeShader(GroupCount, 1, 1);

    // 4. Readback (Sync for now, should be async in production)
    uint32* Data = (uint32*)RHICmdList.LockStructuredBuffer(OutputBuffer, 0, NumChunks * sizeof(uint32), RLM_ReadOnly);
    for (int32 i = 0; i < NumChunks; ++i)
    {
        OutVisibility[i] = (Data[i] != 0);
    }
    RHICmdList.UnlockStructuredBuffer(OutputBuffer);
}
