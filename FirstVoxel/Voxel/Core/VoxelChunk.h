/**
 * @file VoxelChunk.h
 * @brief Core chunk class for procedural voxel terrain generation
 *
 * AVoxelChunk represents a single cubic section of the procedural world.
 * It manages density field generation, mesh building, foliage placement,
 * and water simulation for its volume. Chunks are pooled and reused across
 * the session for optimal performance.
 *
 * ARCHITECTURE OVERVIEW:
 * - Density Generation: Background thread task builds density field
 * - Mesh Generation: Surface Nets algorithm converts density to mesh
 * - Foliage System: Per-biome instanced static mesh components
 * - Water Simulation: Translucent water surface mesh with physics
 * - LOD System: Smooth transitions between detail levels
 *
 * DEPENDENCIES:
 * - VoxelMeshGenerator: Converts density to mesh geometry
 * - VoxelDataMap: Stores voxel density data
 * - VoxelGenerationConfig: World generation parameters
 * - VoxelWaterSimulator: Water physics and rendering
 * - VoxelChunkPool: Object pooling for performance
 */

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

// =============================================================================
// AVoxelChunk
// =============================================================================
//
// One cubic section of the procedural world. Owned by AVoxelWorld via the
// chunk pool (FVoxelChunkPool). Created once and reused across the session.
//
// -- RESPONSIBILITIES ---------------------------------------------------------
//
//  Generation    Fire FVoxelGeneratorTask on a background thread to build
//                density field, mesh, foliage, and water sources.
//
//  Mesh upload   ApplyMesh() runs on the game thread after the task finishes:
//                uploads geometry to ProceduralMeshComponent, creates or
//                refreshes BiomeFoliageHISMs, builds FVoxelWaterData.
//
//  LOD           Tick() calls UpdateMeshState() each frame while transitioning.
//                TransitionToLOD() stores current mesh as PreviousMesh and
//                fires GenerateAsync() for the new step size.
//
//  Water surface RebuildWaterMesh() rebuilds WaterMesh from FVoxelWaterData.
//                Called by UVoxelWorldWaterComponent when the simulator marks
//                this chunk dirty.
//
// -- STATE MACHINE (EChunkMeshState) ------------------------------------------
//
//   Empty  -->  Generating  -->  Ready  -->  Transitioning
//          GenerateAsync()    ApplyMesh()   TransitionToLOD()
//                                              |          |
//                                         Generating   Ready
//
// -- CALLBACK PROTOCOL --------------------------------------------------------
//
//  OnGenerationComplete  -- fired by ApplyMesh() AND CancelGeneration().
//                           Always called exactly once per GenerateAsync().
//                           AVoxelWorld uses this to decrement ActiveGenerations.
//                           Uses MoveTemp+null-before-call to prevent double-fire.
//
//  OnChunkWaterReady     -- fired once by ApplyMesh() with the water source
//                           world-voxel list. AVoxelWorld registers sources
//                           with FVoxelWaterSimulator.
//
// -- GENERATION ID GUARD ------------------------------------------------------
//
//  GenerationId (TAtomic<uint32>) is incremented each time GenerateAsync() is
//  called. The background task captures its ID at launch; the game-thread
//  callback discards the result if the ID has changed (chunk was cancelled and
//  restarted). This prevents stale mesh uploads from recycled chunks.
//
// -- WATER GENERATION COUNTER -------------------------------------------------
//
//  WaterGeneration is incremented in ClearMesh() and CancelGeneration() so
//  FVoxelWaterSimulator can detect stale registrations if UnregisterChunk was
//  not called before the chunk was recycled. Always read and written on the
//  game thread.
//
// -- FOLIAGE COMPONENTS -------------------------------------------------------
//
//  Per-biome foliage uses BiomeFoliageHISMs[], a dynamically grown pool of
//  UInstancedStaticMeshComponent. On ClearMesh() instances are cleared but
//  components are kept alive (ReturnChunk pool reuse pattern).
//  Legacy TreeHISM / GrassHISM are populated only when no per-biome foliage
//  entries are configured.
// =============================================================================
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

	/** Threshold (dot product) at which flat material blends into slope cliff material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Materials")
	float SlopeThreshold = 0.7f;

	FVoxelDataMap* GetDataMap() const { return DataMap; }
	void SetDataMap(struct FVoxelDataMap* InMap) { DataMap = InMap; }

	/** Shared data node cache for dense density float grids. */
	TSharedPtr<struct FVoxelDensityChunk> DenseChunk;

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

	FORCEINLINE const FVoxelGenerationConfig& GetGenerationConfig() const { return GenerationConfig; }
	void SetGenerationConfig(const FVoxelGenerationConfig& InConfig) { GenerationConfig = InConfig; }

	/** Level of Detail: 0=High, 1=Medium, 2=Low. Each step doubles the voxel sampling distance. */
	int32 GetLOD() const { return LOD; }
	void SetLOD(int32 NewLOD) { LOD = NewLOD; }

	/** Returns the number of voxels to skip between samples for the current LOD level. */
	int32 GetStepSize() const { return 1 << LOD; }

	/** Grid coordinates of this chunk in the world chunk grid. */
	FORCEINLINE const FIntVector& GetChunkCoord() const { return ChunkCoord; }
	FORCEINLINE void SetChunkCoord(const FIntVector& InCoord) { ChunkCoord = InCoord; }

	/** Callback fired on the GameThread after mesh upload completes. Used by AVoxelWorld to decrement ActiveGenerations. */
	TFunction<void()> OnGenerationComplete;

	/**
	 * Callback fired on the GameThread after ApplyMesh() with the list of water source world-voxel
	 * coordinates detected during generation. AVoxelWorld binds this to register sources with
	 * FVoxelWaterSimulator. Cleared after first call (sources are registered once per generation).
	 */
	TFunction<void(const TArray<FIntVector>&)> OnChunkWaterReady;

	void GenerateAsync();
	void CancelGeneration();
	void Reset();
	void GenerateSync();
	void DestroyAndRebuildMesh();
	void ClearMesh();

	/**
	 * Rebuild the translucent water surface mesh from WaterData.
	 * Called by AVoxelWorld when the simulator marks this chunk's water dirty.
	 * Safe to call repeatedly; clears old water mesh sections first.
	 */
	void RebuildWaterMesh();

	/** Water voxel simulation state. Populated in ApplyMesh(), updated by FVoxelWaterSimulator. */
	FORCEINLINE const FVoxelWaterData& GetWaterData() const { return WaterData; }
	FORCEINLINE FVoxelWaterData& GetWaterData() { return WaterData; }

	/**
	 * Generational counter for water simulator safety.
	 * Incremented in ClearMesh() and CancelGeneration() so FVoxelWaterSimulator
	 * can detect stale registrations if UnregisterChunk was not called before
	 * the chunk was recycled. Read by UVoxelWorldWaterComponent::InitChunkWater().
	 * Game-thread only — no synchronization needed.
	 */
	int32 WaterGeneration = 0;
	
	/**
	 * Generational counter for water mesh stability.
	 * Stores the CRC32 hash of the last successfully built water mesh
	 * to avoid redundant GPU buffer updates and clearing.
	 */
	uint32 LastWaterMeshHash = 0;
	
	/** Warning flags to avoid duplicate logs. Reset in ClearMesh() for pool safety. */
	bool bFlatMaterialWarned = false;
	bool bSlopeMaterialWarned = false;

	bool IsReady()          const { return bMeshApplied; }
	bool IsGenerating()     const { return bGenerating;  }

	/**
	 * Returns true when the physics collision body is fully cooked and ready.
	 * This is STRONGER than IsReady() — IsReady() fires when mesh data is uploaded
	 * but async collision cooking may still be in-flight. The spawn hover-lock
	 * must wait for this before releasing the player, otherwise the character
	 * falls through terrain that has no physics body yet.
	 */
	bool IsCollisionReady() const
	{
		if (!bMeshApplied || !IsValid(ProceduralMesh)) return false;

		// If the mesh is empty (full air/solid), there is no collision to cook.
		if (MeshOutput.FlatMesh.Vertices.Num() == 0 && MeshOutput.SlopeMesh.Vertices.Num() == 0)
			return true;

		return ProceduralMesh->GetBodyInstance() != nullptr
			&& ProceduralMesh->GetBodyInstance()->IsValidBodyInstance();
	}

	bool IsEmpty() const
	{
		return MeshOutput.FlatMesh.Vertices.Num() == 0 && 
		       MeshOutput.SlopeMesh.Vertices.Num() == 0 && 
		       !WaterData.HasAnyWater();
	}
	
	FORCEINLINE UProceduralMeshComponent* GetProceduralMesh() const { return ProceduralMesh; }

	/** Injected density evaluator. Falls back to an internal static instance when null. */
	struct FVoxelDensityGenerator* GetDensityGenerator() const { return DensityGenerator; }
	void SetDensityGenerator(struct FVoxelDensityGenerator* InGen) { DensityGenerator = InGen; }

	/** Set to true when player edits have invalidated this chunk's mesh; rebuilt on the next Tick. */
	bool IsMeshDirty() const { return bMeshDirty; }
	void MarkMeshDirty(bool bDirty) { bMeshDirty = bDirty; }

	/** Water material — assigned by AVoxelWorld from GenerationConfig.Water.OceanMaterial. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Materials")
	UMaterialInterface* WaterMaterial = nullptr;

	/** Pending LOD transition flag — set when chunk is not ready for immediate LOD change. */
	FThreadSafeBool bPendingLODTransition { false };

	/** Target LOD for pending transition. */
	TAtomic<int32> PendingLOD { 0 };

	/** Smoothly transition to a new LOD level. */
	UFUNCTION(BlueprintCallable, Category = "Voxel|LOD")
	void TransitionToLOD(int32 NewLOD);

	/** Current mesh state for managing LOD transitions and visibility. */
	enum class EChunkMeshState : uint8
	{
		Empty,           // No mesh data
		Generating,      // Background task running
		Ready,           // Mesh ready and visible
		Transitioning,   // In LOD transition (generating new LOD, old mesh still visible)
		Error            // Generation failed
	};

	/** Current mesh state. */
	FORCEINLINE EChunkMeshState GetMeshState() const { return MeshState; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
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

	/** Dynamically grown HISM pool — one component per active per-biome foliage entry. */
	UPROPERTY()
	TArray<UInstancedStaticMeshComponent*> BiomeFoliageHISMs;

	/** Mutex to guard CurrentTask access across threads (though typically GameThread only) */
	FCriticalSection TaskLock;

	/** The running background task. Kept alive so we can cancel it on demand. */
	TSharedPtr<FVoxelGeneratorTask> CurrentTask;

	/** True once a mesh has been successfully applied and is visible. */
	FThreadSafeBool bMeshApplied { false };

	/** True while an async generation task is in flight. */
	FThreadSafeBool bGenerating  { false };

	/** Incremented each time a new task is launched; stale task completions are silently discarded. */
	TAtomic<uint64> GenerationId { 0 };

	/** Previous mesh data for smooth transitions during LOD changes. */
	FVoxelMeshOutput PreviousMesh;

	/** Current mesh output for state management. */
	FVoxelMeshOutput MeshOutput;

	/** Target LOD for smooth transitions. */
	int32 TargetLOD { 0 };

	/** Transition progress (0.0 to 1.0) for LOD blending. */
	float TransitionProgress { 0.0f };

	/** Visual-only mesh component for backfaces (no collision) */


	/** Time when transition started for timing-based blending. */
	float TransitionStartTime { 0.0f };

	/** Duration of LOD transitions in seconds. */
	static constexpr float TransitionDuration = 0.2f;

	// ---- Per-chunk material warning flags -----------------------------------
	// Stored as instance members (not static locals) so they reset when the
	// chunk is returned to the pool and ClearMesh() is called, allowing
	// re-warnings in new PIE sessions after the issue is fixed.


	void ApplyMesh(TSharedPtr<FVoxelGeneratorTask> CompletedTask);

	/**
	 * Upload one geometry section to a ProceduralMeshComponent.
	 * If Data.Vertices is empty, the section at SectionIndex is explicitly
	 * cleared to prevent ghost geometry (invisible collision) from a previous
	 * generation persisting after a dirty rebuild produces fewer sections.
	 */
	void UploadSection(int32 SectionIndex, const FVoxelMeshData& Data, UMaterialInterface* Mat, const FString& SectionName = FString(), class UProceduralMeshComponent* TargetMesh = nullptr);
	void CreateCollisionForChunk(const FVoxelMeshOutput& MeshData);

	/** Build water surface mesh from WaterData. Internal — call RebuildWaterMesh() instead. */
	void BuildWaterMeshInternal();

	/** Drive LOD transition progress each frame. */
	void UpdateMeshState();

	/** Set mesh visibility while maintaining proper state. */
	void SetMeshVisibility(bool bVisible);

	// -- Encapsulated Mutable State -------------------------------------------
	struct FVoxelDataMap* DataMap = nullptr;
	int32 LOD = 0;
	struct FVoxelDensityGenerator* DensityGenerator = nullptr;
	FThreadSafeBool bMeshDirty { false };
	
	FVoxelGenerationConfig GenerationConfig;
	FVoxelWaterData WaterData;
	EChunkMeshState MeshState { EChunkMeshState::Empty };
	
	FIntVector ChunkCoord;
};
