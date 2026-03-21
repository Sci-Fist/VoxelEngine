//
// VoxelWorld.h
// FIX SafeSpawnHeightOffset: was 25000 (250m above surface!) → 200 (2m).
// Players were spawning 250m in the air and falling. With the crater anchor
// fix the crater floor is correctly anchored to terrain, so a small offset is enough.

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

UCLASS()
class FIRSTVOXEL_API AVoxelWorld : public AActor
{
    GENERATED_BODY()

public:
    AVoxelWorld();
    virtual ~AVoxelWorld();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|World")
    int32 ChunkSize = 16;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|World")
    float VoxelSize = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming")
    int32 RenderDistanceXY = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming")
    int32 SkylandsRenderDistanceXY = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming", meta=(ClampMin="0"))
    int32 DistantRenderDistanceXY = 24;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming", meta=(ClampMin="2", ClampMax="4"))
    int32 DistantLOD = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming")
    int32 RenderDistanceZ = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Performance")
    int32 MaxConcurrentGenerations = 6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|LOD")
    float LOD1Distance = 6000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|LOD")
    float LOD2Distance = 12000.f;

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
            Out.SkylandsRender = SkylandsRender;
            Out.ForestWater    = ForestWater;
            Out.PeaksWater     = PeaksWater;
            Out.CliffsWater    = CliffsWater;
            Out.MesaWater      = MesaWater;
            Out.CratersWater   = CratersWater;
            Out.DesertWater    = DesertWater;
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Spawn",
        meta=(ToolTip="When true, the player spawns inside a natural noise-crater."))
    bool bSpawnInNaturalCrater = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Spawn", meta=(ClampMin="1000.0"))
    float CraterSpawnSearchRadius = 60000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Spawn", meta=(ClampMin="100.0"))
    float CraterSpawnSearchStep = 4000.f;

    FIntVector WorldToChunkCoord(const FVector& WorldPos) const;
    FVector    ChunkCoordToWorld(const FIntVector& Coord)  const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Spawn",
              meta=(ClampMin="0.0", ClampMax="1.0"))
    float CraterSpawnMinWeight = 0.25f;

    // FIX SafeSpawnHeightOffset: was 25000 (250m!) — player spawned 250m in the air.
    // Now 200cm (2m) above terrain — just enough to clear the surface mesh,
    // with the hover-lock handling fine Z placement once collision is ready.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Spawn",
        meta=(ToolTip="Height above terrain surface to park the player while collision bakes (cm). 200 = 2m."))
    float SafeSpawnHeightOffset = 200.f;

    UFUNCTION(CallInEditor, Category="Voxel") void GenerateWorld();
    UFUNCTION(CallInEditor, Category="Voxel") void ClearWorld();
    UFUNCTION(CallInEditor, Category="Voxel") void SnapPlayerToGround();
    UFUNCTION(CallInEditor, Category="Voxel|Presets") void SaveCurrentToPreset();
    UFUNCTION(CallInEditor, Category="Voxel|Presets") void LoadFromPreset();

    virtual void OnConstruction(const FTransform& Transform) override;

    UFUNCTION(BlueprintCallable, Category="Voxel|Interaction")
    void SetVoxelSphere(FVector WorldPosition, float Radius, float DensityValue,
                        bool bRebuildChunks = true);

    UFUNCTION(BlueprintCallable, Category="Voxel|Testing") void RunVoxelTests();

    FVoxelDataMap* GetVoxelDataMap() { return &DataMap; }
    const TMap<FIntVector, AVoxelChunk*>* GetLoadedChunks() const { return &LoadedChunks; }
    int32 GetQueueCount() const { return GenerationQueue.Num(); }
    int32 GetQueueHead()  const { return QueueHead; }

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Persistence")
    FString SaveSlotName = TEXT("DefaultSlot");

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

    // ── Per-biome ─────────────────────────────────────────────────────────────
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Skylands") FVoxelBiomeRenderConfig SkylandsRender;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Biomes|Skylands") FVoxelBiomeWaterConfig  SkylandsWater;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Voxel")
    class UVoxelWaterComponent* WaterComponent = nullptr;

    UPROPERTY(EditAnywhere, Category="Voxel|Foliage|Legacy") UStaticMesh* TreeMesh  = nullptr;
    UPROPERTY(EditAnywhere, Category="Voxel|Foliage|Legacy") UStaticMesh* GrassMesh = nullptr;
    UPROPERTY(EditAnywhere, Category="Voxel|Foliage|Legacy", meta=(ClampMin="0.0", ClampMax="1.0")) float FoliageDensity = 0.05f;
    UPROPERTY(EditAnywhere, Category="Voxel|Foliage|Legacy", meta=(ClampMin="0.0", ClampMax="1.0")) float MaxFoliageSlope = 0.8f;

    UPROPERTY(VisibleAnywhere, Category="Voxel") class USceneComponent* Root;

private:
    FVoxelDataMap DataMap;
    TMap<FIntVector, AVoxelChunk*> LoadedChunks;
    TSet<FIntVector>               EmptyChunks;
    FVoxelChunkManager             ChunkManager;
    FVoxelChunkPool                ChunkPool;
    TArray<FIntVector>             DirtyRebuildQueue;

    void MarkChunkDirty(const FIntVector& Coord);

    TArray<FIntVector> GenerationQueue;
    int32              QueueHead = 0;
    TAtomic<int32>     ActiveGenerations{0};

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

    TUniquePtr<FVoxelDensityGenerator> DensityGenerator;

    UPROPERTY(VisibleAnywhere, Category="Voxel|Water")
    class UVoxelWorldWaterComponent* WaterSystemComponent = nullptr;

    void ProcessInitialPlayerSpawn();

    bool  bWaitingForInitialSpawn = false;
    TArray<FIntVector> InitialSpawnCoords;
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
