#include "VoxelWaterComponent.h"
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

	AActor* Owner = GetOwner();
	if (!Owner || !OceanPlaneMesh) return;

	OceanComponent = NewObject<UStaticMeshComponent>(Owner, TEXT("ImplicitOceanComponent"));
	if (OceanComponent)
	{
		OceanComponent->SetupAttachment(Owner->GetRootComponent());
		OceanComponent->RegisterComponent();
		OceanComponent->SetStaticMesh(OceanPlaneMesh);
		
		if (OceanMaterial)
		{
			OceanComponent->SetMaterial(0, OceanMaterial);
		}

		// Set to absolute SeaLevel Height position
		OceanComponent->SetWorldLocation(FVector(0.f, 0.f, SeaLevel));
		
		// Set scale to cover visible world radius
		OceanComponent->SetWorldScale3D(FVector(10000.f, 10000.f, 1.f)); 
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
