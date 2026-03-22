// WaterVoxelComponent.cpp
//
// FIX #34 — Ocean visibility raycast cached at 4 Hz or on Z movement > 200cm.
// FIX #39 — Default SeaLevel = 0.f, syncs from AVoxelWorld on BeginPlay.
// FIX N-WATER-MESH — If no OceanPlaneMesh is assigned, auto-load the engine's
//   built-in Plane static mesh (/Engine/BasicShapes/Plane). Previously the
//   component silently skipped initialization when OceanPlaneMesh was null,
//   so water was invisible even when correctly configured.
//   The engine Plane mesh is 100x100 cm by default; we scale it up to
//   OceanPlaneScale * OceanPlaneScale cm to cover the visible horizon.

#include "Voxel/Water/VoxelWaterComponent.h"
#include "Voxel/VoxelLogger.h"
#include "Voxel/Core/World/VoxelWorld.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

UVoxelWaterComponent::UVoxelWaterComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    bTickInEditor = false;
    SeaLevel       = 0.f;
    OceanPlaneScale = 2000000.f;
    bEnableOcean   = true;
}

void UVoxelWaterComponent::BeginPlay()
{
    Super::BeginPlay();

    // FIX #39: pull SeaLevel from the VoxelWorld actor
    for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
    {
        SeaLevel = It->GetEffectiveConfig().SeaLevel;
        break;
    }

    if (!bEnableOcean)
    {
        UVoxelLogger::LogVoxelEvent(TEXT("VoxelWater: bEnableOcean=false — skipping ocean setup."));
        return;
    }

    // FIX N-WATER-MESH: auto-load built-in Plane mesh if nothing is assigned.
    // /Engine/BasicShapes/Plane is a 100×100 cm unit plane included with every
    // UE5 project. We scale it by OceanPlaneScale so it covers the horizon.
    if (!OceanPlaneMesh)
    {
        OceanPlaneMesh = Cast<UStaticMesh>(
            StaticLoadObject(UStaticMesh::StaticClass(), nullptr,
                             TEXT("/Engine/BasicShapes/Plane.Plane")));
        if (!OceanPlaneMesh)
        {
            UVoxelLogger::LogVoxelEvent(TEXT("VoxelWater: Could not load /Engine/BasicShapes/Plane. "
                "Assign OceanPlaneMesh in the Details panel for a visible ocean."));
            return;
        }
        UVoxelLogger::LogVoxelEvent(TEXT("VoxelWater: Using built-in Plane mesh for ocean surface."));
    }

    AActor* Owner = GetOwner();
    if (!Owner) return;

    OceanComponent = NewObject<UStaticMeshComponent>(Owner, TEXT("ImplicitOceanComponent"));
    if (!OceanComponent) return;

    OceanComponent->SetupAttachment(Owner->GetRootComponent());
    OceanComponent->RegisterComponent();
    OceanComponent->SetStaticMesh(OceanPlaneMesh);
    OceanComponent->SetMobility(EComponentMobility::Movable);
    OceanComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    OceanComponent->SetCastShadow(false);

    if (OceanMaterial)
        OceanComponent->SetMaterial(0, OceanMaterial);

    // Scale: engine Plane is 100x100 cm. Divide by 100 to get unit scale, then
    // multiply by OceanPlaneScale to get the desired coverage in cm.
    const float Scale = FMath::Max(100.f, OceanPlaneScale) / 100.f;
    OceanComponent->SetWorldLocation(FVector(0.f, 0.f, SeaLevel));
    OceanComponent->SetWorldScale3D(FVector(Scale, Scale, 1.f));

    UVoxelLogger::LogVoxelEvent(FString::Printf(
        TEXT("VoxelWater: Ocean plane created at Z=%.0f scale=%.0fx%.0f m"),
        SeaLevel, Scale, Scale));
}

void UVoxelWaterComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                          FActorComponentTickFunction* Tick)
{
    Super::TickComponent(DeltaTime, TickType, Tick);
    if (!OceanComponent) return;

    APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
    if (!Player) return;

    const FVector PlayerPos = Player->GetActorLocation();

    // Follow player XY at fixed sea level Z
    OceanComponent->SetWorldLocation(FVector(PlayerPos.X, PlayerPos.Y, SeaLevel));

    // FIX #34: throttle ceiling check to 4 Hz + significant Z movement
    CeilingCheckTimer += DeltaTime;
    const bool bTimeElapsed = CeilingCheckTimer >= 0.25f;
    const bool bMovedVert   = FMath::Abs(PlayerPos.Z - LastCeilingCheckZ) > 200.f;

    if (bTimeElapsed || bMovedVert)
    {
        FHitResult Hit;
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(GetOwner());
        Params.AddIgnoredActor(Player);

        // Shortened trace to 3000 cm (30m) so Skylands or high peaks don't trigger cave logic
        bCachedHasCeiling = GetWorld()->LineTraceSingleByChannel(
            Hit, PlayerPos, PlayerPos + FVector(0.f, 0.f, 3000.f),
            ECC_WorldStatic, Params);

        CeilingCheckTimer = 0.f;
        LastCeilingCheckZ = PlayerPos.Z;
    }

    // Hide when player is in a cave (ceiling close above) AND below sea level
    // This prevents seeing the infinite ocean plane through cave floors or mesh gaps.
    const bool bIsUnderground = bCachedHasCeiling && (PlayerPos.Z < SeaLevel + 1000.f);
    OceanComponent->SetVisibility(!bIsUnderground);
}
