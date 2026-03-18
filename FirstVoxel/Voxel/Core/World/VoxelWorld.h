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
//   VoxelWorld_Streaming.cpp     UpdateChunkStreaming, LOD hysteresis
//   Water/VoxelWorldWater.cpp    UVoxelWorldWaterComponent tick
//
// -- TICK RESPONSIBILITIES ----------------------------------------------------
//
//   UpdateChunkStreaming()    Every StreamingInterval (0.25 s) in game world.
//                            Computes desired chunk set, destroys out-of-range
//                            chunks, queues new ones sorted nearest-first.
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
//   MaxConcurrentGenerations (default 12) caps the thread-pool pressure.
//
// -- GENERATION CONFIG MERGE --------------------------------------------------
//
//   GetEffectiveConfig() returns a reference to MergedConfig (mutable member).
//   When BiomePreset is assigned, preset values win EXCEPT Seed, which always
//   comes from GenerationConfig.Seed so RandomizeSeed() takes effect.
//   Per-biome render/water configs from the Details panel properties are
//   injected into MergedConfig only when no preset is active.
//
// -- EDITOR SPAWN SIMULATION --------------------------------------------------
//
//   When Generate World is pressed in the editor (not PIE), GenerateWorldDeferred
//   runs the same crater-search and spawn-chunk prioritisation as runtime,
//   wrapped in #if WITH_EDITOR. The viewport therefore shows the actual spawn
//   area the player sees on first load.
// =============================================================================
UCLASS()
class FIRSTVOXEL_API AVoxelWorld : public AActor {
  GENERATED_BODY()

public:
  AVoxelWorld();
  virtual ~AVoxelWorld();

  /** Resolution of each chunk (voxels per side). Default is 16. Larger chunks
   * are more efficient for rendering but take longer to generate. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|World")
  int32 ChunkSize = 16;

  /** Size of a single voxel in world units (cm). Default 100cm (1 meter). */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|World")
  float VoxelSize = 100.f;

  /** Horizontal distance (in chunks) to generate around the player. Total
   * chunks: (2*Dist+1)^2. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Streaming")
  int32 RenderDistanceXY = 5;

  /** Horizontal distance (in chunks) specifically for Skylands. High values
   * allow them to render far into the background with minimal performance hit.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Streaming")
  int32 SkylandsRenderDistanceXY = 8; // Increased from 5 for vista size

  /** Vertical distance (in chunks) to generate above/below the player. High
   * values allow for massive mountains and deep caves. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Streaming")
  int32 RenderDistanceZ = 3; // Increased to 3

  /** Max background tasks allowed at once. Higher values speed up generation
   * but can cause framerate hitching or high CPU usage. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Performance")
  int32 MaxConcurrentGenerations = 12;

  /** Distance from player where LOD1 (Lower Detail) chunks begin. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|LOD")
  float LOD1Distance = 6000.f;

  /** Distance from player where LOD2 (Lowest Detail) chunks begin. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|LOD")
  float LOD2Distance = 12000.f;

  /** The material used for flat/top surfaces (e.g. grass). Should use a
   * Triplanar mapping shader for best results. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Materials")
  UMaterialInterface *MasterFlatMaterial = nullptr;

  /** The material used for steep vertical surfaces (e.g. cliffs/rock). */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Materials")
  UMaterialInterface *MasterSlopeMaterial = nullptr;

  /** Threshold (dot product) at which flat material blends into slope cliff material. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Materials")
  float SlopeThreshold = 0.7f;

  /** The primary world generation configuration. Used if BiomePreset is not
   * set. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Generation")
  FVoxelGenerationConfig GenerationConfig;

  /**
   * Asset-based configuration override.
   * Assigning a DataAsset here allows you to quickly swap between different
   * world styles (e.g. 'Moon', 'Evergreen', 'Desert').
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Generation")
  TObjectPtr<UVoxelBiomeDataAsset> BiomePreset;

  /** If true, the seed will be randomized automatically on BeginPlay to ensure
   * a different layout every run. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Generation")
  bool bRandomizeSeedOnStartup = true;

  /**
   * If true, the editor viewport will regenerate the world immediately after
   * PIE ends, so you can inspect the exact terrain that was generated at
   * runtime without pressing Generate World manually.  The PIE seed is
   * preserved so the result is identical.
   */
  UPROPERTY(
      EditAnywhere, BlueprintReadWrite, Category = "Voxel|Generation",
      meta = (ToolTip = "Rebuild the editor viewport world after stopping Play-In-Editor so you can inspect what was generated."))
  bool bRegenerateViewportAfterPIE = true;

  /** Returns the effective generation config.
   * Preset wins for all parameters EXCEPT Seed, which always comes from
   * GenerationConfig so RandomizeSeed() takes effect regardless of preset.
   * We cache the merged result as a mutable member to avoid rebuilding it
   * every call (seed writes are infrequent; config reads are per-voxel). */
  const FVoxelGenerationConfig &GetEffectiveConfig() const
  {
    if (BiomePreset != nullptr)
    {
      // Merge: start with preset, override seed from our runtime GenerationConfig
      // so every RandomizeSeed() call actually changes the world.
      MergedConfig         = BiomePreset->Config;
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

  /** If true, searches for a crater biome region near the PlayerStart and
   * places the player inside the crater basin on spawn.
   * Uses FindCraterSpawnLocation() with the current seed to find the nearest
   * high-weight crater zone. Does NOT mutate any config. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Spawn")
  bool bForceCraterSpawn = true;

  /** Max distance from the initial position to search for a crater spawn (cm).
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Spawn",
            meta = (ClampMin = "1000.0"))
  float CraterSpawnSearchRadius = 60000.f;

  /** Step size for crater spawn search grid (cm). Larger values scan faster but
   * are less precise. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Spawn",
            meta = (ClampMin = "100.0"))
  float CraterSpawnSearchStep = 4000.f;

  FIntVector WorldToChunkCoord(const FVector &WorldPos) const;
  FVector ChunkCoordToWorld(const FIntVector &Coord) const;

  /** Minimum crater biome weight required to accept a spawn location. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Spawn",
            meta = (ClampMin = "0.0", ClampMax = "1.0"))
  float CraterSpawnMinWeight = 0.25f;

  /** Minimum height above terrain when snapping the player to ground (cm).
   *  Raised to 8000cm (80m) so the player always drops in from above terrain
   *  rather than spawning inside it on steep peaks or high craters. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Spawn")
  float SafeSpawnHeightOffset = 8000.f;

  /**
   * Randomizes the seed and regenerates the world from scratch.
   * Also simulates the player spawn sequence (finds crater, places spawn
   * chunk) so the editor viewport shows exactly what PIE will look like.
   */
  UFUNCTION(CallInEditor, Category = "Voxel",
            meta = (ToolTip = "Pick a new random seed and regenerate the world. Simulates player spawn so the viewport shows the actual spawn area."))
  void GenerateWorld();

  /** Destroy all chunks without generating new ones. */
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

  FVoxelDataMap *GetVoxelDataMap() { return &DataMap; }

  /** Read-only access to the loaded chunk map. Used by VoxelMapWidget for chunk
   * outlines. */
  const TMap<FIntVector, AVoxelChunk *> *GetLoadedChunks() const {
    return &LoadedChunks;
  }

  virtual void BeginPlay() override;
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
  virtual void Tick(float DeltaTime) override;

  /** Name of the file slot used for Editor Saving or Loading. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Persistence")
  FString SaveSlotName = TEXT("DefaultSlot");

  UFUNCTION(BlueprintCallable, Category = "Voxel|Persistence")
  void SaveToFile(const FString &SlotName);

  UFUNCTION(BlueprintCallable, Category = "Voxel|Persistence")
  void LoadFromFile(const FString &SlotName);

  UFUNCTION(BlueprintCallable, Category = "Voxel|Persistence")
  void ClearWorldModifications();

  // --- Editor Actions ---

  /** Regenerate with the CURRENT seed — useful for testing config changes without changing the world layout. */
  UFUNCTION(CallInEditor, Category = "Voxel|Actions",
            meta = (ToolTip = "Regenerate the world with the current seed. Use Generate World (above) to get a new random seed instead."))
  void RebuildWorld();

  /** Clears in-memory voxel edits node buckets quickly. */

  UFUNCTION(CallInEditor, Category = "Voxel|Actions")
  void ClearModifications();

  /** Saves in-memory voxel edits node buckets quickly. */
  UFUNCTION(CallInEditor, Category = "Voxel|Persistence", meta=(DisplayName="Save Slot"))
  void SaveDefaultSlot();

  /** Loads state from target SaveSlotName. */
  UFUNCTION(CallInEditor, Category = "Voxel|Persistence", meta=(DisplayName="Load Slot"))
  void LoadDefaultSlot();

  /** Triggers system verification tests from the Editor Details Panel. */
  UFUNCTION(CallInEditor, Category = "Voxel|Actions")
  void RunTests();

  /** Test that chunks can transition between LOD levels without invisible mesh pop */
  void TestSmoothLODTransitions();

  // ----------------------

  UFUNCTION(BlueprintCallable, Category = "Voxel|Terrain")
  float GetTerrainHeight(float X, float Y) const;

  float GetSurfaceZ(float X, float Y) const;

  // ================================================================
  //  PER-BIOME: materials + foliage
  //  Click the arrow next to each biome name to expand its settings.
  //  Each section contains:
  //    - Material overrides (flat surface + slope/cliff)
  //    - Foliage array (add as many mesh entries as you like)
  //  Null material = fall back to global MasterFlatMaterial /
  //  MasterSlopeMaterial.
  // ================================================================

  /** Lush Forest biome — rolling hills and plains. Expand to set materials +
   * foliage. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Forest")
  FVoxelBiomeRenderConfig ForestRender;

  /** Forest water configuration. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Forest")
  FVoxelBiomeWaterConfig ForestWater;

  /** Jagged Peaks biome — alpine mountains. Expand to set materials + foliage.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Peaks")
  FVoxelBiomeRenderConfig PeaksRender;

  /** Peaks water configuration. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Peaks")
  FVoxelBiomeWaterConfig PeaksWater;

  /** Steep Cliffs biome — ridged canyon walls. Expand to set materials +
   * foliage. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Cliffs")
  FVoxelBiomeRenderConfig CliffsRender;

  /** Cliffs water configuration. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Cliffs")
  FVoxelBiomeWaterConfig CliffsWater;

  /** Mesa Plateaus biome — flat-top sandstone columns. Expand to set materials
   * + foliage. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Mesa")
  FVoxelBiomeRenderConfig MesaRender;

  /** Mesa water configuration. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Mesa")
  FVoxelBiomeWaterConfig MesaWater;

  /** Impact Craters biome — rare meteorite basins. Expand to set materials +
   * foliage. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Craters")
  FVoxelBiomeRenderConfig CratersRender;

  /** Craters water configuration. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Craters")
  FVoxelBiomeWaterConfig CratersWater;

  /** Sand Dunes biome — low erosion, high temp. Expand to set materials +
   * foliage. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Desert")
  FVoxelBiomeRenderConfig DesertRender;

  /** Desert water configuration. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Biomes|Desert")
  FVoxelBiomeWaterConfig DesertWater;

  /** Implicit water rendering wrapper to isolate ocean translated budgets. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel")
  class UVoxelWaterComponent *WaterComponent = nullptr;

  /**
   * Skylands — floating islands high above the terrain.
   * Material overrides apply to ALL island surfaces regardless of the biome
   * below. Foliage entries use MinWorldZ / MaxWorldZ to restrict spawns to
   * island altitude. Leave material slots null to fall back to the global
   * MasterFlat/SlopeMaterial.
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite,
            Category = "Voxel|Biomes|Skylands")
  FVoxelBiomeRenderConfig SkylandsRender;

  /** Skylands water configuration. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite,
            Category = "Voxel|Biomes|Skylands")
  FVoxelBiomeWaterConfig SkylandsWater;

  /**
   * Legacy tree mesh — used ONLY if no foliage entries are defined in the
   * generation config's ForestRender.FoliageTypes.  Prefer the per-biome
   * foliage system above.
   */
  UPROPERTY(EditAnywhere, Category = "Voxel|Foliage|Legacy",
            meta = (ToolTip = "Fallback tree mesh when no per-biome foliage is configured. Prefer the Biome sections instead."))
  UStaticMesh *TreeMesh = nullptr;

  /** Legacy grass mesh — see TreeMesh. */
  UPROPERTY(
      EditAnywhere, Category = "Voxel|Foliage|Legacy",
      meta = (ToolTip = "Fallback grass mesh when no per-biome foliage is configured."))
  UStaticMesh *GrassMesh = nullptr;

  /** Legacy global foliage density — used only by the legacy tree/grass
   * fallback path. */
  UPROPERTY(EditAnywhere, Category = "Voxel|Foliage|Legacy",
            meta = (ClampMin = "0.0", ClampMax = "1.0",
                    ToolTip = "Global spawn chance for legacy foliage. Ignored when per-biome foliage is configured."))
  float FoliageDensity = 0.05f;

  /** Legacy max slope for foliage — used only by the legacy tree/grass fallback
   * path. */
  UPROPERTY(EditAnywhere, Category = "Voxel|Foliage|Legacy",
            meta = (ClampMin = "0.0", ClampMax = "1.0",
                    ToolTip = "Maximum Normal.Z for legacy foliage placement."))
  float MaxFoliageSlope = 0.8f;

  UPROPERTY(VisibleAnywhere, Category = "Voxel")
  class USceneComponent *Root;

private:
  FVoxelDataMap DataMap;

#include "Core/VoxelChunkManager.h"

  TMap<FIntVector, AVoxelChunk *> LoadedChunks;
  TSet<FIntVector> EmptyChunks;

  /** Decoupled Dense density node grid manager buffer cache structures. */
  FVoxelChunkManager ChunkManager;

  FVoxelChunkPool ChunkPool;

  /**
   * Dirty-chunk rebuild queue.
   * Chunks added here (via MarkChunkDirty) are rebuilt next tick when a
   * concurrency slot is free.  Replaces the O(N) full LoadedChunks scan that
   * previously ran every single frame regardless of whether anything was dirty.
   */
  TArray<FIntVector> DirtyRebuildQueue;

  /** Mark a chunk as needing a mesh rebuild. Thread-safe: call from game thread only. */
  void MarkChunkDirty(const FIntVector& Coord);

  TArray<FIntVector> GenerationQueue;
  /**
   * GenerationQueue read head.
   *
   * Used by DrainGenerationQueue() and editor synchronous generation to avoid
   * O(N) RemoveAt(0) each tick. We periodically compact the queue when this
   * grows large.
   */
  int32 QueueHead = 0;

  int32 ActiveGenerations = 0;

  FVector LastStreamedPos = FVector::ZeroVector;

  float StreamingTimer = 0.f;
  static constexpr float StreamingInterval = 0.25f;

  /** Cached skyland altitude (world Z, cm). Recomputed only when player moves > SkyAltSnapDist. */
  float CachedSkyAltWorld = 0.f;
  float CachedCurvedH = 0.f;
  float CachedCurvedR = 0.f;
  FVector LastSkyAltPos   = FVector(1e9f); // force first compute
  static constexpr float SkyAltSnapDist = 1000.f; // recompute every 10m of movement


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
  // FIX: Replaces static-local SpawnWaitTime in Tick() to avoid MSVC C2181
  // and to reset properly between PIE sessions.
  float SpawnWaitAccum = 0.f;
  float SpawnDelayAccum = 0.f;
  static constexpr float SpawnHoldDelay = 2.0f;

  FVector SnapToVoxelGrid(const FVector &WorldPos) const;

  FVector FindCraterSpawnLocation(const FVector &StartPos,
                                  const FVoxelGenerationConfig &Config) const;
  float GetSafeSpawnHeightOffset() const;

  /** Internal helper — picks a new random seed. Called by GenerateWorld() and BeginPlay(). Not exposed to the editor panel. */
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
  virtual void
  PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent) override;

  /** Handle for the editor drain ticker to prevent stacking. */
  FTSTicker::FDelegateHandle DrainTickerHandle;

  /** Handle for the editor defer ticker to prevent stacking. */
  FTSTicker::FDelegateHandle DeferTickerHandle;
#endif

  void ConfigureChunk(AVoxelChunk *Chunk) const;
};
