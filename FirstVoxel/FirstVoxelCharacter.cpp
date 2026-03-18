// Copyright Epic Games, Inc. All Rights Reserved.

#include "FirstVoxelCharacter.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

#include "CoreMinimal.h"
#include "FirstVoxel.h"
#include "FirstVoxelPlayerController.h"
#include "FirstVoxelHUD.h"
#include "Voxel/Core/World/VoxelWorld.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"

AFirstVoxelCharacter::AFirstVoxelCharacter()
{
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw   = false;
	bUseControllerRotationRoll  = false;

	GetCharacterMovement()->bOrientRotationToMovement    = true;
	GetCharacterMovement()->bUseControllerDesiredRotation = false;
	GetCharacterMovement()->RotationRate                 = FRotator(0.f, 500.f, 0.f);
	GetCharacterMovement()->bUseFlatBaseForFloorChecks   = true; // FIX: prevents Rounded capsule Slip on Voxel wedges
	GetCharacterMovement()->SetWalkableFloorAngle(60.0f);        // FIX: prevents slope slides from locking landing anims
	GetCharacterMovement()->AirControl                   = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed                 = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed           = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking   = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling   = 1500.f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength        = 400.f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetMesh(), TEXT("head"));
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, 100.f)); // Offset to eye level
	FirstPersonCamera->SetVisibility(false);

	// Initialize camera properties
	CameraTransitionSpeed = 5.0f;
	ThirdPersonDistance = 300.0f;
	ThirdPersonHeight = 100.0f;
	ThirdPersonLookAtOffset = 50.0f;

	// Set default camera mode to third person
	bIsFirstPerson = false;
	bIsThirdPerson = true;
}

void AFirstVoxelCharacter::BeginPlay()
{
	Super::BeginPlay();



	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			InputSubsystem->AddMappingContext(MappingContext, 0);
		}
	}

	// Reset dig/build timestamps so re-entering PIE never blocks the first action
	DigLastActionTime   = -1.f;
	BuildLastActionTime = -1.f;
	// FIX: Clear the cached VoxelWorld pointer on each BeginPlay.
	// Without this, PIE restart leaves a stale pointer to the destroyed actor
	// from the previous session, causing all tool raycasts to silently fail.
	CachedVoxelWorld = nullptr;

	// ── FIX: Disable Collision for Visual Attachment components ──────────
	// If a brush radius sphere or wireframe is added in Blueprints, it can
	// support the actor's weight and suspend the capsule in air, causing
	// continuous falling animation locks.
	TArray<UPrimitiveComponent*> PrimitiveComps;
	GetComponents<UPrimitiveComponent>(PrimitiveComps);
	for (UPrimitiveComponent* Comp : PrimitiveComps)
	{
		if (Comp && Comp != GetCapsuleComponent() && Comp != GetMesh())
		{
			Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			UE_LOG(LogTemplateCharacter, Log, TEXT("Disabled landing collision on widget: %s"), *Comp->GetName());
		}
	}
}

void AFirstVoxelCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Auto-load input assets when not pre-assigned via a Blueprint subclass
	if (!JumpAction)
		JumpAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
	if (!MoveAction)
		MoveAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
	if (!LookAction)
		LookAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Look.IA_Look"));
	if (!MouseLookAction)
		MouseLookAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_MouseLook.IA_MouseLook"));
	if (!MappingContext)
		MappingContext = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default"));
	if (!ToggleFlyAction)
		ToggleFlyAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Fly.IA_Fly"));
	if (!FlyDownAction)
		FlyDownAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_FlyDown.IA_FlyDown"));
	if (!MapAction)
		MapAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Map.IA_Map"));
	if (!PauseAction)
		PauseAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Pause.IA_Pause"));
	if (!ToggleCameraAction)
		ToggleCameraAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_ToggleCamera.IA_ToggleCamera"));

	if (UEnhancedInputComponent* EnhancedIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jump (keyboard/gamepad A)
		if (JumpAction)
		{
			EnhancedIC->BindAction(JumpAction, ETriggerEvent::Started,   this, &ACharacter::Jump);
			EnhancedIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
		}

		// Look / Move — guarded: raw key polling in Tick is the primary path anyway
		if (LookAction)      EnhancedIC->BindAction(LookAction,      ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::Look);
		if (MouseLookAction) EnhancedIC->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::Look);
		if (MoveAction)      EnhancedIC->BindAction(MoveAction,      ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::Move);

		// Sprint (keyboard Shift) — guarded: SprintAction is optional, not auto-loaded
		if (SprintAction)
		{
			EnhancedIC->BindAction(SprintAction, ETriggerEvent::Started,   this, &AFirstVoxelCharacter::Sprint);
			EnhancedIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &AFirstVoxelCharacter::StopSprinting);
		}

		// Dig / Build — optional; polling fallback in Tick handles LMB/RMB directly
		if (DigAction)   EnhancedIC->BindAction(DigAction,   ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::Dig);
		if (BuildAction) EnhancedIC->BindAction(BuildAction, ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::Build);

		// Flight toggle / map
		if (ToggleFlyAction)
			EnhancedIC->BindAction(ToggleFlyAction, ETriggerEvent::Started, this, &AFirstVoxelCharacter::ToggleFly);
		if (MapAction)
			EnhancedIC->BindAction(MapAction, ETriggerEvent::Started, this, &AFirstVoxelCharacter::ToggleMap);
		if (PauseAction)
			EnhancedIC->BindAction(PauseAction, ETriggerEvent::Started, this, &AFirstVoxelCharacter::TogglePauseMenu);

		// Explicit fly-down action (Ctrl)
		if (FlyDownAction)
			EnhancedIC->BindAction(FlyDownAction, ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::FlyDown);

		// Camera toggle (V key / Y button)
		if (ToggleCameraAction)
			EnhancedIC->BindAction(ToggleCameraAction, ETriggerEvent::Started, this, &AFirstVoxelCharacter::ToggleCameraMode);
	}

	// ── Keyboard/Mouse fallback bindings ────────────────────────────────────
	PlayerInputComponent->BindKey(EKeys::F,               IE_Pressed, this, &AFirstVoxelCharacter::ToggleFly);
	PlayerInputComponent->BindKey(EKeys::M,               IE_Pressed, this, &AFirstVoxelCharacter::ToggleMap);
	PlayerInputComponent->BindKey(EKeys::P,               IE_Pressed, this, &AFirstVoxelCharacter::TogglePauseMenu);
	PlayerInputComponent->BindKey(EKeys::V,               IE_Pressed, this, &AFirstVoxelCharacter::ToggleCameraMode);
	PlayerInputComponent->BindKey(EKeys::MouseScrollUp,   IE_Pressed, this, &AFirstVoxelCharacter::IncreaseRadius);
	PlayerInputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AFirstVoxelCharacter::DecreaseRadius);

	// Tool Wheel Bindings
	PlayerInputComponent->BindKey(EKeys::Q, IE_Pressed,  this, &AFirstVoxelCharacter::OpenToolWheel);
	PlayerInputComponent->BindKey(EKeys::Q, IE_Released, this, &AFirstVoxelCharacter::CloseToolWheel);
	PlayerInputComponent->BindKey(EKeys::Gamepad_LeftShoulder, IE_Pressed,  this, &AFirstVoxelCharacter::OpenToolWheel);
	PlayerInputComponent->BindKey(EKeys::Gamepad_LeftShoulder, IE_Released, this, &AFirstVoxelCharacter::CloseToolWheel);

	// ── Tool selection (Keyboard 1-4) ─────────────────────────────────────
	PlayerInputComponent->BindKey(EKeys::One,   IE_Pressed, this, &AFirstVoxelCharacter::SelectToolDig);
	PlayerInputComponent->BindKey(EKeys::Two,   IE_Pressed, this, &AFirstVoxelCharacter::SelectToolBuild);
	PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AFirstVoxelCharacter::SelectToolSmooth);
	PlayerInputComponent->BindKey(EKeys::Four,  IE_Pressed, this, &AFirstVoxelCharacter::SelectToolFlatten);

	// ── Auto-walk (keyboard R / gamepad Select) ─────────────────────────────
	PlayerInputComponent->BindKey(EKeys::R,                      IE_Pressed, this, &AFirstVoxelCharacter::ToggleAutoWalk);
	PlayerInputComponent->BindKey(EKeys::Gamepad_Special_Left,   IE_Pressed, this, &AFirstVoxelCharacter::ToggleAutoWalk);

	// ── Gamepad bindings ─────────────────────────────────────────────────────
	// B  → Toggle Flight
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Right, IE_Pressed, this, &AFirstVoxelCharacter::ToggleFly);
	// Y  → Toggle Map
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Top,   IE_Pressed, this, &AFirstVoxelCharacter::ToggleMap);
	// X  → Toggle Camera
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Left,  IE_Pressed, this, &AFirstVoxelCharacter::ToggleCameraMode);
	// Start → Pause menu (Start was previously bound to Map but is more
	// conventionally used for pause on consoles; Map stays on Y button only)
	PlayerInputComponent->BindKey(EKeys::Gamepad_Special_Right,    IE_Pressed, this, &AFirstVoxelCharacter::TogglePauseMenu);
	// RB → Increase Brush Radius
	PlayerInputComponent->BindKey(EKeys::Gamepad_RightShoulder,    IE_Pressed, this, &AFirstVoxelCharacter::IncreaseRadius);
	// LB → Decrease Brush Radius
	PlayerInputComponent->BindKey(EKeys::Gamepad_LeftShoulder,     IE_Pressed, this, &AFirstVoxelCharacter::DecreaseRadius);

	// ── Tool selection (D-Pad) ─────────────────────────────────────────────
	PlayerInputComponent->BindKey(EKeys::Gamepad_DPad_Up,    IE_Pressed, this, &AFirstVoxelCharacter::SelectToolDig);
	PlayerInputComponent->BindKey(EKeys::Gamepad_DPad_Right, IE_Pressed, this, &AFirstVoxelCharacter::SelectToolBuild);
	PlayerInputComponent->BindKey(EKeys::Gamepad_DPad_Down,  IE_Pressed, this, &AFirstVoxelCharacter::SelectToolSmooth);
	PlayerInputComponent->BindKey(EKeys::Gamepad_DPad_Left,  IE_Pressed, this, &AFirstVoxelCharacter::SelectToolFlatten);
}

// ---------------------------------------------------------------------------
void AFirstVoxelCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;

	// ── Pause guard: skip all gameplay input while the pause menu is open ─────
	// The pause menu uses FInputModeUIOnly which already blocks Enhanced Input
	// bindings, but the raw key-polling below (IsInputKeyDown / GetInputAnalogKeyState)
	// still reads hardware state. Skipping the Tick body prevents movement,
	// digging, and look from firing while the menu is visible.
	if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
		if (HUD->IsPaused() || HUD->bShowTitleScreen) return;

	// ── Input Device Detection ────────────────────────────────────────────
	const float GPLx = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	const float GPLy = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	const float GPRx = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
	const float GPRy = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY);
	const float GPRT = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis);
	const float GPLT = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis);

	const bool bGPActive =
		FMath::Abs(GPLx) > 0.1f || FMath::Abs(GPLy) > 0.1f ||
		FMath::Abs(GPRx) > 0.1f || FMath::Abs(GPRy) > 0.1f ||
		GPRT > 0.1f || GPLT > 0.1f ||
		PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom) ||
		PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Right)  ||
		PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Top)    ||
		PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Left)   ||
		PC->IsInputKeyDown(EKeys::Gamepad_RightShoulder)     ||
		PC->IsInputKeyDown(EKeys::Gamepad_LeftShoulder);

	const bool bKBMActive =
		FMath::Abs(PC->GetInputAnalogKeyState(EKeys::MouseX)) > 0.05f ||
		FMath::Abs(PC->GetInputAnalogKeyState(EKeys::MouseY)) > 0.05f ||
		PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::A) ||
		PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::D) ||
		PC->IsInputKeyDown(EKeys::LeftMouseButton) ||
		PC->IsInputKeyDown(EKeys::RightMouseButton);

	if      (bGPActive)  bLastInputWasGamepad = true;
	else if (bKBMActive) bLastInputWasGamepad = false;

	const bool bFlying = GetCharacterMovement() &&
		GetCharacterMovement()->MovementMode == MOVE_Flying;

	// ── 0. Auto-walk (cancelled by any manual move input) ───────────────────
	if (bAutoWalk)
	{
		// Cancel auto-walk if player pushes any movement controls
		if (!bLastInputWasGamepad)
		{
			if (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::S) ||
				PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::D))
			{
				bAutoWalk = false;
				UE_LOG(LogTemplateCharacter, Log, TEXT("Auto-walk cancelled by manual input"));
			}
		}
		else // Gamepad
		{
			if (FMath::Abs(GPLx) > 0.15f || FMath::Abs(GPLy) > 0.15f)
			{
				bAutoWalk = false;
				UE_LOG(LogTemplateCharacter, Log, TEXT("Auto-walk cancelled by manual input"));
			}
		}

		// If auto-walk is still active, move forward
		if (bAutoWalk)
		{
			DoMove(0.f, 1.f);
		}
	}

	// ── 1. Keyboard: Move (WASD) ──────────────────────────────────────────
	// Note: NOT duplicated in PlayerController::PlayerTick — that path was removed.
	if (!bLastInputWasGamepad)
	{
		if (PC->IsInputKeyDown(EKeys::W)) DoMove(0.f,  1.f);
		if (PC->IsInputKeyDown(EKeys::S)) DoMove(0.f, -1.f);
		if (PC->IsInputKeyDown(EKeys::A)) DoMove(-1.f, 0.f);
		if (PC->IsInputKeyDown(EKeys::D)) DoMove( 1.f, 0.f);

		// FIX: Q/E yaw-look removed — Q is bound to ToolWheel open, so using it
		// for look input caused the camera to spin every time the wheel opened.
		// Use mouse or right stick for looking instead.
	}

	// ── 2. Keyboard: Flight vertical (Space = rise, Ctrl = descend) ──────────
	// FIX: Only inject vertical movement when already flying. Space also binds
	// Jump (ACharacter::Jump) but MOVE_Flying mode ignores the jump impulse, so
	// polling IsInputKeyDown here is safe — it won't cause a double-jump on land.
	// LeftControl is polled here as a backup; FlyDown() via Input also
	// fires for Ctrl — both paths call AddMovementInput so there's no conflict.
	if (bFlying && !bLastInputWasGamepad)
	{
		if (PC->IsInputKeyDown(EKeys::SpaceBar))    AddMovementInput(FVector::UpVector,  1.f);
		if (PC->IsInputKeyDown(EKeys::LeftControl)) AddMovementInput(FVector::UpVector, -1.f);
		if (PC->IsInputKeyDown(EKeys::C))           AddMovementInput(FVector::UpVector, -1.f); // bonus: C to descend
	}



	// ── 3. Gamepad: Left Stick Move ──────────────────────────────────────
	if (FMath::Abs(GPLx) > 0.15f || FMath::Abs(GPLy) > 0.15f)
		DoMove(GPLx, GPLy);

	// ── 4. Gamepad: Right Stick Look ─────────────────────────────────────
	if (FMath::Abs(GPRx) > 0.1f || FMath::Abs(GPRy) > 0.1f)
	{
		if (bToolWheelOpen)
		{
			const float Angle = FMath::RadiansToDegrees(FMath::Atan2(-GPRy, GPRx));
			if (Angle >= -135.f && Angle < -45.f)      SelectToolByIndex(1); // Build (Top)
			else if (Angle >= -45.f && Angle < 45.f) SelectToolByIndex(2); // Smooth (Right)
			else if (Angle >= 45.f && Angle < 135.f) SelectToolByIndex(3); // Flatten (Bottom)
			else                                      SelectToolByIndex(0); // Dig (Left)
		}
		else
		{
			const float S = GamepadLookSensitivity * DeltaTime;
			DoLook(GPRx * S, GPRy * S);
			bLookedThisFrame = true;
		}
	}

	// ── 5. Gamepad: Flight vertical (A = up, X = down) ───────────────────
	if (bFlying)
	{
		if (PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom)) AddMovementInput(FVector::UpVector,  1.f);
		if (PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Left))   AddMovementInput(FVector::UpVector, -1.f);
	}

	// ── 6. Gamepad: Triggers → Dig (RT) / Build (LT) ─────────────────────
	if (GPRT > 0.3f || GPLT > 0.3f)
	{
		ApplyCurrentTool();
	}

	// ── 9. Mouse: LMB → Dig / RMB → Build ───────────────────────────────
	// Input bindings only fire when DigAction/BuildAction assets are assigned
	// in the Blueprint subclass. Polling here guarantees mouse dig/build always works
	// regardless of whether those assets are set up, matching the gamepad trigger path.
	if (!bLastInputWasGamepad)
	{
		if (PC->IsInputKeyDown(EKeys::LeftMouseButton))
		{
			CurrentTool = EVoxelToolMode::Dig;
			ApplyCurrentTool();
		}
		else if (PC->IsInputKeyDown(EKeys::RightMouseButton))
		{
			CurrentTool = EVoxelToolMode::Build;
			ApplyCurrentTool();
		}
	}

	// ── 7. Gamepad: LS Click → Sprint toggle ─────────────────────────────
	if (PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftThumbstick))
	{
		if (GetCharacterMovement())
		{
			const bool bIsSprinting = GetCharacterMovement()->MaxWalkSpeed > 600.f;
			GetCharacterMovement()->MaxWalkSpeed = bIsSprinting ? 500.f : 900.f;
		}
	}

	// ── 8. Mouse look fallback (when Input doesn't supply deltas) ─
	if (bToolWheelOpen && !bLastInputWasGamepad)
	{
		FVector2D ScreenSize;
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->GetViewportSize(ScreenSize);
			float MouseX, MouseY;
			if (PC->GetMousePosition(MouseX, MouseY))
			{
				const FVector2D Dir(MouseX - ScreenSize.X * 0.5f, MouseY - ScreenSize.Y * 0.5f);
				if (Dir.Size() > 20.f)
				{
					const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
					if (Angle >= -135.f && Angle < -45.f)      SelectToolByIndex(1); // Build (Top)
					else if (Angle >= -45.f && Angle < 45.f) SelectToolByIndex(2); // Smooth (Right)
					else if (Angle >= 45.f && Angle < 135.f) SelectToolByIndex(3); // Flatten (Bottom)
					else                                      SelectToolByIndex(0); // Dig (Left)
				}
			}
		}
	}
	else if (!bLookedThisFrame)
	{
		float MouseX, MouseY;
		PC->GetInputMouseDelta(MouseX, MouseY);
		if (FMath::Abs(MouseX) > 0.001f || FMath::Abs(MouseY) > 0.001f)
			DoLook(MouseX, MouseY * -1.f);
	}
	bLookedThisFrame = false;
}

// ---------------------------------------------------------------------------
void AFirstVoxelCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D V = Value.Get<FVector2D>();
	DoMove(V.X, V.Y);
}

void AFirstVoxelCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D V = Value.Get<FVector2D>();
	DoLook(V.X, V.Y);
	bLookedThisFrame = true;
}

void AFirstVoxelCharacter::DoMove(float Right, float Forward)
{
	if (!GetController()) return;
	const FRotator Yaw(0, GetController()->GetControlRotation().Yaw, 0);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Forward);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Right);
}

void AFirstVoxelCharacter::DoLook(float Yaw, float Pitch)
{
	if (!GetController()) return;
	// FIXED: Correct mouse look sensitivity and remove inversion
	AddControllerYawInput(Yaw);
	AddControllerPitchInput(Pitch);
}

void AFirstVoxelCharacter::DoJumpStart()
{
	// FIX: Do NOT call AddMovementInput here when flying — Tick already polls
	// SpaceBar every frame for vertical flight input. Calling it here too caused
	// a one-shot impulse on press but no sustained rise (felt like nothing happened).
	// Just call Jump() unconditionally; MOVE_Flying mode ignores the jump velocity.
	Jump();
}

void AFirstVoxelCharacter::DoJumpEnd() { StopJumping(); }

// FlyDown is the action-bound version (Ctrl key / Input FlyDown action)
// It only fires while the button is held (ETriggerEvent::Triggered).
void AFirstVoxelCharacter::FlyDown()
{
	if (GetCharacterMovement() && GetCharacterMovement()->MovementMode == MOVE_Flying)
		AddMovementInput(FVector::UpVector, -1.f);
}

// FlyVertical kept for backward compatibility; routes to FlyDown
void AFirstVoxelCharacter::FlyVertical(const FInputActionValue& /*Value*/)
{
	FlyDown();
}

void AFirstVoxelCharacter::ToggleMap()
{
	if (AFirstVoxelPlayerController* PC = Cast<AFirstVoxelPlayerController>(GetController()))
		PC->ToggleMap();
}

void AFirstVoxelCharacter::ToggleFly()
{
	if (!GetCharacterMovement()) return;
	if (GetCharacterMovement()->MovementMode == MOVE_Flying)
	{
		// FIX: Restore full collision BEFORE switching to Walking so the
		// capsule is solid again when the movement mode snaps to ground.
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));
		
		// Properly transition to walking to avoid stuck falling animation
		// Use UpdateFloorFromAdjustment to force immediate floor detection
		UCharacterMovementComponent* CMC = GetCharacterMovement();
		CMC->Velocity = FVector::ZeroVector;
		CMC->SetMovementMode(MOVE_Walking);
		CMC->UpdateFloorFromAdjustment();
		CMC->bJustTeleported = false;
		
		UE_LOG(LogTemplateCharacter, Log, TEXT("Flight Mode DISABLED"));
	}
	else
	{
		GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		// FIX: Keep collision enabled while flying so terrain dig/build
		// raycasts still hit the world, and so re-enabling walk collision
		// works correctly. Flying mode in UE already ignores floor/gravity
		// without needing to disable the capsule collision entirely.
		// The old NoCollision call was the root cause of clipping through terrain.
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));
		UE_LOG(LogTemplateCharacter, Log, TEXT("Flight Mode ENABLED"));
	}
}

// ── Missing Functions Implementation ──────────────────────────────────────

void AFirstVoxelCharacter::Sprint()
{
	if (GetCharacterMovement())
	{
		GetCharacterMovement()->MaxWalkSpeed = 900.f;
	}
}

void AFirstVoxelCharacter::StopSprinting()
{
	if (GetCharacterMovement())
	{
		GetCharacterMovement()->MaxWalkSpeed = 500.f;
	}
}

void AFirstVoxelCharacter::Dig()
{
	CurrentTool = EVoxelToolMode::Dig;
	ApplyCurrentTool();
}

void AFirstVoxelCharacter::Build()
{
	CurrentTool = EVoxelToolMode::Build;
	ApplyCurrentTool();
}

void AFirstVoxelCharacter::SelectToolByIndex(int32 ToolIndex)
{
	CurrentTool = static_cast<EVoxelToolMode>(FMath::Clamp(ToolIndex, 0, 3));
}

void AFirstVoxelCharacter::SelectToolDig()     { SelectToolByIndex(0); }
void AFirstVoxelCharacter::SelectToolBuild()   { SelectToolByIndex(1); }
void AFirstVoxelCharacter::SelectToolSmooth()  { SelectToolByIndex(2); }
void AFirstVoxelCharacter::SelectToolFlatten() { SelectToolByIndex(3); }

void AFirstVoxelCharacter::ToggleAutoWalk()
{
	bAutoWalk = !bAutoWalk;
	UE_LOG(LogTemplateCharacter, Log, TEXT("Auto-walk toggled: %s"), bAutoWalk ? TEXT("TRUE") : TEXT("FALSE"));
}

void AFirstVoxelCharacter::IncreaseRadius()
{
	InteractionRadius = FMath::Clamp(InteractionRadius + 50.f, 50.f, 1000.f);
	UE_LOG(LogTemplateCharacter, Log, TEXT("Interaction Radius Increased: %.1f"), InteractionRadius);
}

void AFirstVoxelCharacter::DecreaseRadius()
{
	InteractionRadius = FMath::Clamp(InteractionRadius - 50.f, 50.f, 1000.f);
	UE_LOG(LogTemplateCharacter, Log, TEXT("Interaction Radius Decreased: %.1f"), InteractionRadius);
}

AVoxelWorld* AFirstVoxelCharacter::FindAndCacheVoxelWorld()
{
	if (CachedVoxelWorld) return CachedVoxelWorld;

	CachedVoxelWorld = Cast<AVoxelWorld>(UGameplayStatics::GetActorOfClass(GetWorld(), AVoxelWorld::StaticClass()));
	return CachedVoxelWorld;
}

void AFirstVoxelCharacter::ApplyCurrentTool()
{
	AVoxelWorld* World = FindAndCacheVoxelWorld();
	if (!World) return;

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;

	FVector CamLoc;
	FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);

	FVector Start = CamLoc;
	FVector End = Start + (CamRot.Vector() * 1500.f);

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	Params.bTraceComplex = true; // FIXED: Enable complex collision for better accuracy

	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		FVector ImpactPoint = Hit.ImpactPoint;
		float CurrentTime = GetWorld()->GetTimeSeconds();

		if (CurrentTool == EVoxelToolMode::Dig)
		{
			if (CurrentTime - DigLastActionTime > 0.05f)
			{
				// FIX: Move INTO the surface (subtract normal) so the sphere actually
				// overlaps solid voxels. Adding the normal placed the sphere in air.
				FVector DigPos = ImpactPoint - (Hit.ImpactNormal * (InteractionRadius * 0.5f));
				World->SetVoxelSphere(DigPos, InteractionRadius, -1.0f, true);
				DigLastActionTime = CurrentTime;
			}
		}
		else if (CurrentTool == EVoxelToolMode::Build)
		{
			if (CurrentTime - BuildLastActionTime > 0.05f)
			{
				// Build: place sphere just above the surface so new voxels attach cleanly
				FVector BuildPos = ImpactPoint + (Hit.ImpactNormal * (InteractionRadius * 0.5f));
				World->SetVoxelSphere(BuildPos, InteractionRadius, 1.0f, true);
				BuildLastActionTime = CurrentTime;
			}
		}
		else if (CurrentTool == EVoxelToolMode::Smooth || CurrentTool == EVoxelToolMode::Flatten)
		{
			// Smooth/Flatten: dig very lightly at the surface to shave protruding voxels
			if (CurrentTime - DigLastActionTime > 0.08f)
			{
				FVector SmoothPos = ImpactPoint - (Hit.ImpactNormal * (InteractionRadius * 0.25f));
				World->SetVoxelSphere(SmoothPos, InteractionRadius, -0.3f, true);
				DigLastActionTime = CurrentTime;
			}
		}
	}
}

void AFirstVoxelCharacter::OpenToolWheel()
{
	bToolWheelOpen = true;
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC)
	{
		// Center the mouse cursor on open
		if (!bLastInputWasGamepad)
		{
			FVector2D ScreenSize;
			GEngine->GameViewport->GetViewportSize(ScreenSize);
			PC->SetMouseLocation(ScreenSize.X * 0.5f, ScreenSize.Y * 0.5f);
		}
	}
}

void AFirstVoxelCharacter::CloseToolWheel()
{
	bToolWheelOpen = false;
}

void AFirstVoxelCharacter::TogglePauseMenu()
{
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
		if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
			HUD->TogglePause();
}

void AFirstVoxelCharacter::ToggleCameraMode()
{
	bIsFirstPerson = !bIsFirstPerson;
	
	if (bIsFirstPerson)
	{
		// Switch to first person
		FollowCamera->SetVisibility(false);
		FirstPersonCamera->SetVisibility(true);
		FirstPersonCamera->Activate();
		CameraBoom->TargetArmLength = 0.f; // Retract the boom
	}
	else
	{
		// Switch to third person
		FirstPersonCamera->SetVisibility(false);
		FollowCamera->SetVisibility(true);
		FollowCamera->Activate();
		CameraBoom->TargetArmLength = 400.f; // Extend the boom
	}
}
