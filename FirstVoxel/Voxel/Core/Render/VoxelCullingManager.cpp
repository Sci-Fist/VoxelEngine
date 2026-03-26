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
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HZBTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, HZBSampler)
        SHADER_PARAMETER(FVector2D, HZBSize)
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
    // Legacy support: immediately request and wait (still stalls, but uses new infra)
    int32 ReqID = RequestCulling_RenderThread(RHICmdList, Bounds, FMatrix::Identity, FVector::ZeroVector);
    
    // This is the stall we want to avoid in the caller!
    while (!GetResults(ReqID, OutVisibility))
    {
        FPlatformProcess::Sleep(0.001f);
    }
}

int32 UVoxelCullingManager::RequestCulling_RenderThread(
    FRHICommandListImmediate& RHICmdList, 
    const TArray<FBox>& Bounds, 
    const FMatrix& ViewProjection, 
    const FVector& CameraPos,
    const FRHITexture* HZBTexture)
{
    if (!bInitialized || Bounds.Num() == 0) return -1;

    int32 RequestID = NextRequestID++;
    int32 NumChunks = Bounds.Num();

    // 1. Create Input Buffer (Bounds)
    TResourceArray<FBox> BoundsData;
    BoundsData.Append(Bounds);
    
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
    Params.ViewProjectionMatrix = ViewProjection;
    Params.CameraPos = CameraPos;
    Params.HorizonPlane = FVector4(0, 0, 1, 0); 

    if (HZBTexture)
    {
        Params.HZBTexture = (FRHITexture*)HZBTexture;
        Params.HZBSize = FVector2D(HZBTexture->GetSizeXYZ().X, HZBTexture->GetSizeXYZ().Y);
    }
    else
    {
        Params.HZBTexture = GBlackTexture->GetTextureRHI();
        Params.HZBSize = FVector2D(1.f, 1.f);
    }
    Params.HZBSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

    SetShaderParameters(RHICmdList, ComputeShader, ComputeShader.GetComputeShader(), Params);

    uint32 GroupCount = FMath::DivideAndRoundUp((uint32)NumChunks, 64u);
    RHICmdList.DispatchComputeShader(GroupCount, 1, 1);

    // 4. Copy to Readback
    RHICmdList.Transition(FRHITransitionInfo(OutputBuffer, ERHIAccess::UAVMask, ERHIAccess::CopySrc));
    
    TSharedPtr<FRHIGPUBufferReadback> Readback = GetOrCreateReadback();
    Readback->EnqueueCopy(RHICmdList, OutputBuffer);

    FCullingRequest Req;
    Req.RequestID = RequestID;
    Req.NumChunks = NumChunks;
    Req.Readback  = Readback;
    Req.Results.SetNumUninitialized(NumChunks);
    Req.bReady    = false;
    
    PendingRequests.Add(Req);

    return RequestID;
}

bool UVoxelCullingManager::GetResults(int32 RequestID, TArray<bool>& OutVisibility)
{
    for (int32 i = 0; i < PendingRequests.Num(); ++i)
    {
        if (PendingRequests[i].RequestID == RequestID)
        {
            FCullingRequest& Req = PendingRequests[i];
            
            if (Req.Readback->IsReady())
            {
                uint32* Data = (uint32*)Req.Readback->Lock(Req.NumChunks * sizeof(uint32));
                for (int32 j = 0; j < Req.NumChunks; ++j)
                {
                    Req.Results[j] = (Data[j] != 0);
                }
                Req.Readback->Unlock();
                
                OutVisibility = Req.Results;
                
                // Return readback buffer to pool
                ReadbackPool.Add(Req.Readback);
                PendingRequests.RemoveAt(i);
                return true;
            }
            
            return false;
        }
    }
    return false;
}

TSharedPtr<FRHIGPUBufferReadback> UVoxelCullingManager::GetOrCreateReadback()
{
    if (ReadbackPool.Num() > 0)
    {
        return ReadbackPool.Pop();
    }
    return MakeShared<FRHIGPUBufferReadback>(TEXT("VoxelCullingReadback"));
}
