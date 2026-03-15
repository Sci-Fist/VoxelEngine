// VoxelGeneratorTask.h
// Thread-safe task: DensityField -> Mesh -> Per-Biome Foliage
//
// Foliage output is now a flat array of transform lists, one entry per foliage
// slot across all biomes.  Slot ordering matches FVoxelGenerationConfig biome order:
//   Forest entries first, then Peaks, Cliffs, Mesa, Craters.
//
// The chunk reads GetPerFoliageTransforms() and GetPerFoliageMeshes() to create
// one HISM component per unique mesh slot.
#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#include "Config/VoxelGenerationConfig.h"
#include "Generation/VoxelMeshGenerator.h"
#include "Generation/IVoxelDensityProvider.h"
#include "Math/Vector.h"
#include "Math/IntVector.h"

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
		float                         InFoliageDensity,   // legacy fallback density
		float                         InMaxFoliageSlope,  // legacy fallback slope
		const TMap<int32, float>&     InLocalDataCache
	);

	/** Runs BuildDensityField -> BuildMesh -> CalculateFoliage on a background thread. */
	void Execute();

	void Cancel() { bCancelled = true; }
	bool IsCancelled() const { return bCancelled; }

	// ---- Result accessors (call after Execute()) ----

	const FVoxelMeshOutput& GetMeshOutput() const { return MeshOutput; }

	/**
	 * Per-foliage-slot transforms.
	 * Outer index = global foliage slot (Forest entries first, then Peaks, etc.)
	 * Inner array  = spawn transforms for that slot in this chunk.
	 */
	const TArray<TArray<FTransform>>& GetPerFoliageTransforms() const { return PerFoliageTransforms; }

	/**
	 * Mesh pointer for each foliage slot, same indexing as GetPerFoliageTransforms().
	 * Null entries mean the slot had no mesh assigned (skip HISM creation).
	 */
	const TArray<UStaticMesh*>& GetPerFoliageMeshes() const { return PerFoliageMeshes; }

	/** Legacy: first Forest-biome foliage slot with SpawnChance > 0 (or nullptr). */
	const TArray<FTransform>& GetTreeTransforms()  const { return LegacyTreeTransforms;  }
	const TArray<FTransform>& GetGrassTransforms() const { return LegacyGrassTransforms; }

private:
	// --- Inputs ---
	FIntVector             ChunkCoord;
	FVector                WorldOrigin;
	int32                  ChunkSize;
	float                  VoxelSize;
	int32                  StepSize;
	FVoxelGenerationConfig Config;
	IVoxelDensityProvider* DensityProvider;
	float                  FoliageDensity;   // legacy
	float                  MaxFoliageSlope;  // legacy
	TMap<int32, float>     LocalDataCache;

	// --- Outputs ---
	FVoxelMeshOutput            MeshOutput;
	TArray<TArray<FTransform>>  PerFoliageTransforms;
	TArray<UStaticMesh*>        PerFoliageMeshes;
	TArray<FTransform>          LegacyTreeTransforms;
	TArray<FTransform>          LegacyGrassTransforms;

	// --- Intermediate ---
	struct FFoliageSlot
	{
		EVoxelBiome Biome;
		int32       EntryIdx;
		UStaticMesh* Mesh;          // raw ptr, safe: task lifetime < world lifetime
	};
	TArray<FFoliageSlot> FoliageSlots;
	bool bHasPerBiomeFoliage = false;

	TArray<float> Densities;
	TArray<FVoxelBiomeWeightMap> ColumnWeights; // Cache for foliage speedups


	void BuildDensityField();

	void BuildMesh();
	void CalculateFoliage();

	FThreadSafeBool bCancelled { false };
};
