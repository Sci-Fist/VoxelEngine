#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "Generation/VoxelMeshGenerator.h"
#include "Core/VoxelDataMap.h"
#include "Config/VoxelGenerationConfig.h"
#include "HAL/ThreadSafeBool.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "VoxelLogger.h"
#include "Generation/VoxelGeneratorTask.h"
#include "VoxelChunk.generated.h"

class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;
struct FVoxelDataMap;
class FVoxelGeneratorTask;

class AVoxelChunk;

/**
 * AVoxelChunk represents a single cubic segment of the procedural world.
 * It owns the ProceduralMeshComponent responsible for rendering the terrain
 * and dispatches background tasks (FVoxelGeneratorTask) to calculate the density field.
 * Once generation finishes on a worker thread, this class builds the final mesh
 * and spawns dynamic foliage instances on the GameThread.
 */
UCLASS()
class FIRSTVOXEL_API AVoxelChunk : public AActor
{
	GENERATED_BODY()

public:
	AVoxelChunk();

	/** Resolution of this chunk in voxels (e.g. 32x32x32). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Voxel|Internal")
	int32 ChunkSize = 32;

	/** Physical scale of a single voxel in world units (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Voxel|Internal")
	float VoxelSize = 100.f;

	/** Shared pointer to the world's flat terrain material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Materials")
	UMaterialInterface* MasterFlatMaterial = nullptr;

	/** Shared pointer to the world's slope/cliff material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Materials")
	UMaterialInterface* MasterSlopeMaterial = nullptr;

	/** Threshold for swapping between flat and slope mesh sections. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Materials", meta=(ClampMin="0.0", ClampMax="1.0"))
	float SlopeThreshold = 0.7f;

	FVoxelDataMap* DataMap = nullptr;

	/** Internal pointer to tree mesh. */
	UPROPERTY(BlueprintReadWrite, Category = "Voxel|Internal|Foliage")
	UStaticMesh* TreeMesh = nullptr;

	/** Internal pointer to grass mesh. */
	UPROPERTY(BlueprintReadWrite, Category = "Voxel|Internal|Foliage")
	UStaticMesh* GrassMesh = nullptr;

	/** Internal density multiplier. */
	UPROPERTY(BlueprintReadWrite, Category = "Voxel|Internal|Foliage")
	float FoliageDensity = 0.05f;

	/** Internal slope threshold. */
	UPROPERTY(BlueprintReadWrite, Category = "Voxel|Internal|Foliage")
	float MaxFoliageSlope = 0.8f;

	FVoxelGenerationConfig GenerationConfig;

	/** Level of Detail: 0=High, 1=Medium, 2=Low. Each step doubles the voxel sampling distance. */
	int32 LOD = 0;

	/** Returns the number of voxels to skip between samples for the current LOD level. */
	int32 GetStepSize() const { return 1 << LOD; }

	/** Grid coordinates of this chunk in the world chunk grid. */
	FIntVector ChunkCoord;

	/** Callback fired on the GameThread after mesh upload completes. Used by AVoxelWorld to decrement ActiveGenerations. */
	TFunction<void()> OnGenerationComplete;

	void GenerateAsync();
	void CancelGeneration();
	void GenerateSync();
	void DestroyAndRebuildMesh();
	void ClearMesh();

	bool IsReady()      const { return bMeshApplied; }
	bool IsGenerating() const { return bGenerating;  }

	/** Injected density evaluator. Falls back to an internal static instance when null. */
	struct FVoxelDensityGenerator* DensityGenerator = nullptr;

	/** Set to true when player edits have invalidated this chunk's mesh; rebuilt on the next Tick. */
	bool bMeshDirty = false;

protected:
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPROPERTY(VisibleAnywhere)
	UProceduralMeshComponent* ProceduralMesh;

	// Legacy fixed-slot HISM components (used when no per-biome foliage is configured)
	UPROPERTY(VisibleAnywhere)
	UInstancedStaticMeshComponent* TreeHISM;

	UPROPERTY(VisibleAnywhere)
	UInstancedStaticMeshComponent* GrassHISM;

	/**
	 * Dynamically created HISM components, one per active foliage slot.
	 */
	UPROPERTY()
	/** Dynamically grown HISM pool — one slot per active per-biome foliage entry. */
	TArray<UInstancedStaticMeshComponent*> BiomeFoliageHISMs;

	/** The running background task. Kept alive so we can cancel it on demand. */
	TSharedPtr<FVoxelGeneratorTask> CurrentTask;

	/** True once a mesh has been successfully applied and is visible. */
	FThreadSafeBool bMeshApplied { false };

	/** True while an async generation task is in flight. */
	FThreadSafeBool bGenerating  { false };

	/** Incremented each time a new task is launched; stale task completions are silently discarded. */
	TAtomic<uint32> GenerationId { 0 };

	void ApplyMesh(TSharedPtr<FVoxelGeneratorTask> CompletedTask);
	void UploadSection(int32 SectionIndex, const FVoxelMeshData& Data, UMaterialInterface* Mat);
};
