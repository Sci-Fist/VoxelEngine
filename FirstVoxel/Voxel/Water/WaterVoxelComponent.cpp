// WaterVoxelComponent.cpp
//
// FIX #34 — Ocean visibility raycast was 200m every tick (60 Hz).
//            Now cached and re-evaluated at 4 Hz or on significant Z movement.
//
// FIX #39 — Default SeaLevel was 64.0f; the world config default is 0.0f.
//            Now defaults to 0.0f and syncs from AVoxelWorld on BeginPlay.

#include "Voxel/Water/VoxelWaterComponent.h"
#include "Voxel/VoxelLogger.h"
#include "Voxel/Core/World/VoxelWorld.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"

UVoxelWaterComponent::UVoxelWaterComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    bTickInEditor = false;

    SeaLevel       = 0.f;    // FIX #39: matches FVoxelGenerationConfig default
    OceanPlaneScale = 1000.f;
    bEnableOcean   = true;
}

void UVoxelWaterComponent::BeginPlay()
{
    Super::BeginPlay();

    // FIX #39: pull SeaLevel from the VoxelWorld actor if one exists
    for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
    {
        SeaLevel = It->GetEffectiveConfig().SeaLevel;
        break;
    }

    if (!bEnableOcean || !OceanPlaneMesh)
    {
        UVoxelLogger::LogVoxelEvent(TEXT("VoxelWater: Ocean disabled or no mesh — skipping."));
        return;
    }

    AActor* Owner = GetOwner();
    if (!Owner) return;

    OceanComponent = NewObject<UStaticMeshComponent>(Owner, TEXT("ImplicitOceanComponent"));
    if (!OceanComponent) return;

    OceanComponent->SetupAttachment(Owner->GetRootComponent());
    OceanComponent->RegisterComponent();
    OceanComponent->SetStaticMesh(OceanPlaneMesh);
    OceanComponent->SetMobility(EComponentMobility::Movable);
    if (OceanMaterial) OceanComponent->SetMaterial(0, OceanMaterial);

    const float Scale = FMath::Max(100.f, OceanPlaneScale);
    OceanComponent->SetWorldLocation(FVector(0.f, 0.f, SeaLevel));
    OceanComponent->SetWorldScale3D(FVector(Scale, Scale, 1.f));

    UVoxelLogger::LogVoxelEvent(FString::Printf(
        TEXT("VoxelWater: Ocean created at Z=%.0f scale=%.0f"), SeaLevel, Scale));
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

    // FIX #34: only re-run the 200m ceiling raycast at 4 Hz or on significant
    // vertical movement — not every frame at 60 Hz.
    CeilingCheckTimer     += DeltaTime;
    const float ZDelta     = FMath::Abs(PlayerPos.Z - LastCeilingCheckZ);
    const bool  bTimeElapsed = CeilingCheckTimer >= 0.25f;
    const bool  bMovedVert   = ZDelta > 200.f;

    if (bTimeElapsed || bMovedVert)
    {
        FHitResult Hit;
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(GetOwner());
        Params.AddIgnoredActor(Player);

        bCachedHasCeiling = GetWorld()->LineTraceSingleByChannel(
            Hit, PlayerPos, PlayerPos + FVector(0.f, 0.f, 20000.f),
            ECC_WorldStatic, Params);

        CeilingCheckTimer = 0.f;
        LastCeilingCheckZ = PlayerPos.Z;
    }

    OceanComponent->SetVisibility(!bCachedHasCeiling);
}
