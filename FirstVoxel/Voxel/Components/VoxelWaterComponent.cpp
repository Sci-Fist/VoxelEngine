#include "VoxelWaterComponent.h"
#include "Voxel/VoxelLogger.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "Components/StaticMeshComponent.h"

UVoxelWaterComponent::UVoxelWaterComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = false; // Runs only in gameplay to save editor overhead
}

void UVoxelWaterComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!bEnableOcean)
	{
		UVoxelLogger::LogVoxelEvent(TEXT("VoxelWater: Ocean disabled (bEnableOcean=false)."));
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner || !OceanPlaneMesh)
	{
		UVoxelLogger::LogVoxelEvent(TEXT("VoxelWater: No owner or OceanPlaneMesh, skipping ocean."));
		return;
	}

	OceanComponent = NewObject<UStaticMeshComponent>(Owner, TEXT("ImplicitOceanComponent"));
	if (OceanComponent)
	{
		UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelWater: Ocean created at Z=%.0f scale=%.0f"), SeaLevel, OceanPlaneScale));
		OceanComponent->SetupAttachment(Owner->GetRootComponent());
		OceanComponent->RegisterComponent();
		OceanComponent->SetStaticMesh(OceanPlaneMesh);

		if (OceanMaterial)
		{
			OceanComponent->SetMaterial(0, OceanMaterial);
		}

		OceanComponent->SetWorldLocation(FVector(0.f, 0.f, SeaLevel));
		const float Scale = FMath::Max(100.f, OceanPlaneScale);
		OceanComponent->SetWorldScale3D(FVector(Scale, Scale, 1.f));
		OceanComponent->SetMobility(EComponentMobility::Movable);
	}
}

void UVoxelWaterComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (OceanComponent)
	{
		APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
		if (Player)
		{
			FVector PlayerPos = Player->GetActorLocation();
			// Continuous translation gate: follow horizontal movement to provide infinite plane illusion
			OceanComponent->SetWorldLocation(FVector(PlayerPos.X, PlayerPos.Y, SeaLevel));
		}
	}
}
