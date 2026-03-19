#pragma once

#include "FirstVoxel.h"
#include "Voxel/Biomes/VoxelBiomeDataAsset.h"
#include "Voxel/Water/VoxelWaterSimulator.h" // Required: TUniquePtr<FVoxelWaterSimulator> needs complete type
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "Containers/Ticker.h"
#include "Voxel/Core/VoxelDataMap.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "Voxel/Generation/VoxelDensityGenerator.h" // Required: TUniquePtr<FVoxelDensityGenerator> needs complete type
#include "Voxel/Core/VoxelChunkPool.h"
#include "Voxel/VoxelLogger.h"
#include "Voxel/Core/VoxelChunkManager.h"
#include "VoxelWorld.generated.h"

class AVoxelChunk;
class UMaterialInterface;
class UStaticMesh;
class USceneComponent;

// =============================================================================
// AVoxelWorld
// =============================================================================
//
// Central manager for the FirstVoxel procedural world engine.
// Place one instance in the level. Drives all chunk generation, streaming,
// LOD transitions, water simulation, player spawn, and persistence.
//
// -- IMPLEMENTATION FILES -----------------------------------------------------
// 
//   VoxelWorld.cpp               Constructor, BeginPlay, Tick, ClearWorld,
//                                SnapPlayerToGround, preset helpers, utils
//   VoxelWorldGeneration.cpp     GenerateWorldDeferred, SpawnChunk,
//                                DestroyChunk, DrainQueue, ConfigureChunk,
//                                ProcessInitialPlayerSpawn
//   VoxelWorldModification.cpp   SetVoxelSphere, Save/Load, FindCraterSpawn,
//                                RunVoxelTests
//   VoxelWorld_Streaming.cpp     UpdateChunkStreaming, LOD BFS consistency
//   Water/VoxelWorldWater.cpp    UVoxelWorldWaterComponent tick
//
// -- TICK RESPONSIBILITIES ----------------------------------------------------
//
//   UpdateChunkStreaming()    Every StreamingInterval (0.25 s) in game world.
//                            Computes desired chunk set, destroys out-of-range
//                            chunks, queues new ones sorted nearest-first.
//                            LOD consistency uses a BFS dirty-queue seeded
//                            from chunks whose LOD changed — O(changed × 6)
//                            instead of the old O(N × 6 × MaxPasses).
//
//   DrainGenerationQueue()   Every tick. Spawns up to Limit chunks per tick
//                            (2 in editor, 8 in game) while ActiveGenerations
//                            < MaxConcurrentGenerations.
//
//   Hover-lock logic         After ProcessInitialPlayerSpawn() sets
//                            bWaitingForInitialSpawn, Tick holds the player
//                            at TargetCoordsZ until InitialSpawnCoords[] are
//                            all Ready, then releases movement.
//
//   Dirty-chunk rebuild      Any chunk with bMeshDirty=true is re-queued for
//                            GenerateAsync() once a concurrency slot opens.
//
// -- ACTIVE GENERATIONS COUNTER -----------------------------------------------
//
//   ActiveGenerations tracks in-flight background tasks. Incremented in
//   SpawnChunk BEFORE GenerateAsync(); decremented in the OnGenerationComplete
//   lambda. CancelGeneration() also fires OnGenerationComplete exactly once
//   (MoveTemp pattern) to keep the counter balanced.
//   MaxConcurrentGenerations (default 6) caps the thread-pool pressure.
//
// -- GENERATION CONFIG MERGE --------------------------------------------------
//
//   GetEffectiveConfig() returns a reference to MergedConfig (mutable member).
//   When BiomePreset is assigned, preset values win EXCEPT Seed, which always
//   comes from GenerationConfig.Seed so RandomizeSeed() takes effect.
//   Per-biome render/water configs from the Details panel properties are
//   injected into MergedConfig only when no preset is active.
//
// -- LOD CONSISTENCY BFS ------------------------------------------------------
//
//   UpdateChunkStreaming() seeds LodDirtyQueue with chunks whose desired LOD
//   changed in Pass 1. Pass 2 runs a BFS: each dequeued chunk pushes its LOD
//   to any neighbour with a higher (coarser) LOD. This ensures all loaded
//   chunks within the render volume share uniform LOD without seams, at
//   O(changed chunks × 6) cost instead of O(N × 6 × MaxPasses).
// =============================================================================
UCLASS()
class FIRSTVOXEL_API AVoxelWorld : public AActor {
  GENERATED_BODY()

public:
  AVoxelWorld();
  virtual ~AVoxelWorld();

  /** Resolution of each chunk (voxels per side). Default is 16. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|World")
  int32 ChunkSize = 16;

  /** Size of a single voxel in world units (cm). Default 100cm (1 meter). */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|World")
  float VoxelSize = 100.f;

  /** Horizontal distance (in chunks) to generate around the player. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Streaming")
  int32 RenderDistanceXY = 8;

  /** Horizontal distance (in chunks) specifically for Skylands. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Streaming")
  int32 SkylandsRenderDistanceXY = 8;

  /** Vertical distance (in chunks) to generate above/below the player. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Streaming")
  int32 RenderDistanceZ = 2;

  /** Max background tasks allowed at once. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Performance")
  int32 MaxConcurrentGenerations = 6;

  /** Distance from player where LOD1 (Lower Detail) chunks begin. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|LOD")
  float LOD1Distance = 6000.f;

  /** Distance from player where LOD2 (Lowest Detail) chunks begin. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|LOD")
  float LOD2Distance = 12000.f;

  /** The material used for flat/top surfaces (e.g. grass). */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Materials")
  UMaterialInterface *MasterFlatMaterial = nullptr;

  /** The material used for steep vertical surfaces (e.g. cliffs/rock). */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Materials")
  UMaterialInterface *MasterSlopeMaterial = nullptr;

  /** Threshold (dot product) at which flat material blends into slope cliff material. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Materials")
  float SlopeThreshold = 0.7f;

  /** The primary world generation configuration. Used if BiomePreset is not set. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Generation")
  FVoxelGenerationConfig GenerationConfig;

  /**
   * Asset-based configuration override.
   * Assigning a DataAsset here allows you to quickly swap between different
   * world styles (e.g. 'Moon', 'Evergreen', 'Desert').
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Generation")
  TObjectPtr<UVoxelBiomeDataAsset> BiomePreset;

  /** If true, the seed will be randomized automatically on BeginPlay. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Generation")
  bool bRandomizeSeedOnStartup = true;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Generation",
      meta = (ToolTip = "Rebuild the editor viewport world after stopping PIE."))
  bool bRegenerateViewportAfterPIE = true;

  /** Returns the effective generation config (merged with preset if active). */
  const FVoxelGenerationConfig &GetEffectiveConfig() const
  {
    if (BiomePreset != nullptr)
    {
      MergedConfig = BiomePreset->Config;
    }
    else
    {
      MergedConfig               = GenerationConfig;
      MergedConfig.ForestRender  = ForestRender;
      MergedConfig.PeaksRender   = PeaksRender;
      MergedConfig.CliffsRender  = CliffsRender;
      MergedConfig.MesaRender    = MesaRender;
      MergedConfig.CratersRender = CratersRender;
      MergedConfig.DesertRender  = DesertRender;
      MergedConfig.SkylandsRender = SkylandsRender;

      MergedConfig.ForestWater   = ForestWater;
      MergedConfig.PeaksWater    = PeaksWater;
      MergedConfig.CliffsWater   = CliffsWater;
      MergedConfig.MesaWater     = MesaWater;
      MergedConfig.CratersWater  = CratersWater;
      MergedConfig.DesertWater   = DesertWater;
      MergedConfig.SkylandsWater = SkylandsWater;
    }
    MergedConfig.Seed = GenerationConfig.Seed;
    MergedConfig.Craters.bForceCraterAtOrigin = bForceCraterSpawn;
    return MergedConfig;
  }

private:
  mutable FVoxelGenerationConfig MergedConfig;
public:

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Generation")
  bool bAutoGenerateOnBeginPlay = false;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Spawn")
  bool bForceCraterSpawn = true;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Spawn",
            meta = (ClampMin = "1000.0"))
  float CraterSpawnSearchRadius = 60000.f;

  /** Step size for the crater spawn search grid. Smaller values find craters
   *  more precisely but take longer to search. Works with CraterSpawnSearchRadius
   *  to define the search grid density. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Spawn",
            meta = (ClampMin = "100.0"))
  float CraterSpawnSearchStep = 4000.f;

  /** Convert a world-space position to its chunk grid coordinate.
   *  Uses floor division so negative positions map to the correct chunk.
   *  @param WorldPos  Absolute world position in cm.
   *  @return Chunk coordinate (e.g. chunk (2, -1, 0)). */
  FIntVector WorldToChunkCoord(const FVector &WorldPos) const;

  /** Convert a chunk grid coordinate to the world-space origin of that chunk.
   *  @param Coord  Chunk grid coordinate.
   *  @return World position of the chunk's minimum corner in cm. */
  FVector ChunkCoordToWorld(const FIntVector &Coord) const;

  /** Minimum crater biome weight (0-1) for a position to be considered a valid
   *  crater spawn. Higher values (e.g. 0.5) force the player into the deepest
   *  part of the crater. Lower values (e.g. 0.1) accept crater edges. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Spawn",
            meta = (ClampMin = "0.0", ClampMax = "1.0"))
  float CraterSpawnMinWeight = 0.25f;

  /** Height offset above terrain surface for safe player spawning (in cm).
   *  Must be at least capsule half-height (96cm) plus clearance.
   *  Default 8000cm (80m) ensures the player doesn't clip through terrain
   *  even in deep crater basins. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Spawn")
  float SafeSpawnHeightOffset = 8000.f;

  UFUNCTION(CallInEditor, Category = "Voxel",
            meta = (ToolTip = "Pick a new random seed and regenerate the world."))
  void GenerateWorld();

  UFUNCTION(CallInEditor, Category = "Voxel",
            meta = (ToolTip = "Destroy all chunks without generating new ones."))
  void ClearWorld();

  UFUNCTION(CallInEditor, Category = "Voxel")
  void SnapPlayerToGround();

  UFUNCTION(CallInEditor, Category = "Voxel|Presets")
  void SaveCurrentToPreset();

  UFUNCTION(CallInEditor, Category = "Voxel|Presets")
  void LoadFromPreset();

  virtual void OnConstruction(const FTransform &Transform) override;

  UFUNCTION(BlueprintCallable, Category = "Voxel|Interaction")
  void SetVoxelSphere(FVector WorldPosition, float Radius, float DensityValue,
                      bool bRebuildChunks = true);

  UFUNCTION(BlueprintCallable, Category = "Voxel|Testing")
  void RunVoxelTests();

  /** Pointer to the sparse player-edit data map.
   *  Stores all SetVoxelSphere modifications. Use this to query or
   *  persist player-driven terrain changes. */
  FVoxelDataMap *GetVoxelDataMap() { return &DataMap; }

  /** Read-only access to the loaded chunk map. Used by VoxelMapWidget for chunk outlines. */
  const TMap<FIntVector, AVoxelChunk *> *GetLoadedChunks() const {
    return &LoadedChunks;
  }

  /** Number of chunks waiting to be generated (pending in GenerationQueue). */
  int32 GetQueueCount() const { return GenerationQueue.Num(); }

  /** Read index into GenerationQueue. Chunks before this index have already
   *  been spawned; the queue is compacted periodically when QueueHead > 256. */
  int32 GetQueueHead() const { return QueueHead; }

  virtual void BeginPlay() override;
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
  virtual void Tick(float DeltaTime) override;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Persistence")
  FString SaveSlotName = TEXT("DefaultSlot");

  UFUNCTION(BlueprintCallable, Category = "Voxel|Persistence")
  void SaveToFile(const FString &SlotName);

  UFUNCTION(BlueprintCallable, Category = "Voxel|Persistence")
  void LoadFromFile(const FString &SlotName);

  UFUNCTION(BlueprintCallable, Category = "Voxel|Persistence")
  void ClearWorldModifications();

  UFUNCTION(CallInEditor, Category = "Voxel|Actions",
            meta = (ToolTip = "Regenerate the world with the current seed."))
  void RebuildWorld();

  UFUNCTION(CallInEditor, Category = "Voxel|Actions")
  void ClearModifications();

  UFUNCTION(CallInEditor, Category = "Voxel|Persistence", meta=(DisplayName="Save Slot"))
  void SaveDefaultSlot();

  UFUNCTION(CallInEditor, Category = "Voxel|Persistence", meta=(DisplayName="Load Slot"))
  void LoadDefaultSlot();

  UFUNCTION(CallInEditor, Category = "Voxel|Actions")
  void RunTests();

  void TestSmoothLODTransitions();

  UFUNCTION(BlueprintCallable, Category = "Voxel|Terrain")
  float GetTerrainHeight(float X, float Y) const;

  /** Convenience alias for GetTerrainHeight(X, Y). Returns the biome-blended
   *  surface height at world position (X, Y) in cm. */
  float GetSurfaceZ(float X, float Y) const;

  // ================================================================
  //  PER-BIOME: materials + foliage
  // ================================================================

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Forest")
  FVoxelBiomeRenderConfig ForestRender;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Forest")
  FVoxelBiomeWaterConfig ForestWater;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Peaks")
  FVoxelBiomeRenderConfig PeaksRender;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Peaks")
  FVoxelBiomeWaterConfig PeaksWater;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Cliffs")
  FVoxelBiomeRenderConfig CliffsRender;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Cliffs")
  FVoxelBiomeWaterConfig CliffsWater;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Mesa")
  FVoxelBiomeRenderConfig MesaRender;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Mesa")
  FVoxelBiomeWaterConfig MesaWater;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Craters")
  FVoxelBiomeRenderConfig CratersRender;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Craters")
  FVoxelBiomeWaterConfig CratersWater;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Desert")
  FVoxelBiomeRenderConfig DesertRender;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Desert")
  FVoxelBiomeWaterConfig DesertWater;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel")
  class UVoxelWaterComponent *WaterComponent = nullptr;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Skylands")
  FVoxelBiomeRenderConfig SkylandsRender;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Skylands")
  FVoxelBiomeWaterConfig SkylandsWater;

  UPROPERTY(EditAnywhere, Category = "Voxel|Foliage|Legacy",
            meta = (ToolTip = "Fallback tree mesh when no per-biome foliage is configured."))
  UStaticMesh *TreeMesh = nullptr;

  UPROPERTY(EditAnywhere, Category = "Voxel|Foliage|Legacy",
            meta = (ToolTip = "Fallback grass mesh when no per-biome foliage is configured."))
  UStaticMesh *GrassMesh = nullptr;

  UPROPERTY(EditAnywhere, Category = "Voxel|Foliage|Legacy",
            meta = (ClampMin = "0.0", ClampMax = "1.0",
                    ToolTip = "Global spawn chance for legacy foliage."))
  float FoliageDensity = 0.05f;

  UPROPERTY(EditAnywhere, Category = "Voxel|Foliage|Legacy",
            meta = (ClampMin = "0.0", ClampMax = "1.0",
                    ToolTip = "Maximum Normal.Z for legacy foliage placement."))
  float MaxFoliageSlope = 0.8f;

  UPROPERTY(VisibleAnywhere, Category = "Voxel")
  class USceneComponent *Root;

private:
  FVoxelDataMap DataMap;

  TMap<FIntVector, AVoxelChunk *> LoadedChunks;
  TSet<FIntVector> EmptyChunks;

  /** Decoupled dense density node grid manager. */
  FVoxelChunkManager ChunkManager;

  FVoxelChunkPool ChunkPool;

  /**
   * Dirty-chunk rebuild queue.
   * Chunks added here (via MarkChunkDirty) are rebuilt next tick when a
   * concurrency slot is free.
   */
  TArray<FIntVector> DirtyRebuildQueue;

  /** Mark a chunk as needing a mesh rebuild. Game-thread only.
   *  Adds the chunk to DirtyRebuildQueue AND sets bMeshDirty on the chunk.
   *  The chunk will be regenerated next tick when a concurrency slot opens.
   *  @param Coord  Chunk grid coordinate to mark dirty. */
  void MarkChunkDirty(const FIntVector& Coord);

  TArray<FIntVector> GenerationQueue;

  /**
   * GenerationQueue read head. Avoids O(N) RemoveAt(0) each tick.
   * Compacted periodically in DrainGenerationQueue().
   */
  int32 QueueHead = 0;

  int32 ActiveGenerations = 0;

  FVector LastStreamedPos = FVector::ZeroVector;

  float StreamingTimer = 0.f;
  static constexpr float StreamingInterval = 0.25f;

  /** Cached skyland altitude. Recomputed only when player moves > SkyAltSnapDist. */
  float CachedSkyAltWorld = 0.f;
  float CachedCurvedH = 0.f;
  float CachedCurvedR = 0.f;
  FVector LastSkyAltPos   = FVector(1e9f);
  static constexpr float SkyAltSnapDist = 1000.f;

  bool bInitialized = false;
  FThreadSafeBool bShutdown{false};

  TUniquePtr<FVoxelDensityGenerator> DensityGenerator;

  UPROPERTY(VisibleAnywhere, Category = "Voxel|Water")
  class UVoxelWorldWaterComponent* WaterSystemComponent = nullptr;

  void ProcessInitialPlayerSpawn();

  // --- Startup Spawn Tracking ---
  bool bWaitingForInitialSpawn = false;
  TArray<FIntVector> InitialSpawnCoords;
  float TargetCoordsZ = 0.f;
  bool bSkylandFoundBackup = false;
  float CachedSurfaceHeight = 0.f;
  FVector SpawnTargetPos = FVector::ZeroVector;
  float SpawnWaitAccum = 0.f;
  float SpawnDelayAccum = 0.f;
  static constexpr float SpawnHoldDelay = 2.0f;

  FVector SnapToVoxelGrid(const FVector &WorldPos) const;
  FVector FindCraterSpawnLocation(const FVector &StartPos, const FVoxelGenerationConfig &Config) const;
  float GetSafeSpawnHeightOffset() const;

  void RandomizeSeed();

public:
  void GenerateWorldDeferred();

  UFUNCTION(BlueprintPure, Category = "Voxel")
  bool IsWaitingForInitialSpawn() const { return bWaitingForInitialSpawn; }

  UFUNCTION(BlueprintPure, Category = "Voxel")
  float GetGenerationProgress() const;

private:
  void SpawnChunk(const FIntVector &Coord, bool bSyncCollision = false);
  void DestroyChunk(const FIntVector &Coord);
  void RebuildChunk(const FIntVector &Coord);
  void UpdateChunkStreaming();
  void DrainGenerationQueue();
  void DiscoverExistingChunks();

#if WITH_EDITOR
  virtual void PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent) override;

  FTSTicker::FDelegateHandle DrainTickerHandle;
  FTSTicker::FDelegateHandle DeferTickerHandle;
#endif

  void ConfigureChunk(AVoxelChunk *Chunk) const;

  void PerformWorldDiscoveryAndBoundsCalculation();
  void FinalizeGenerationSetup();

  // Close-range visibility health check — called every Tick.
  // Ensures chunks near the player that are marked Ready have visible meshes.
  void CheckCloseRangeVisibility();
  // NOTE: ApplyMeshToChunk() and EnforceLODConsistency() have been removed.
  // They were dead code — defined in VoxelWorld_Streaming.cpp but never called
  // from any code path. LOD consistency is now handled by the BFS dirty-queue
  // in UpdateChunkStreaming(). Close-range visibility is handled by
  // CheckCloseRangeVisibility() above.
};
