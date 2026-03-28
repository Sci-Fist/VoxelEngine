#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Math/IntVector.h"
#include "Async/AsyncWork.h"
#include "VoxelWorldWater.generated.h"

class AVoxelWorld;
class AVoxelChunk;
class FVoxelWaterSimulator;
class UVoxelWaterComponent;
class FWaterSimAsyncTask;

UCLASS(ClassGroup=(Voxel), meta=(BlueprintSpawnableComponent))
class FIRSTVOXEL_API UVoxelWorldWaterComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UVoxelWorldWaterComponent();

    void Initialize(TUniquePtr<FVoxelWaterSimulator> InSim, UVoxelWaterComponent* InOcean);

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* Tick) override;

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    void InitChunkWater(AVoxelChunk* Chunk);
    void RemoveChunkFromWaterSimulation(const FIntVector& ChunkCoord);

    void RegisterWaterSource  (const FIntVector& WorldVoxelCoord);
    void UnregisterWaterSource(const FIntVector& WorldVoxelCoord);
    void ClearAllWaterSources ();

    void ResetWaterState();
    void ClearChunkWaterData(const FIntVector& ChunkCoord);
    bool IsChunkWaterDirty  (const FIntVector& ChunkCoord) const;

    FVoxelWaterSimulator* GetSimulator() const { return WaterSimulator.Get(); }

private:
    TUniquePtr<FVoxelWaterSimulator>   WaterSimulator;
    TWeakObjectPtr<UVoxelWaterComponent> OceanComponent;

    /** Background task currently executing a simulation step. */
    FAsyncTask<FWaterSimAsyncTask>* CurrentSimTask = nullptr;

    /** Results from the background thread waiting to be processed on the Game Thread. */
    TArray<FIntVector> QueuedDirtyChunks;

    // FIX #35: TSet for O(1) Add/Remove deduplication (was TArray with AddUnique → O(N))
    TSet<FIntVector> WaterSources;

    TMap<FIntVector, TArray<FIntVector>> ChunkWaterSources;

    bool  bWaterSimulationEnabled = true;
    float WaterSimTimer           = 0.f;
    float WaterSimInterval        = 0.25f; // FIX-3: raised from 0.2s; settled chunks skip anyway

    void UpdateWaterSimulation(float DeltaTime);
    void ProcessChunkWaterSources(AVoxelChunk* Chunk, const FIntVector& ChunkCoord);
    void RebuildWaterMeshForChunk(AVoxelChunk* Chunk);
};
