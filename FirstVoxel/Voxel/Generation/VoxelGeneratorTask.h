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

class FVoxelGeneratorTask
{
public:
    FVoxelGeneratorTask(
        const FIntVector&             InChunkCoord,
        const FVector&                InWorldOrigin,
        int32                         InChunkSize,
        float                         InVoxelSize,
        int32                         InStepSize,
        const FVoxelGenerationConfig& InConfig,
        IVoxelDensityProvider*        InProvider,
        float                         InFoliageDensity,
        float                         InMaxFoliageSlope,
        struct FVoxelDataMap*         InDataMap);

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

private:
    // Inputs
    FIntVector             ChunkCoord;
    FVector                WorldOrigin;
    int32                  ChunkSize;
    float                  VoxelSize;
    int32                  StepSize;
    FVoxelGenerationConfig Config;
    IVoxelDensityProvider* DensityProvider;
    float                  FoliageDensity;
    float                  MaxFoliageSlope;
    struct FVoxelDataMap*  DataMap;

    // Outputs
    FVoxelMeshOutput            MeshOutput;
    TArray<TArray<FTransform>>  PerFoliageTransforms;
    TArray<UStaticMesh*>        PerFoliageMeshes;
    TArray<FTransform>          LegacyTreeTransforms;
    TArray<FTransform>          LegacyGrassTransforms;

    struct FFoliageSlot { EVoxelBiome Biome; int32 EntryIdx; UStaticMesh* Mesh; };
    TArray<FFoliageSlot> FoliageSlots;
    bool bHasPerBiomeFoliage = false;

    TArray<float>                 Densities;
    TArray<FVoxelBiomeWeightMap>  ColumnWeights;
    TArray<float>                 ColumnSurfaceH;

    // FIX #5: flattened 1D array — single alloc, indexed [i*ChunkSize+j]
    TArray<FSkylandColumnCache>   SkylandColumnCaches;

    TArray<FIntVector>            WaterSources;

    void BuildDensityField();
    void PostProcessDensities(int32 TotalSamples);
    void BuildMesh();
    void CalculateFoliage();
    void ProcessLegacyFoliage(const FVector& Center, float SlopeZ,
                               const FVoxelBiomeWeightMap& W, const FVector& WorldCenter);
    void PlaceWaterSources();

    bool bIsFullSolid = false;
    bool bIsFullAir   = false;

    void CountDensityStates(int32 TotalSamples);   // no-op (folded into parallel loop)
    void TrimFoliageToCap  (const int32 Cap);

    FThreadSafeBool bCancelled{ false };
};
