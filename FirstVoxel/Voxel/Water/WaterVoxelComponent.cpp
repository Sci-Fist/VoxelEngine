// VoxelWaterComponent.cpp  [canonical location: Voxel/Water/]
#include "Voxel/Water/VoxelWaterComponent.h"
#include "Voxel/VoxelLogger.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "Components/StaticMeshComponent.h"

UVoxelWaterComponent::UVoxelWaterComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    bTickInEditor = false;
}

void UVoxelWaterComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!bEnableOcean)
    {
        UVoxelLogger::LogVoxelEvent(TEXT("VoxelWater: Ocean disabled."));
        return;
    }

    AActor* Owner = GetOwner();
    if (!Owner || !OceanPlaneMesh)
    {
        UVoxelLogger::LogVoxelEvent(TEXT("VoxelWater: No owner or OceanPlaneMesh — skipping ocean."));
        return;
    }

    OceanComponent = NewObject<UStaticMeshComponent>(Owner, TEXT("ImplicitOceanComponent"));
    if (!OceanComponent) return;

    OceanComponent->SetupAttachment(Owner->GetRootComponent());
    OceanComponent->RegisterComponent();
    OceanComponent->SetStaticMesh(OceanPlaneMesh);
    OceanComponent->SetMobility(EComponentMobility::Movable);

    if (OceanMaterial)
        OceanComponent->SetMaterial(0, OceanMaterial);

    OceanComponent->SetWorldLocation(FVector(0.f, 0.f, SeaLevel));
    const float Scale = FMath::Max(100.f, OceanPlaneScale);
    OceanComponent->SetWorldScale3D(FVector(Scale, Scale, 1.f));

    UVoxelLogger::LogVoxelEvent(FString::Printf(
        TEXT("VoxelWater: Ocean created at Z=%.0f scale=%.0f"), SeaLevel, Scale));
}

void UVoxelWaterComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                          FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!OceanComponent) return;

    APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
    if (!Player) return;

    // Follow player on XY, stay pinned to sea level on Z.
    const FVector Pos = Player->GetActorLocation();
    OceanComponent->SetWorldLocation(FVector(Pos.X, Pos.Y, SeaLevel));

    // Hide when inside a cave or underground so the plane doesn't clip through terrain.
    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(GetOwner());
    Params.AddIgnoredActor(Player);

    const bool bHitCeiling = GetWorld()->LineTraceSingleByChannel(
        Hit, Pos, Pos + FVector(0.f, 0.f, 20000.f), ECC_WorldStatic, Params);

    OceanComponent->SetVisibility(!bHitCeiling);
}
