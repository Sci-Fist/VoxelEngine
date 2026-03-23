// VoxelWorld.h — Valheim-scale render distance parameters

#pragma once

#include "FirstVoxel.h"
#include "Voxel/Biomes/VoxelBiomeDataAsset.h"
#include "Voxel/Water/VoxelWaterSimulator.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "Containers/Ticker.h"
#include "HAL/ThreadSafeCounter.h"
#include "Voxel/Core/VoxelDataMap.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "Voxel/Generation/VoxelDensityGenerator.h"
#include "Voxel/Core/VoxelChunkPool.h"
#include "Voxel/VoxelLogger.h"
#include "Voxel/Core/VoxelChunkManager.h"
#include "VoxelWorld.generated.h"

class AVoxelChunk;
class UMaterialInterface;
class UStaticMesh;
class USceneComponent;
class UVoxelStreamingComponent;
class UVoxelSpawnHandlerComponent;

UCLASS()
class FIRSTVOXEL_API AVoxelWorld : public AActor
{
    GENERATED_BODY()

    friend class UVoxelStreamingComponent;
    friend class UVoxelSpawnHandlerComponent;


public:
    AVoxelWorld();
    virtual ~AVoxelWorld();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|World")
    int32 ChunkSize = 16;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|World")
    float VoxelSize = 100.f;

    // ── Render Distance — Three-zone system ──────────────────────────────────
    //
    //  Zone A (LOD 0 — full detail):
    //    Radius = RenderDistanceXY chunks × 1600 cm = 2240 cm = ~22m
    //    Vertical = RenderDistanceZ × 1600 cm above/below terrain = ±16000cm
    //    This is the immediate play area — everything looks sharp.
    //
    //  Zone B (LOD 1 — half resolution):
    //    Radius = MidRenderDistanceXY × 1600 cm = ~640m
    //    Vertical = MidRenderDistanceZ × 1600 cm = ±6400cm around terrain
    //    Distant terrain still looks good, hills and forests readable.
    //
    //  Zone C (LOD 2 — quarter resolution):
    //    Radius = DistantRenderDistanceXY × 1600 cm = ~1280m
    //    Vertical = only 1 chunk above/below surface (silhouette only)
    //    Far horizon like Valheim — you can see the shape of distant lands.
    //
    //  Skylands:
    //    Radius = SkylandsRenderDistanceXY = ~320m
    //    Vertical = SkylandsRenderDistanceZ chunks above SkyAlt (sky band)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming",
        meta=(ToolTip="Zone A radius (LOD 0 full detail). 24 chunks = 384m."))
    int32 RenderDistanceXY = 24;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming",
        meta=(ToolTip="Zone A vertical half-range above/below terrain (chunks). 16 = ±256m, covers caves and skylands from crater floor."))
    int32 RenderDistanceZ = 16;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming",
        meta=(ToolTip="Zone B radius (LOD 1 half-resolution). 40 chunks = 640m. Middle ground between playspace and horizon."))
    int32 MidRenderDistanceXY = 40;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming",
        meta=(ToolTip="Zone B vertical half-range (chunks). 4 = ±64m. Thin slice — just terrain surface + a little above/below."))
    int32 MidRenderDistanceZ = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming",
        meta=(ToolTip="Zone C radius (LOD 2 silhouette). 80 chunks = 1280m. Valheim-level horizon view distance."))
    int32 DistantRenderDistanceXY = 80;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming",
        meta=(ClampMin="2", ClampMax="4",
              ToolTip="LOD for chunks beyond MidRenderDistanceXY. 2 = quarter-resolution silhouette (recommended)."))
    int32 DistantLOD = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming",
        meta=(ToolTip="Skylands XY radius (chunks). 20 chunks = 320m sky island coverage."))
    int32 SkylandsRenderDistanceXY = 20;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming",
        meta=(ToolTip="Extra Z chunks above skylands SkyAlt for full sky band. 8 = 128m sky coverage."))
    int32 SkylandsRenderDistanceZ = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Performance",
        meta=(ClampMin="4", ClampMax="128",
              ToolTip="Parallel chunk generation slots. 12 recommended — reduces memory contention and cache thrashing vs 48. Fewer concurrent tasks = faster individual completion."))
    int32 MaxConcurrentGenerations = 12;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|LOD",
        meta=(ToolTip="World distance (cm) at which LOD 0 transitions to LOD 1. 40000 = 400m (Zone A/B boundary)."))
    float LOD1Distance = 22400.f;   // = RenderDistanceXY * ChunkSize * VoxelSize

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|LOD",
        meta=(ToolTip="World distance (cm) at which LOD 1 transitions to LOD 2. 100000 = 1000m (Zone B/C boundary)."))
    float LOD2Distance = 64000.f;   // = MidRenderDistanceXY * ChunkSize * VoxelSize

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Materials")
    UMaterialInterface* MasterFlatMaterial  = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Materials")
    UMaterialInterface* MasterSlopeMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Materials")
    float SlopeThreshold = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Generation")
    FVoxelGenerationConfig GenerationConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Generation")
    TObjectPtr<UVoxelBiomeDataAsset> BiomePreset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Generation")
    bool bRandomizeSeedOnStartup = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Generation")
    bool bRegenerateViewportAfterPIE = true;

    FVoxelGenerationConfig GetEffectiveConfig() const
    {
        FVoxelGenerationConfig Out;
        if (BiomePreset != nullptr)
            Out = BiomePreset->Config;
        else
        {
            Out                = GenerationConfig;
            Out.ForestRender   = ForestRender;
            Out.PeaksRender    = PeaksRender;
            Out.CliffsRender   = CliffsRender;
            Out.MesaRender     = MesaRender;
            Out.CratersRender  = CratersRender;
            Out.DesertRender   = DesertRender;
            Out.OceanRender    = OceanRender;
            Out.SkylandsRender = SkylandsRender;
            Out.ForestWater    = ForestWater;
            Out.PeaksWater     = PeaksWater;
            Out.CliffsWater    = CliffsWater;
            Out.MesaWater      = MesaWater;
            Out.CratersWater   = CratersWater;
            Out.DesertWater    = DesertWater;
            Out.OceanWater     = OceanWater;
            Out.SkylandsWater  = SkylandsWater;
        }
        Out.Seed = GenerationConfig.Seed;
        return Out;
    }

private:
    mutable FVoxelGenerationConfig MergedConfig;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Generation")
    bool bAutoGenerateOnBeginPlay = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Spawn")
    bool bSpawnInNaturalCrater = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Spawn", meta=(ClampMin="1000.0"))
    float CraterSpawnSearchRadius = 60000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Spawn", meta=(ClampMin="100.0"))
    float CraterSpawnSearchStep = 4000.f;

    FIntVector WorldToChunkCoord(const FVector& WorldPos) const;
    FVector    ChunkCoordToWorld(const FIntVector& Coord)  const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Spawn", meta=(ClampMin="0.0", ClampMax="1.0"))
    float CraterSpawnMinWeight = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Spawn")
    float SafeSpawnHeightOffset = 200.f;

    UFUNCTION(CallInEditor, Category="Voxel") void GenerateWorld();
    UFUNCTION(CallInEditor, Category="Voxel") void ClearWorld();
    UFUNCTION(CallInEditor, Category="Voxel") void SnapPlayerToGround();
    UFUNCTION(CallInEditor, Category="Voxel|Presets") void SaveCurrentToPreset();
    UFUNCTION(CallInEditor, Category="Voxel|Presets") void LoadFromPreset();

    virtual void OnConstruction(const FTransform& Transform) override;

    UFUNCTION(BlueprintCallable, Category="Voxel|Interaction")
    void SetVoxelSphere(FVector WorldPosition, float Radius, float DensityValue, bool bRebuildChunks = true);

    UFUNCTION(BlueprintCallable, Category="Voxel|Testing") void RunVoxelTests();

    FVoxelDataMap* GetVoxelDataMap() { return &DataMap; }
    const TMap<FIntVector, AVoxelChunk*>* GetLoadedChunks() const { return &LoadedChunks; }
    int32 GetQueueCount() const { return GenerationQueue.Num(); }
    int32 GetQueueHead()  const { return QueueHead; }

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Persistence") FString SaveSlotName = TEXT("DefaultSlot");

    UFUNCTION(BlueprintCallable, Category="Voxel|Persistence") void SaveToFile(const FString& SlotName);
    UFUNCTION(BlueprintCallable, Category="Voxel|Persistence") void LoadFromFile(const FString& SlotName);
    UFUNCTION(CallInEditor, Category="Voxel|Actions") void RebuildWorld();
    UFUNCTION(CallInEditor, Category="Voxel|Actions") void ClearModifications();
    UFUNCTION(CallInEditor, Category="Voxel|Persistence", meta=(DisplayName="Save Slot")) void SaveDefaultSlot();
    UFUNCTION(CallInEditor, Category="Voxel|Persistence", meta=(DisplayName="Load Slot")) void LoadDefaultSlot();
    UFUNCTION(CallInEditor, Category="Voxel|Actions") void RunTests();
    void TestSmoothLODTransitions();

    UFUNCTION(BlueprintCallable, Category="Voxel|Terrain") float GetTerrainHeight(float X, float Y) const;
    float GetSurfaceZ(float X, float Y) const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Forest")  FVoxelBiomeRenderConfig ForestRender;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Forest")  FVoxelBiomeWaterConfig  ForestWater;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Peaks")   FVoxelBiomeRenderConfig PeaksRender;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Peaks")   FVoxelBiomeWaterConfig  PeaksWater;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Cliffs")  FVoxelBiomeRenderConfig CliffsRender;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Cliffs")  FVoxelBiomeWaterConfig  CliffsWater;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Mesa")    FVoxelBiomeRenderConfig MesaRender;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Mesa")    FVoxelBiomeWaterConfig  MesaWater;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Craters") FVoxelBiomeRenderConfig CratersRender;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Craters") FVoxelBiomeWaterConfig  CratersWater;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Desert")  FVoxelBiomeRenderConfig DesertRender;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Desert")  FVoxelBiomeWaterConfig  DesertWater;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Ocean")   FVoxelBiomeRenderConfig OceanRender;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Ocean")   FVoxelBiomeWaterConfig  OceanWater;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Skylands") FVoxelBiomeRenderConfig SkylandsRender;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Skylands") FVoxelBiomeWaterConfig  SkylandsWater;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Voxel") class UVoxelWaterComponent* WaterComponent = nullptr;
    UPROPERTY(EditAnywhere, Category="Voxel|Foliage|Legacy") UStaticMesh* TreeMesh  = nullptr;
    UPROPERTY(EditAnywhere, Category="Voxel|Foliage|Legacy") UStaticMesh* GrassMesh = nullptr;
    UPROPERTY(EditAnywhere, Category="Voxel|Foliage|Legacy", meta=(ClampMin="0.0", ClampMax="1.0")) float FoliageDensity  = 0.05f;
    UPROPERTY(EditAnywhere, Category="Voxel|Foliage|Legacy", meta=(ClampMin="0.0", ClampMax="1.0")) float MaxFoliageSlope = 0.8f;
    UPROPERTY(VisibleAnywhere, Category="Voxel") class USceneComponent* Root;

private:
    FVoxelDataMap DataMap;
    TMap<FIntVector, AVoxelChunk*> LoadedChunks;
    TSet<FIntVector>               EmptyChunks;
    // FIX-1: pending-set for visibility check — only chunks that just became ready.
    // ApplyMesh() adds coordinates here; CheckCloseRangeVisibility() drains it.
    // Avoids O(N loaded chunks) scan every frame.
    TSet<FIntVector>               ChunksNeedingVisibilityCheck;
    FVoxelChunkManager             ChunkManager;
    FVoxelChunkPool                ChunkPool;
    TArray<FIntVector>             DirtyRebuildQueue;
    void MarkChunkDirty(const FIntVector& Coord);

    TArray<FIntVector> GenerationQueue;
    int32              QueueHead = 0;
    TAtomic<int32>     ActiveGenerations{0};
    // PERF-4: populated once per DrainGenerationQueue call so ConfigureChunk
    // reads a const-ref instead of deep-copying the large config struct N times.
    mutable FVoxelGenerationConfig CachedEffectiveConfig;

    FVector LastStreamedPos = FVector::ZeroVector;
    float   StreamingTimer  = 0.f;
    static constexpr float StreamingInterval = 0.25f;

    float   CachedSkyAltWorld = 0.f;
    float   CachedCurvedH     = 0.f;
    float   CachedCurvedR     = 0.f;
    FVector LastSkyAltPos     = FVector(1e9f);
    static constexpr float SkyAltSnapDist = 1000.f;

    bool bInitialized = false;
    FThreadSafeBool bShutdown{false};
    TAtomic<bool> bGenerationActive{false};

    TUniquePtr<FVoxelDensityGenerator> DensityGenerator;

    UPROPERTY(VisibleAnywhere, Category="Voxel|Water")
    class UVoxelWorldWaterComponent* WaterSystemComponent = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Voxel|Streaming")
    class UVoxelStreamingComponent* StreamingComponent = nullptr;

    UPROPERTY(VisibleAnywhere, Category="Voxel|Spawn")
    class UVoxelSpawnHandlerComponent* SpawnHandlerComponent = nullptr;


    void ProcessInitialPlayerSpawn();

    bool  bWaitingForInitialSpawn = false;
    TSet<FIntVector> InitialSpawnCoords;
    TSet<FIntVector> InitialSpawnCoords_Visual;
    // PERF Fix #3: Counters incremented by OnGenerationComplete to avoid O(N)
    // per-frame scan of InitialSpawnCoords (up to 6900 entries × every frame).
    TAtomic<int32> InitialSpawnCollisionReadyCount{0};
    TAtomic<int32> InitialSpawnVisualReadyCount{0};
    float TargetCoordsZ       = 0.f;
    bool  bSkylandFoundBackup = false;
    float CachedSurfaceHeight = 0.f;
    FVector SpawnTargetPos    = FVector::ZeroVector;
    float SpawnWaitAccum      = 0.f;
    float SpawnDelayAccum     = 0.f;
    static constexpr float SpawnHoldDelay = 2.f;

    FVector SnapToVoxelGrid         (const FVector& WorldPos) const;
    FVector FindCraterSpawnLocation  (const FVector& StartPos, const FVoxelGenerationConfig& Config) const;
    float   GetSafeSpawnHeightOffset () const;
    void RandomizeSeed();

public:
    void GenerateWorldDeferred();
    UFUNCTION(BlueprintPure, Category="Voxel") bool  IsWaitingForInitialSpawn() const { return bWaitingForInitialSpawn; }
    UFUNCTION(BlueprintPure, Category="Voxel") float GetGenerationProgress()    const;
    UFUNCTION(BlueprintPure, Category="Voxel") FString GetGenerationStatusString() const;
    int32 GetInitialSpawnReadyCount() const { return InitialSpawnCollisionReadyCount.Load(); }
    int32 GetInitialSpawnTotalCount() const { return InitialSpawnCoords.Num(); }

private:
    void SpawnChunk        (const FIntVector& Coord, bool bSyncCollision = false);
    void DestroyChunk      (const FIntVector& Coord);
    void RebuildChunk      (const FIntVector& Coord);
    void UpdateChunkStreaming();
    void DrainGenerationQueue();
    void DiscoverExistingChunks();

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    FTSTicker::FDelegateHandle DrainTickerHandle;
    FTSTicker::FDelegateHandle DeferTickerHandle;
#endif

    void ConfigureChunk                         (AVoxelChunk* Chunk) const;
    void PerformWorldDiscoveryAndBoundsCalculation();
    void FinalizeGenerationSetup();
    void CheckCloseRangeVisibility();
};
