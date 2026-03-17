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
#include "Voxel/Water/VoxelWaterTypes.h"
#include "Voxel/Water/VoxelWaterSimulator.h"
#include "VoxelChunk.generated.h"

class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;
struct FVoxelDataMap;
class FVoxelGeneratorTask;

class AVoxelChunk;

/**
 * AVoxelChunk represents a single cubic segment of the procedural world.
 * 
 * ARCHITECTURE OVERVIEW:
 * This class is the core building block of the voxel engine, responsible for:
 * 
 * 1. **Terrain Generation**: Dispatches background tasks to calculate density fields
 *    using the configured density generator (surface + skylands + caves)
 * 
 * 2. **Mesh Construction**: Converts density data into renderable ProceduralMeshComponent
 *    using Marching Cubes algorithm with material blending
 * 
 * 3. **LOD Management**: Supports multiple detail levels for performance optimization
 *    with smooth transitions between LOD levels
 * 
 * 4. **Foliage System**: Spawns trees, grass, and other vegetation based on biome
 *    configuration and terrain properties
 * 
 * 5. **Water Simulation**: Manages voxel water data and renders translucent water surfaces
 * 
 * 6. **Memory Management**: Implements chunk pooling for efficient memory usage
 * 
 * THREADING MODEL:
 * - Generation tasks run on background threads via FVoxelGeneratorTask
 * - Mesh upload and foliage spawning occur on GameThread
 * - Thread-safe state management prevents race conditions
 * 
 * PERFORMANCE FEATURES:
 * - Configurable chunk size and voxel resolution
 * - Multi-level LOD with smooth transitions
 * - Instanced Static Mesh Components for efficient foliage rendering
 * - Water mesh separation for optimal material blending
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

	/**
	 * Callback fired on the GameThread after ApplyMesh() with the list of water source world-voxel
	 * coordinates detected during generation.  AVoxelWorld binds this to register sources with
	 * FVoxelWaterSimulator.  Cleared after first call (sources are registered once per generation).
	 */
	TFunction<void(const TArray<FIntVector>&)> OnChunkWaterReady;

	void GenerateAsync();
	void CancelGeneration();
	void GenerateSync();
	void DestroyAndRebuildMesh();
	void ClearMesh();

	/**
	 * Rebuild the translucent water surface mesh from WaterData.
	 * Called by AVoxelWorld when the simulator marks this chunk's water dirty.
	 * Safe to call repeatedly; clears old water mesh sections first.
	 */
	void RebuildWaterMesh();

	/** Water voxel simulation state.  Populated in ApplyMesh(), updated by FVoxelWaterSimulator. */
	FVoxelWaterData WaterData;

	bool IsReady()      const { return bMeshApplied; }
	bool IsGenerating() const { return bGenerating;  }
	
	FORCEINLINE UProceduralMeshComponent* GetProceduralMesh() const { return ProceduralMesh; }

	/** Injected density evaluator. Falls back to an internal static instance when null. */
	struct FVoxelDensityGenerator* DensityGenerator = nullptr;

	/** Set to true when player edits have invalidated this chunk's mesh; rebuilt on the next Tick. */
	bool bMeshDirty = false;

	/** Water material — assigned by AVoxelWorld from GenerationConfig.Water.OceanMaterial. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Materials")
	UMaterialInterface* WaterMaterial = nullptr;

	/** Smoothly transition to a new LOD level. */
	UFUNCTION(BlueprintCallable, Category = "Voxel|LOD")
	void TransitionToLOD(int32 NewLOD);

	/** Current mesh state for managing LOD transitions and visibility. */
	enum class EChunkMeshState : uint8
	{
		Empty,           // No mesh data
		Generating,      // Background task running
		Ready,           // Mesh ready and visible
		Transitioning,   // In LOD transition
		Error            // Generation failed
	};

	/** Current mesh state for managing LOD transitions and visibility. */
	EChunkMeshState MeshState { EChunkMeshState::Empty };

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPROPERTY(VisibleAnywhere)
	UProceduralMeshComponent* ProceduralMesh;

	/** Separate translucent mesh component for voxel water surfaces. */
	UPROPERTY(VisibleAnywhere)
	UProceduralMeshComponent* WaterMesh;

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

	/** Previous mesh data for smooth transitions during LOD changes. */
	FVoxelMeshOutput PreviousMesh;

	/** Current mesh output for state management. */
	FVoxelMeshOutput MeshOutput;

	/** Target LOD for smooth transitions. */
	int32 TargetLOD { 0 };

	/** Transition progress (0.0 to 1.0) for smooth LOD blending. */
	float TransitionProgress { 0.0f };

	/** Time when transition started for timing-based blending. */
	float TransitionStartTime { 0.0f };

	/** Duration of LOD transitions in seconds. */
	static constexpr float TransitionDuration = 0.2f;

	void ApplyMesh(TSharedPtr<FVoxelGeneratorTask> CompletedTask);
	void UploadSection(int32 SectionIndex, const FVoxelMeshData& Data, UMaterialInterface* Mat);

	/** Build water surface mesh from WaterData. Internal — call RebuildWaterMesh() instead. */
	void BuildWaterMeshInternal();

	/** Update mesh state and handle transitions. */
	void UpdateMeshState();

	/** Blend between two mesh outputs for smooth transitions. */
	void BlendMeshes(const FVoxelMeshOutput& From, const FVoxelMeshOutput& To, float Alpha);

	/** Set mesh visibility while maintaining proper state. */
	void SetMeshVisibility(bool bVisible);
};
