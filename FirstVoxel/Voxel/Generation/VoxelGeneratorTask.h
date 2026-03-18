// =============================================================================
// VoxelGeneratorTask.h
// =============================================================================
//
// Self-contained background task that converts a chunk coordinate into a
// complete mesh, foliage, and water-source dataset ready for upload on the
// game thread. All work runs on a background thread; no UObject API is touched.
//
// -- PIPELINE (called by Execute() in order) ----------------------------------
//
//   BuildDensityField()    Fill Densities[(EffSize+3)^3] via ParallelFor.
//                          Per-column work (biome weights, surface height,
//                          skyland cache) is hoisted above the Z loop so it
//                          runs O(n^2) instead of O(n^3).
//
//   PostProcessDensities() Safety-clamp extreme solid/air ratios that can
//                          cause entirely solid or empty chunks.
//
//   BuildMesh()            Run Surface Nets on Densities to produce FlatMesh.
//
//   CalculateFoliage()     Scatter instanced meshes on upward-facing triangles.
//                          Uses ColumnWeights[] built during density pass to
//                          avoid re-running biome noise per triangle.
//
//   PlaceWaterSources()    Scan for enclosed air-on-solid depressions and
//                          emit world-voxel coordinates for water spawning.
//
// -- FOLIAGE SLOT LAYOUT ------------------------------------------------------
//
//   FoliageSlots[] is built once in the constructor from the config.
//   PerFoliageTransforms[s] holds all spawn transforms for slot s.
//   PerFoliageMeshes[s]     holds the UStaticMesh* for slot s.
//   Slot order: Forest entries first, then Peaks, Cliffs, Mesa, Craters, Desert.
//   GBiomeOrder[] in the .cpp enforces this with a compile-time static_assert.
//
//   Legacy fallback (no per-biome foliage configured):
//     LegacyTreeTransforms  / LegacyGrassTransforms are populated instead.
//
// -- CANCELLATION -------------------------------------------------------------
//
//   Call Cancel() from any thread to set bCancelled. Execute() checks this
//   flag between sub-passes and returns early. AVoxelChunk::GenerationId
//   provides a second guard: the game-thread callback discards results from
//   any task whose ID no longer matches the chunk's current generation.
//
// -- THREAD SAFETY ------------------------------------------------------------
//
//   No UObject methods are called inside Execute(). All outputs are plain C++
//   containers. AVoxelChunk::ApplyMesh() uploads them on the game thread.
// =============================================================================
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
	TArray<FVoxelBiomeWeightMap> ColumnWeights;  // Cache for foliage speedups
	TArray<float>               ColumnSurfaceH;  // Surface height per column, same indexing as ColumnWeights

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

	bool bIsFullSolid = false;
	bool bIsFullAir   = false;

	void CountDensityStates(int32 TotalSamples);
	void TrimFoliageToCap(const int32 MaxMeshesPerChunk);

	FThreadSafeBool bCancelled { false };
};
