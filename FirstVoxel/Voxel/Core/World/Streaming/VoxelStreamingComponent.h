// VoxelStreamingComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoxelStreamingComponent.generated.h"

class AVoxelWorld;

UCLASS( ClassGroup=(Voxel), meta=(BlueprintSpawnableComponent) )
class FIRSTVOXEL_API UVoxelStreamingComponent : public UActorComponent
{
    GENERATED_BODY()

public:	
    UVoxelStreamingComponent();

protected:
    virtual void BeginPlay() override;

public:	
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UFUNCTION(BlueprintCallable, Category="Voxel|Streaming")
    void UpdateStreaming();

    UFUNCTION(BlueprintCallable, Category="Voxel|Streaming")
    void CheckCloseRangeVisibility();

    void AddChunkNeedingVisibilityCheck(const FIntVector& Coord);
    void ClearVisibilityChecks();

private:
    float StreamingTimer = 0.f;
    static constexpr float StreamingInterval = 0.25f;

    FVector LastStreamedPos = FVector::ZeroVector;
    FVector LastSkyAltPos = FVector(1e9f);

    float CachedSkyAltWorld = 0.f;
    float CachedCurvedH = 0.f;
    float CachedCurvedR = 0.f;

    TSet<FIntVector> ChunksNeedingVisibilityCheck;

    TWeakObjectPtr<AVoxelWorld> WorldOwner;
};
