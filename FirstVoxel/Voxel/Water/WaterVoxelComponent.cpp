// VoxelWaterComponent.cpp
// 
// Water system implementation for the voxel engine, providing both ocean surface
// rendering and integrated water simulation capabilities.
//
// ARCHITECTURE OVERVIEW:
// This component manages the visual representation of water in the voxel world,
// including a dynamic ocean plane that follows the player and automatic visibility
// management to prevent clipping through terrain. It works in conjunction with
// the VoxelWaterSimulator for cellular automata-based water flow simulation.
//
// KEY FEATURES:
// - Dynamic ocean plane that follows player movement
// - Automatic visibility culling when underground/cave environments
// - Configurable sea level and ocean plane scaling
// - Integration with voxel terrain for proper water-terrain interaction
//
// PERFORMANCE CHARACTERISTICS:
// - Uses a single static mesh component for the entire ocean surface
// - Dynamic positioning avoids mesh regeneration
// - Raycasting for visibility determination is optimized with collision filtering
// - Component is only active when ocean rendering is enabled

#include "Voxel/Water/VoxelWaterComponent.h"
#include "Voxel/VoxelLogger.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "Components/StaticMeshComponent.h"

UVoxelWaterComponent::UVoxelWaterComponent()
{
    // Configure component to tick for dynamic ocean behavior
    // Ticking is disabled in editor to prevent unnecessary updates during design
    PrimaryComponentTick.bCanEverTick = true;
    bTickInEditor = false;
    
    // Default configuration values
    SeaLevel = 64.0f;                    // Default sea level height
    OceanPlaneScale = 1000.0f;           // Default ocean plane scale in world units
    bEnableOcean = true;                 // Enable ocean rendering by default
}

void UVoxelWaterComponent::BeginPlay()
{
    Super::BeginPlay();

    // Early exit if ocean rendering is disabled
    if (!bEnableOcean)
    {
        UVoxelLogger::LogVoxelEvent(TEXT("VoxelWater: Ocean rendering disabled."));
        return;
    }

    // Validate component prerequisites
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        UVoxelLogger::LogVoxelEvent(TEXT("VoxelWater: No owner actor found — skipping ocean creation."));
        return;
    }
    
    if (!OceanPlaneMesh)
    {
        UVoxelLogger::LogVoxelEvent(TEXT("VoxelWater: No OceanPlaneMesh specified — skipping ocean creation."));
        return;
    }

    // Create and configure the ocean static mesh component
    // The ocean is implemented as a single large plane mesh that follows the player
    OceanComponent = NewObject<UStaticMeshComponent>(Owner, TEXT("ImplicitOceanComponent"));
    if (!OceanComponent) 
    {
        UVoxelLogger::LogVoxelEvent(TEXT("VoxelWater: Failed to create ocean component."));
        return;
    }

    // Attach ocean component to owner and configure basic properties
    OceanComponent->SetupAttachment(Owner->GetRootComponent());
    OceanComponent->RegisterComponent();
    OceanComponent->SetStaticMesh(OceanPlaneMesh);
    OceanComponent->SetMobility(EComponentMobility::Movable);

    // Apply material if specified
    if (OceanMaterial)
        OceanComponent->SetMaterial(0, OceanMaterial);

    // Position and scale the ocean plane
    // Z position is set to sea level, XY is centered at origin initially
    OceanComponent->SetWorldLocation(FVector(0.f, 0.f, SeaLevel));
    
    // Ensure minimum scale for visibility, then apply configured scale
    const float Scale = FMath::Max(100.f, OceanPlaneScale);
    OceanComponent->SetWorldScale3D(FVector(Scale, Scale, 1.f));

    UVoxelLogger::LogVoxelEvent(FString::Printf(
        TEXT("VoxelWater: Ocean component created at Z=%.0f with scale=%.0f"), SeaLevel, Scale));
}

void UVoxelWaterComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                          FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    // Safety check - ensure ocean component exists
    if (!OceanComponent) return;

    // Get player reference for dynamic positioning
    APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
    if (!Player) return;

    // DYNAMIC POSITIONING:
    // Move ocean plane to follow player on XY plane while maintaining
    // constant Z position at sea level. This creates the illusion of
    // an infinite ocean without requiring a massive static mesh.
    const FVector PlayerPos = Player->GetActorLocation();
    OceanComponent->SetWorldLocation(FVector(PlayerPos.X, PlayerPos.Y, SeaLevel));

    // UNDERGROUND VISIBILITY CULLING:
    // Perform raycast upward from player to detect if they're underground
    // or in a cave. If ceiling is detected, hide the ocean plane to prevent
    // visual clipping through terrain. This is essential for maintaining
    // immersion in cave systems and underground areas.
    
    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(GetOwner());    // Ignore the component's owner
    Params.AddIgnoredActor(Player);       // Ignore the player pawn
    
    // Raycast upward 20km to detect any static world geometry (terrain, caves)
    const bool bHitCeiling = GetWorld()->LineTraceSingleByChannel(
        Hit, 
        PlayerPos, 
        PlayerPos + FVector(0.f, 0.f, 20000.f), 
        ECC_WorldStatic, 
        Params);

    // Set visibility based on whether we hit terrain above us
    // If bHitCeiling is true, we're underground/cave -> hide ocean
    // If bHitCeiling is false, we're in open air -> show ocean
    OceanComponent->SetVisibility(!bHitCeiling);
}
