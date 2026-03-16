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

	/**
	 * World-voxel coordinates of detected water source positions for this chunk.
	 * These are terrain depressions and skyland pool surfaces where water should spawn.
	 * Populated by PlaceWaterSources() — consumed by AVoxelChunk::ApplyMesh().
	 */
	const TArray<FIntVector>& GetWaterSources() const { return WaterSources; }

	/**
	 * Raw terrain density field, size = (ChunkSize/StepSize + 3)^3.
	 * Exposed so AVoxelChunk can build FVoxelWaterData::SolidCells without re-running noise.
	 */
	const TArray<float>& GetDensities() const { return Densities; }

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

	// Water source positions (world-voxel coords) detected during generation.
	TArray<FIntVector> WaterSources;

	void BuildDensityField();
	void PostProcessDensities(int32 TotalSamples);
	void BuildMesh();

	void CalculateFoliage();
	void ProcessLegacyFoliage(const FVector& Center, float SlopeZ, const FVoxelBiomeWeightMap& TriWeights, const FVector& WorldCenter);


	/**
	 * Scan the density field for terrain depressions and skyland flat surfaces
	 * suitable for water pools/springs.  Populates WaterSources.
	 * Called after BuildMesh() so we have both density and mesh normal data.
	 */
	void PlaceWaterSources();

	FThreadSafeBool bCancelled { false };
};
