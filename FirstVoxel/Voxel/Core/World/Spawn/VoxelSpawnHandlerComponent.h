// VoxelSpawnHandlerComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoxelSpawnHandlerComponent.generated.h"

class AVoxelWorld;

UCLASS( ClassGroup=(Voxel), meta=(BlueprintSpawnableComponent) )
class FIRSTVOXEL_API UVoxelSpawnHandlerComponent : public UActorComponent
{
    GENERATED_BODY()

public:	
    UVoxelSpawnHandlerComponent();

protected:
    virtual void BeginPlay() override;

public:	
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UFUNCTION(BlueprintCallable, Category="Voxel|Spawn")
    void ProcessInitialPlayerSpawn();

    bool IsWaitingForInitialSpawn() const { return bWaitingForInitialSpawn; }
    void SetWaitingForInitialSpawn(bool bWait) { bWaitingForInitialSpawn = bWait; }

    int32 GetCollisionReadyCount() const { return InitialSpawnCollisionReadyCount; }
    int32 GetVisualReadyCount() const { return InitialSpawnVisualReadyCount; }
    int32 GetTotalCollisionCount() const { return InitialSpawnCoords.Num(); }
    int32 GetTotalVisualCount() const { return InitialSpawnCoords_Visual.Num(); }

    void IncrementCollisionReady() { ++InitialSpawnCollisionReadyCount; }
    void IncrementVisualReady() { ++InitialSpawnVisualReadyCount; }

    bool ContainsCoord(const FIntVector& Coord) const { return InitialSpawnCoords.Contains(Coord) || InitialSpawnCoords_Visual.Contains(Coord); }
    bool ContainsCollisionCoord(const FIntVector& Coord) const { return InitialSpawnCoords.Contains(Coord); }
    bool ContainsVisualCoord(const FIntVector& Coord) const { return InitialSpawnCoords_Visual.Contains(Coord); }

    void ClearSpawnCoords() { InitialSpawnCoords.Empty(); InitialSpawnCoords_Visual.Empty(); }
    
    // Allow Streaming to add to the generation queue
    const TSet<FIntVector>& GetInitialSpawnCoords() const { return InitialSpawnCoords; }
    const TSet<FIntVector>& GetInitialSpawnCoordsVisual() const { return InitialSpawnCoords_Visual; }

    void ResetCounters() { InitialSpawnCollisionReadyCount = 0; InitialSpawnVisualReadyCount = 0; }

private:
    bool bWaitingForInitialSpawn = false;
    
    TSet<FIntVector> InitialSpawnCoords;
    TSet<FIntVector> InitialSpawnCoords_Visual;

    int32 InitialSpawnCollisionReadyCount = 0;
    int32 InitialSpawnVisualReadyCount = 0;

    float SpawnWaitAccum = 0.f;
    float SpawnDelayAccum = 0.f;
    float TargetCoordsZ = 0.f;
    float CachedSurfaceHeight = 0.f;

    TWeakObjectPtr<AVoxelWorld> WorldOwner;
};
