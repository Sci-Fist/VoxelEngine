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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Water")
	UStaticMesh* OceanPlaneMesh = nullptr;

	/** Material instance applied to the ocean plane geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Water")
	UMaterialInterface* OceanMaterial = nullptr;

	/** Physical Altitude height coordinate (Z) where the water surface plane sits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Water")
	float SeaLevel = 0.f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	/** The rendered plane component spawned dynamically under the owner actor. */
	UPROPERTY()
	UStaticMeshComponent* OceanComponent = nullptr;
};
