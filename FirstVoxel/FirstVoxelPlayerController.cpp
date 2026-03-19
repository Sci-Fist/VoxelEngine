// FirstVoxelPlayerController.cpp
#include "FirstVoxelPlayerController.h"
#include "UI/VoxelMapWidget.h"
#include "Voxel/Core/World/VoxelWorld.h"
#include "Voxel/Core/VoxelChunk.h"

#include "CoreMinimal.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "DrawDebugHelpers.h"
#include "FirstVoxelCharacter.h"

AFirstVoxelPlayerController::AFirstVoxelPlayerController()
{
}

void AFirstVoxelPlayerController::BeginPlay()
{
    Super::BeginPlay();

    if (ULocalPlayer* LP = GetLocalPlayer())
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
        {
            if (MappingContext)
                Subsystem->AddMappingContext(MappingContext, 0);
        }

        FInputModeGameOnly InputMode;
        SetInputMode(InputMode);
        bShowMouseCursor = false;
    }

    if (CrosshairWidgetClass)
    {
        // Comment out UserWidget overlay as it covers the Canvas HUD text
        // if (UUserWidget* HUD = CreateWidget<UUserWidget>(this, CrosshairWidgetClass))
        //     HUD->AddToViewport();
    }

    // Pre-create the map widget so the first open has no hitch
    EnsureMapWidget();
}

void AFirstVoxelPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
}

// ============================================================
//  Map toggle
// ============================================================
void AFirstVoxelPlayerController::EnsureMapWidget()
{
    if (MapWidgetInstance && MapWidgetInstance->IsValidLowLevel()) return;

    // Default to the pure-C++ widget class if none is assigned in the Blueprint.
    // Use UClass* to avoid TSubclassOf<T> vs UClass* ternary ambiguity (MSVC C2874).
    UClass* ClassToUse = MapWidgetClass
        ? static_cast<UClass*>(MapWidgetClass)
        : UVoxelMapWidget::StaticClass();

    MapWidgetInstance = CreateWidget<UVoxelMapWidget>(this, ClassToUse);
    if (MapWidgetInstance)
        MapWidgetInstance->AddToViewport(10); // High ZOrder so it draws over game HUD
}

AVoxelWorld* AFirstVoxelPlayerController::FindVoxelWorld() const
{
    if (!GetWorld()) return nullptr;
    
    // Use TActorIterator safely with proper null checking
    for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
    {
        if (It && *It)
        {
            return *It; // Return the first valid found
        }
    }
    return nullptr;
}

void AFirstVoxelPlayerController::ToggleMap()
{
    EnsureMapWidget();
    if (!MapWidgetInstance) return;

    if (MapWidgetInstance->IsMapOpen())
    {
        CloseMap();
    }
    else
    {
        OpenMap();
    }
}

void AFirstVoxelPlayerController::OpenMap()
{
    EnsureMapWidget();
    if (!MapWidgetInstance) return;

    AVoxelWorld* World      = FindVoxelWorld();
    APawn*       LocalPawn  = GetPawn();  // renamed to avoid shadowing APlayerController::Pawn
    if (!World || !LocalPawn) return;

    MapWidgetInstance->OpenMap(World, LocalPawn);

    // Switch to UI input mode so the widget can receive key events (M/Escape to close)
    FInputModeUIOnly UIMode;
    UIMode.SetWidgetToFocus(MapWidgetInstance->TakeWidget());
    UIMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(UIMode);
    bShowMouseCursor = false; // Keep cursor hidden â€” map is keyboard-navigated only
}

void AFirstVoxelPlayerController::CloseMap()
{
    if (MapWidgetInstance)
        MapWidgetInstance->CloseMap();

    // Restore game-only input so player can move again immediately
    FInputModeGameOnly GameMode;
    SetInputMode(GameMode);
    bShowMouseCursor = false;
}

bool AFirstVoxelPlayerController::IsMapOpen() const
{
    return MapWidgetInstance && MapWidgetInstance->IsMapOpen();
}

// ============================================================
//  Voxel terrain interaction
// ============================================================
void AFirstVoxelPlayerController::ModifyVoxelTerrain(float InRadius, float InDensity)
{
    if (!bHasAimHit) return;

    AVoxelWorld* OwnerWorld = nullptr;
    if (AVoxelWorld* W = Cast<AVoxelWorld>(LastAimHit.GetActor()))
        OwnerWorld = W;
    else if (AVoxelChunk* C = Cast<AVoxelChunk>(LastAimHit.GetActor()))
        OwnerWorld = Cast<AVoxelWorld>(C->GetOwner());

    if (OwnerWorld)
        OwnerWorld->SetVoxelSphere(LastAimHit.ImpactPoint, InRadius, InDensity);
}

void AFirstVoxelPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);

    // [Expert Optimization] Reset raycast caching
    bHasAimHit = false;

    if (IsMapOpen()) return;

    APawn* P = GetPawn();
    if (!P || !GetWorld()) return;

    FVector ViewLoc;
    FRotator ViewRot;
    GetPlayerViewPoint(ViewLoc, ViewRot);

    const FVector End = ViewLoc + ViewRot.Vector() * 2000.f;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(P);

    bHasAimHit = GetWorld()->LineTraceSingleByChannel(LastAimHit, ViewLoc, End, ECC_Visibility, Params);

    if (bHasAimHit)
    {
        float VisRadius = 300.f;
        if (AFirstVoxelCharacter* Char = Cast<AFirstVoxelCharacter>(P))
            VisRadius = Char->InteractionRadius;

        DrawDebugSphere(GetWorld(), LastAimHit.ImpactPoint, VisRadius, 12, FColor::Red, false, -1, 0, 2.f);
    }
}
