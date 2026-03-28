// =============================================================================
// VoxelGeneratorTask.h
// FIX #5 — SkylandColumnCaches changed from TArray<TArray<FSkylandColumnCache>>
//           (2D, 33+ heap allocs per chunk) to TArray<FSkylandColumnCache>
//           (1D, single allocation). Indexed as [i * ChunkSize + j].
// =============================================================================
#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#include "Config/VoxelGenerationConfig.h"
#include "Generation/VoxelMeshGenerator.h"
#include "Generation/IVoxelDensityProvider.h"
#include "Math/Vector.h"
#include "Voxel/Core/VoxelDensityChunk.h"
#include "Math/IntVector.h"
#include "Biomes/VoxelBiomeGenerators.h"
#include "Biomes/VoxelBiomeManager.h"

class FVoxelGeneratorTask
{
public:
    FVoxelGeneratorTask(
        const FIntVector&             InChunkCoord,
        const FVector&                InWorldOrigin,
        const FVector&                InCameraPos,
        int32                         InChunkSize,
        float                         InVoxelSize,
        int32                         InStepSize,
        const FVoxelGenerationConfig& InConfig,
        IVoxelDensityProvider*        InProvider,
        float                         InFoliageDensity,
        float                         InMaxFoliageSlope,
        struct FVoxelDataMap*         InDataMap,
        uint32                        InFrameNumber,
        bool                          bIsDistantHeightmesh = false);

    ~FVoxelGeneratorTask();

    void Execute();
    void Cancel() { bCancelled = true; }
    bool IsCancelled() const { return bCancelled; }

    const FVoxelMeshOutput&           GetMeshOutput()           const { return MeshOutput;           }
    const TArray<TArray<FTransform>>& GetPerFoliageTransforms() const { return PerFoliageTransforms; }
    const TArray<UStaticMesh*>&       GetPerFoliageMeshes()     const { return PerFoliageMeshes;     }
    const TArray<FTransform>&         GetTreeTransforms()        const { return LegacyTreeTransforms; }
    const TArray<FTransform>&         GetGrassTransforms()       const { return LegacyGrassTransforms;}
    const TArray<FIntVector>&         GetWaterSources()          const { return WaterSources;         }
    const TArray<float>&              GetDensities()             const { return Densities;            }
    const FIntVector&                 GetChunkCoord()            const { return ChunkCoord;           }
    // OPT-4: pre-computed water column data (background thread) — consumed by ApplyMesh
    const TArray<float>& GetWaterColOceanWeights()   const { return ColScratch.WaterColOceanWeights;   }
    const TArray<float>& GetWaterColCraterWeights()  const { return ColScratch.WaterColCraterWeights;  }
    const TArray<float>& GetWaterColNeutralHeights() const { return ColScratch.WaterColNeutralHeights; }
    const TArray<float>& GetWaterColSurfaceHeights() const { return ColScratch.WaterColSurfaceHeights; }

private:
    // Inputs
    FIntVector             ChunkCoord;
    FVector                WorldOrigin;
    FVector                CameraPos;
    int32                  ChunkSize;
    float                  VoxelSize;
    int32                  StepSize;
    FVoxelGenerationConfig Config;
    IVoxelDensityProvider* DensityProvider;
    float                  FoliageDensity;
    float                  MaxFoliageSlope;
    struct FVoxelDataMap*  DataMap;
    uint32                 FrameNumber;
    bool                   bIsDistantHeightmesh;

    // Outputs
    FVoxelMeshOutput            MeshOutput;
    TArray<TArray<FTransform>>  PerFoliageTransforms;
    TArray<UStaticMesh*>        PerFoliageMeshes;
    TArray<FTransform>          LegacyTreeTransforms;
    TArray<FTransform>          LegacyGrassTransforms;

    struct FFoliageSlot { EVoxelBiome Biome; int32 EntryIdx; UStaticMesh* Mesh; };
    TArray<FFoliageSlot> FoliageSlots;
    bool bHasPerBiomeFoliage = false;

    // PERF-2: Column cache data promoted from BuildDensityField local → member,
    // so ComputeWaterColumns() can sample from it without re-evaluating noise.
    struct FColumnCacheItem
    {
        FVoxelBiomeWeightMap Weights;
        float SurfH    = 0.f;
        float NeutralH = 0.f;
        FSkylandColumnCache SkylandCache;
    };

    TArray<float>                 Densities;
    int32                         EffSize = 0;     // PERF-2: EffCS+3, set in BuildDensityField
    
public:
    struct FColumnScratchData
    {
        TArray<FVoxelBiomeWeightMap>  ColumnWeights;
        TArray<float>                 ColumnSurfaceH;
        TArray<FColumnCacheItem>      PrecalcColumns;
        TArray<float>                 WaterColOceanWeights;
        TArray<float>                 WaterColCraterWeights;
        TArray<float>                 WaterColNeutralHeights;
        TArray<float>                 WaterColSurfaceHeights;
    };
private:
    FColumnScratchData ColScratch;

    // FIX #5: flattened 1D array — single alloc, indexed [i*ChunkSize+j]
    TArray<FSkylandColumnCache>   SkylandColumnCaches;

    TArray<FIntVector>            WaterSources;

    FVoxelMeshScratchBuffers      ScratchBuffers;

    void BuildDensityField();
    void PostProcessDensities(int32 TotalSamples);
    void BuildMesh();
    void CalculateFoliage();
    void ComputeWaterColumns(); // OPT-4: pre-computes water biome data on background thread
    void ProcessLegacyFoliage(const FVector& Center, float SlopeZ,
                               const FVoxelBiomeWeightMap& W, const FVector& WorldCenter);
    void PlaceWaterSources();

    bool bIsFullSolid = false;
    bool bIsFullAir   = false;

    void CountDensityStates(int32 TotalSamples);   // no-op (folded into parallel loop)
    void TrimFoliageToCap  (const int32 Cap);

    FThreadSafeBool bCancelled{ false };
};
