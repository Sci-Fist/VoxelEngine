// VoxelWaterComponent.h  [canonical location: Voxel/Water/]
// Actor component that manages the infinite ocean plane mesh.
// Follows the player, hides when inside a cave ceiling.
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

    /** Static Mesh asset used to render the infinite ocean plane. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water")
    UStaticMesh* OceanPlaneMesh = nullptr;

    /** Material instance applied to the ocean plane. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water")
    UMaterialInterface* OceanMaterial = nullptr;

    /** Z coordinate where the water surface plane sits (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water")
    float SeaLevel = 0.f;

    /** If false, the ocean plane is not created. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water")
    bool bEnableOcean = true;

    /** World-space XY scale of the ocean plane — should cover the full render area. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water", meta=(ClampMin="100.0"))
    float OceanPlaneScale = 10000.f;

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY()
    UStaticMeshComponent* OceanComponent = nullptr;
};
