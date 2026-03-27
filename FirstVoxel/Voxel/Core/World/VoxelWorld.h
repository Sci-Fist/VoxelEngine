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

/** Entry for the spatial priority generation queue. */
struct FVoxelGenerationQueueEntry
{
    FIntVector Coord;
    float Priority;

    FVoxelGenerationQueueEntry() : Coord(0), Priority(0.f) {}
    FVoxelGenerationQueueEntry(FIntVector InCoord, float InPriority) : Coord(InCoord), Priority(InPriority) {}

    // Max-Heap behavior: highest priority at the top.
    bool operator<(const FVoxelGenerationQueueEntry& Other) const { return Priority < Other.Priority; }
};

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

 public:
    AVoxelWorld();
    virtual ~AVoxelWorld();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|World")
    int32 ChunkSize = 16;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|World")
    float VoxelSize = 100.f;

    // ── Render Distance — Three-zone system ──────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming")
    int32 RenderDistanceXY = 16;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming")
    int32 RenderDistanceZ = 16;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming")
    int32 MidRenderDistanceXY = 24;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming")
    int32 MidRenderDistanceZ = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming")
    int32 DistantRenderDistanceXY = 128;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming")
    int32 DistantLOD = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming")
    int32 SkylandsRenderDistanceXY = 48;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Streaming")
    int32 SkylandsRenderDistanceZ = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Performance")
    int32 MaxConcurrentGenerations = 12;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|LOD")
    float LOD1Distance = 22400.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|LOD")
    float LOD2Distance = 38400.f;

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

    FVoxelGenerationConfig GetEffectiveConfig() const;

private:
    mutable FVoxelGenerationConfig MergedConfig;

public:
    FORCEINLINE class UVoxelSpawnHandlerComponent* GetSpawnHandlerComponent() const { return SpawnHandlerComponent; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Generation")
    bool bAutoGenerateOnBeginPlay = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Spawn")
    bool bSpawnInNaturalCrater = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Spawn")
    float CraterSpawnSearchRadius = 60000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Spawn")
    float CraterSpawnSearchStep = 4000.f;

    FIntVector WorldToChunkCoord(const FVector& WorldPos) const;
    FVector    ChunkCoordToWorld(const FIntVector& Coord)  const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Spawn")
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
    
    mutable FRWLock LoadedChunksLock;

    int32 GetQueueCount() const { return GenerationQueue.Num(); }

    bool ContainsEmptyChunk(const FIntVector& Coord) const { return EmptyChunks.Contains(Coord); }
    const TSet<FIntVector>& GetEmptyChunks() const { return EmptyChunks; }

    void SetGenerationQueue(const TArray<FVoxelGenerationQueueEntry>& InQueue) { GenerationQueue = InQueue; }
    const TArray<FVoxelGenerationQueueEntry>& GetGenerationQueue() const { return GenerationQueue; }

    void ClearEmptyChunksInRange(int32 MinZ, int32 MaxZ);

    struct FVoxelDensityGenerator* GetDensityGenerator() const { return DensityGenerator.Get(); }
    FVector GetSpawnTargetPos() const { return SpawnTargetPos; }

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Persistence") FString SaveSlotName = TEXT("DefaultSlot");

    UFUNCTION(BlueprintCallable, Category="Voxel|Persistence") void SaveToFile(const FString& SlotName);
    UFUNCTION(BlueprintCallable, Category="Voxel|Persistence") void LoadFromFile(const FString& SlotName);
    UFUNCTION(CallInEditor, Category="Voxel|Actions") void RebuildWorld();
    UFUNCTION(CallInEditor, Category="Voxel|Actions") void ClearModifications();
    UFUNCTION(CallInEditor, Category="Voxel|Persistence") void SaveDefaultSlot();
    UFUNCTION(CallInEditor, Category="Voxel|Persistence") void LoadDefaultSlot();
    UFUNCTION(CallInEditor, Category="Voxel|Actions") void RunTests();
    void TestSmoothLODTransitions();

    UFUNCTION(BlueprintCallable, Category="Voxel|Terrain") float GetTerrainHeight(float X, float Y) const;
    float GetSurfaceZ(float X, float Y) const;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Voxel") class UVoxelWaterComponent* WaterComponent = nullptr;
    UPROPERTY(EditAnywhere, Category="Voxel|Foliage|Legacy") UStaticMesh* TreeMesh  = nullptr;
    UPROPERTY(EditAnywhere, Category="Voxel|Foliage|Legacy") UStaticMesh* GrassMesh = nullptr;
    UPROPERTY(EditAnywhere, Category="Voxel|Foliage|Legacy") float FoliageDensity  = 0.05f;
    UPROPERTY(EditAnywhere, Category="Voxel|Foliage|Legacy") float MaxFoliageSlope = 0.8f;
    UPROPERTY(VisibleAnywhere, Category="Voxel") class USceneComponent* Root;

private:
    FVoxelDataMap DataMap;
    TMap<FIntVector, AVoxelChunk*> LoadedChunks;
    TSet<FIntVector>               EmptyChunks;
    TSet<FIntVector>               ChunksNeedingVisibilityCheck;
    FVoxelChunkManager             ChunkManager;
    UPROPERTY()
    FVoxelChunkPool                ChunkPool;
    TArray<FIntVector>             DirtyRebuildQueue;
    void MarkChunkDirty(const FIntVector& Coord);

    TArray<FVoxelGenerationQueueEntry> GenerationQueue;
    TSet<class AVoxelChunk*>           ActiveChunkGenerations;
    TAtomic<int32>                     ActiveGenerations{0};
    mutable FVoxelGenerationConfig CachedEffectiveConfig;

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
    FVector SpawnTargetPos    = FVector::ZeroVector;
    static constexpr float SpawnHoldDelay = 2.f;

    FVector FindCraterSpawnLocation  (const FVector& StartPos, const FVoxelGenerationConfig& Config) const;
    void RandomizeSeed();

public:
    void GenerateWorldDeferred();

    FVector SnapToVoxelGrid         (const FVector& WorldPos) const;
    float   GetSafeSpawnHeightOffset () const;

    void SpawnChunk        (FIntVector Coord, bool bSyncCollision = false);
    void DestroyChunk      (const FIntVector& Coord);
    void RebuildChunk      (const FIntVector& Coord);
    UFUNCTION(BlueprintPure, Category="Voxel") bool IsWaitingForInitialSpawn() const;
    
    UFUNCTION(BlueprintPure, Category="Voxel") float GetGenerationProgress()    const;
    UFUNCTION(BlueprintPure, Category="Voxel") FString GetGenerationStatusString() const;

private:
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

    // Spawn HUD stats
    TSet<FIntVector> InitialSpawnCoords;
    TAtomic<int32> InitialSpawnCollisionReadyCount{0};
    TAtomic<int32> InitialSpawnVisualReadyCount{0};
    bool bWaitingForInitialSpawn = false;
public:
    int32 GetInitialSpawnReadyCount() const { return InitialSpawnCollisionReadyCount.Load(); }
    int32 GetInitialSpawnTotalCount() const { return InitialSpawnCoords.Num(); }
};
