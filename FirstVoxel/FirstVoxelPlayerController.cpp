// FirstVoxelPlayerController.cpp
//
// FIX N8 — PlayerTick no longer runs an unconditional line-trace every frame.
//           Previously: 2000cm physics raycast + debug sphere draw on every
//           tick, even when standing still with no tool active. This wasted
//           CPU time proportional to physics complexity near the player.
//           Fix: throttle to 12 Hz (every ~83 ms) and skip when map is open.
//
// FIX N11 — FindVoxelWorld() now caches the result instead of running a full
//            TActorIterator<AVoxelWorld> scan on every call. The iterator
//            iterates ALL actors in the level which is O(N_actors), not O(1).
//            Result is cached in CachedVoxelWorld (set in BeginPlay and on
//            first call if not yet set). Invalidated only in EndPlay.

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
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup    = TG_PrePhysics;
}

void AFirstVoxelPlayerController::BeginPlay()
{
    Super::BeginPlay();

    if (ULocalPlayer* LP = GetLocalPlayer())
    {
        if (UEnhancedInputLocalPlayerSubsystem* Sub =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
        {
            if (MappingContext) Sub->AddMappingContext(MappingContext, 0);
        }
        SetInputMode(FInputModeGameOnly());
        bShowMouseCursor = false;
    }

    // FIX N11: cache VoxelWorld once at BeginPlay rather than scanning every call
    for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
    {
        if (*It) { CachedVoxelWorld = *It; break; }
    }

    EnsureMapWidget();
}

void AFirstVoxelPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    CachedVoxelWorld = nullptr; // FIX N11: invalidate cache on teardown
    Super::EndPlay(Reason);
}

void AFirstVoxelPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
}

// ── Map widget ────────────────────────────────────────────────────────────────
void AFirstVoxelPlayerController::EnsureMapWidget()
{
    if (MapWidgetInstance && MapWidgetInstance->IsValidLowLevel()) return;
    UClass* Cls = MapWidgetClass
        ? static_cast<UClass*>(MapWidgetClass)
        : UVoxelMapWidget::StaticClass();
    MapWidgetInstance = CreateWidget<UVoxelMapWidget>(this, Cls);
    if (MapWidgetInstance) MapWidgetInstance->AddToViewport(10);
}

// FIX N11: O(1) cached lookup — no TActorIterator scan on every call
AVoxelWorld* AFirstVoxelPlayerController::FindVoxelWorld() const
{
    if (IsValid(CachedVoxelWorld)) return CachedVoxelWorld;
    // Fallback: scan once and cache (handles late-spawned worlds)
    for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
        if (*It) { const_cast<AFirstVoxelPlayerController*>(this)->CachedVoxelWorld = *It; return *It; }
    return nullptr;
}

void AFirstVoxelPlayerController::ToggleMap()
{
    EnsureMapWidget();
    if (!MapWidgetInstance) return;
    MapWidgetInstance->IsMapOpen() ? CloseMap() : OpenMap();
}

void AFirstVoxelPlayerController::OpenMap()
{
    EnsureMapWidget();
    if (!MapWidgetInstance) return;
    AVoxelWorld* World = FindVoxelWorld();
    APawn* P = GetPawn();
    if (!World || !P) return;
    MapWidgetInstance->OpenMap(World, P);
    FInputModeUIOnly UIMode;
    UIMode.SetWidgetToFocus(MapWidgetInstance->TakeWidget());
    UIMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(UIMode);
    bShowMouseCursor = false;
}

void AFirstVoxelPlayerController::CloseMap()
{
    if (MapWidgetInstance) MapWidgetInstance->CloseMap();
    SetInputMode(FInputModeGameOnly());
    bShowMouseCursor = false;
}

bool AFirstVoxelPlayerController::IsMapOpen() const
{
    return MapWidgetInstance && MapWidgetInstance->IsMapOpen();
}

// ── Terrain modification ──────────────────────────────────────────────────────
void AFirstVoxelPlayerController::ModifyVoxelTerrain(float InRadius, float InDensity)
{
    if (!bHasAimHit) return;
    AVoxelWorld* W = nullptr;
    if (AVoxelWorld* VW = Cast<AVoxelWorld>(LastAimHit.GetActor()))      W = VW;
    else if (AVoxelChunk* C = Cast<AVoxelChunk>(LastAimHit.GetActor()))  W = Cast<AVoxelWorld>(C->GetOwner());
    if (W) W->SetVoxelSphere(LastAimHit.ImpactPoint, InRadius, InDensity);
}

// ── PlayerTick — FIX N8: throttled to ~12 Hz ─────────────────────────────────
void AFirstVoxelPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);

    bHasAimHit = false;
    if (IsMapOpen()) return;

    APawn* P = GetPawn();
    if (!P || !GetWorld()) return;

    // FIX N8: throttle the physics raycast to ~12 Hz (every 83ms).
    // The old code fired unconditionally every frame — at 120 FPS that's
    // 120 physics traces/s just to show a debug sphere, even when idle.
    // 12 Hz is plenty for visual feedback and tool targeting.
    RaycastAccum += DeltaTime;
    static constexpr float RaycastInterval = 1.f / 12.f; // ~83 ms
    if (RaycastAccum < RaycastInterval) return;
    RaycastAccum = 0.f;

    FVector ViewLoc; FRotator ViewRot;
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
        DrawDebugSphere(GetWorld(), LastAimHit.ImpactPoint, VisRadius, 12, FColor::Red,
                        false, RaycastInterval * 1.5f, 0, 2.f);
    }
}
