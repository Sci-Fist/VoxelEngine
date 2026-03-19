// =============================================================================
// VoxelGeneratorTask.h
// =============================================================================
//
// Self-contained background task that converts a chunk coordinate into a
// complete mesh, foliage, and water-source dataset ready for upload on the
// game thread. All work runs on a background thread; no UObject API is touched.
//
// @thread-safety Thread-safe. All processing runs on background threads without
//                UObject dependencies. Results are plain C++ containers safe
//                for game-thread upload via AVoxelChunk::ApplyMesh().
// @performance   O(N³) density generation, O(N²) mesh output for N³ voxel chunk.
//                Optimized with ParallelFor, per-column caching, and early termination.
//
// -- PIPELINE OVERVIEW --------------------------------------------------------
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
// -- PERFORMANCE OPTIMIZATIONS ------------------------------------------------
//
//   - Per-column caching: Biome weights and surface heights computed once per
//     XY column (O(N²)) instead of per voxel (O(N³))
//   - ParallelFor processing for density field generation
//   - Early termination for cancellation and extreme density states
//   - LOD-aware processing with StepSize parameter
//   - Memory-efficient density array with padding for Surface Nets algorithm
//
// -- FOLIAGE SYSTEM ARCHITECTURE ---------------------------------------------
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
//   Foliage placement algorithm:
//   1. Iterate through mesh triangles
//   2. Filter by normal direction (upward-facing)
//   3. Sample biome weights at triangle center
//   4. Spawn appropriate foliage types based on biome configuration
//   5. Apply density and slope constraints
//
// -- WATER SOURCE DETECTION ---------------------------------------------------
//
//   PlaceWaterSources() implements terrain analysis to find suitable water
//   spawning locations:
//   1. Scan for terrain depressions (air voxels surrounded by solid)
//   2. Identify skyland flat surfaces suitable for water pools
//   3. Validate slope and accessibility constraints
//   4. Emit world-voxel coordinates for water system initialization
//
// -- CANCELLATION AND LIFETIME MANAGEMENT -------------------------------------
//
//   Call Cancel() from any thread to set bCancelled. Execute() checks this
//   flag between sub-passes and returns early. AVoxelChunk::GenerationId
//   provides a second guard: the game-thread callback discards results from
//   any task whose ID no longer matches the chunk's current generation.
//
//   This dual-cancellation system ensures:
//   - Immediate response to cancellation requests
//   - Prevention of stale data being applied to chunks
//   - Safe cleanup of background tasks
//
// -- INTEGRATION WITH CHUNK SYSTEM --------------------------------------------
//
//   Designed for seamless integration with VoxelChunkManager:
//   - Chunk coordinates and world origin provided as input
//   - Results uploaded via AVoxelChunk::ApplyMesh() on game thread
//   - GenerationId ensures result validity
//   - Supports LOD system with configurable StepSize
//
//   Output data flow:
//   1. Density field → Surface Nets mesh generation
//   2. Mesh data → Foliage placement and water source detection
//   3. Combined results → Game thread upload and rendering
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
		float                         InFoliageDensity,   // legacy fallback density
		float                         InMaxFoliageSlope,  // legacy fallback slope
		struct FVoxelDataMap*         InDataMap
	);

	~FVoxelGeneratorTask();

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
	const FIntVector& GetChunkCoord() const { return ChunkCoord; }

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
	struct FVoxelDataMap*  DataMap;

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

	// Precomputed skyland caches for LOD-independent generation.
	// Indexed by actual voxel coordinates: [i][j] where i,j in [0, ChunkSize-1].
	// This ensures the same skyland presence regardless of LOD step size.
	TArray<TArray<FSkylandColumnCache>> SkylandColumnCaches;

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
