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
    void ClearState();

    bool IsWaitingForInitialSpawn() const { return bWaitingForInitialSpawn; }
    void SetWaitingForInitialSpawn(bool bWait) { bWaitingForInitialSpawn = bWait; }

    int32 GetCollisionReadyCount() const { return CachedCollisionReadyCount; }
    int32 GetVisualReadyCount()    const { return CachedVisualReadyCount; }

    int32 GetTotalCollisionCount() const { return InitialSpawnCoords.Num(); }
    int32 GetTotalVisualCount() const { return InitialSpawnCoords_Visual.Num(); }

    void ResetCounters() { CachedCollisionReadyCount = 0; CachedVisualReadyCount = 0; }

    bool ContainsCoord(const FIntVector& Coord) const { return InitialSpawnCoords.Contains(Coord) || InitialSpawnCoords_Visual.Contains(Coord); }
    bool ContainsCollisionCoord(const FIntVector& Coord) const { return InitialSpawnCoords.Contains(Coord); }
    bool ContainsVisualCoord(const FIntVector& Coord) const { return InitialSpawnCoords_Visual.Contains(Coord); }

    void ClearSpawnCoords() { InitialSpawnCoords.Empty(); InitialSpawnCoords_Visual.Empty(); }
    
    // Allow Streaming to add to the generation queue
    const TSet<FIntVector>& GetInitialSpawnCoords() const { return InitialSpawnCoords; }
    const TSet<FIntVector>& GetInitialSpawnCoordsVisual() const { return InitialSpawnCoords_Visual; }


private:
    bool bWaitingForInitialSpawn = false;
    
    TSet<FIntVector> InitialSpawnCoords;
    TSet<FIntVector> InitialSpawnCoords_Visual;

    // Plain int32 — all access is on the GameThread (TickComponent + ProcessInitialPlayerSpawn).
    // TAtomic was unnecessary overhead and caused operator=(int32) compile errors.
    int32 CachedCollisionReadyCount = 0;
    int32 CachedVisualReadyCount    = 0;

    float SpawnWaitAccum = 0.f;
    float SpawnDelayAccum = 0.f;

    /** Maximum duration in seconds to wait for chunks before forcing a player drop. */
    static constexpr float MaxSpawnWaitTime = 900.0f;
    float TargetCoordsZ = 0.f;
    FVector2D ActualSpawnXY = FVector2D(0.f, 0.f);
    float CachedSurfaceHeight = 0.f;

    TWeakObjectPtr<AVoxelWorld> WorldOwner;
};
