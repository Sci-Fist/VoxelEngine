// VoxelWaterComponent.h
// Added FIX #34 cache members (CeilingCheckTimer, LastCeilingCheckZ, bCachedHasCeiling)
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoxelWaterComponent.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

UCLASS(ClassGroup=(Voxel), meta=(BlueprintSpawnableComponent))
class FIRSTVOXEL_API UVoxelWaterComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UVoxelWaterComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water")
    UStaticMesh* OceanPlaneMesh = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water")
    UMaterialInterface* OceanMaterial = nullptr;

    /** Z of the water surface plane (cm). FIX #39: default matches world config (0). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water")
    float SeaLevel = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water")
    bool bEnableOcean = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water", meta=(ClampMin="100.0"))
    float OceanPlaneScale = 10000.f;

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;
protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY()
    UStaticMeshComponent* OceanComponent = nullptr;

    // FIX #34: cache for throttled ceiling visibility check
    float CeilingCheckTimer  = 0.f;
    float LastCeilingCheckZ  = 0.f;
    bool  bCachedHasCeiling  = false;
};
