// Copyright Epic Games, Inc. All Rights Reserved.

#include "FirstVoxelCharacter.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

#include "CoreMinimal.h"
#include "FirstVoxel.h"
#include "FirstVoxelPlayerController.h"
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
	GetCharacterMovement()->JumpZVelocity                = 500.f;
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
}

void AFirstVoxelCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Auto-load input assets when not pre-assigned via a Blueprint subclass
	if (!JumpAction)
		JumpAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
	if (!MoveAction)
		MoveAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
	if (!LookAction)
		LookAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Look.IA_Look"));
	if (!MouseLookAction)
		MouseLookAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_MouseLook.IA_MouseLook"));
	if (!DefaultMappingContext)
		DefaultMappingContext = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default"));
	if (!ToggleFlyAction)
		ToggleFlyAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Fly.IA_Fly"));
	if (!FlyDownAction)
		FlyDownAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_FlyDown.IA_FlyDown"));
	if (!MapAction)
		MapAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Map.IA_Map"));

	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}

	// Reset dig/build timestamps so re-entering PIE never blocks the first action
	DigLastActionTime   = -1.f;
	BuildLastActionTime = -1.f;
}

void AFirstVoxelCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jump (keyboard/gamepad A)
		EIC->BindAction(JumpAction, ETriggerEvent::Started,   this, &ACharacter::Jump);
		EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Look / Move
		EIC->BindAction(LookAction,      ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::Look);
		EIC->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::Look);
		EIC->BindAction(MoveAction,      ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::Move);

		// Sprint (keyboard Shift)
		EIC->BindAction(SprintAction, ETriggerEvent::Started,   this, &AFirstVoxelCharacter::Sprint);
		EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &AFirstVoxelCharacter::StopSprinting);

		// Dig / Build — ETriggerEvent::Triggered fires every frame while held
		EIC->BindAction(DigAction,   ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::Dig);
		EIC->BindAction(BuildAction, ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::Build);

		// Flight toggle / map
		if (ToggleFlyAction)
			EIC->BindAction(ToggleFlyAction, ETriggerEvent::Started, this, &AFirstVoxelCharacter::ToggleFly);
		if (MapAction)
			EIC->BindAction(MapAction, ETriggerEvent::Started, this, &AFirstVoxelCharacter::ToggleMap);

		// Explicit fly-down action (Ctrl)
		if (FlyDownAction)
			EIC->BindAction(FlyDownAction, ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::FlyDown);
	}

	// ── Keyboard/Mouse fallback bindings ────────────────────────────────────
	PlayerInputComponent->BindKey(EKeys::F,               IE_Pressed, this, &AFirstVoxelCharacter::ToggleFly);
	PlayerInputComponent->BindKey(EKeys::M,               IE_Pressed, this, &AFirstVoxelCharacter::ToggleMap);
	PlayerInputComponent->BindKey(EKeys::MouseScrollUp,   IE_Pressed, this, &AFirstVoxelCharacter::IncreaseRadius);
	PlayerInputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AFirstVoxelCharacter::DecreaseRadius);

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
	// Start → Toggle Map (alternative)
	PlayerInputComponent->BindKey(EKeys::Gamepad_Special_Right,    IE_Pressed, this, &AFirstVoxelCharacter::ToggleMap);
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

		// Q/E yaw look
		const float KBLook = 120.f * DeltaTime;
		if (PC->IsInputKeyDown(EKeys::Q)) AddControllerYawInput(-KBLook);
		if (PC->IsInputKeyDown(EKeys::E)) AddControllerYawInput( KBLook);
	}

	// ── 2. Keyboard: Flight vertical (Space / Ctrl) ───────────────────────
	// Only poll here; FlyDown() handles the Ctrl action via EnhancedInput.
	if (bFlying && !bLastInputWasGamepad)
	{
		if (PC->IsInputKeyDown(EKeys::SpaceBar))    AddMovementInput(FVector::UpVector,  1.f);
		if (PC->IsInputKeyDown(EKeys::LeftControl)) AddMovementInput(FVector::UpVector, -1.f);
	}

	// ── 3. Gamepad: Left Stick Move ──────────────────────────────────────
	if (FMath::Abs(GPLx) > 0.15f || FMath::Abs(GPLy) > 0.15f)
		DoMove(GPLx, GPLy);

	// ── 4. Gamepad: Right Stick Look ─────────────────────────────────────
	if (FMath::Abs(GPRx) > 0.1f || FMath::Abs(GPRy) > 0.1f)
	{
		const float S = GamepadLookSensitivity * DeltaTime;
		DoLook(GPRx * S, GPRy * S);
		bLookedThisFrame = true;
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
	// EnhancedInput bindings only fire when DigAction/BuildAction assets are assigned
	// in the Blueprint subclass. Polling here guarantees mouse dig/build always works
	// regardless of whether those assets are set up, matching the gamepad trigger path.
	if (!bLastInputWasGamepad)
	{
		if (PC->IsInputKeyDown(EKeys::LeftMouseButton) || PC->IsInputKeyDown(EKeys::RightMouseButton))
		{
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

	// ── 8. Mouse look fallback (when EnhancedInput doesn't supply deltas) ─
	if (!bLookedThisFrame)
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
	AddControllerYawInput(Yaw);
	AddControllerPitchInput(Pitch);
}

void AFirstVoxelCharacter::DoJumpStart()
{
	if (GetCharacterMovement() && GetCharacterMovement()->MovementMode == MOVE_Flying)
		AddMovementInput(FVector::UpVector, 1.f);
	else
		Jump();
}

void AFirstVoxelCharacter::DoJumpEnd() { StopJumping(); }

// FlyDown is the action-bound version (Ctrl key / EnhancedInput FlyDown action)
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
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		UE_LOG(LogTemplateCharacter, Log, TEXT("Flight Mode DISABLED"));
	}
	else
	{
		GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		UE_LOG(LogTemplateCharacter, Log, TEXT("Flight Mode ENABLED"));
	}
}

void AFirstVoxelCharacter::Sprint()
{
	if (GetCharacterMovement())
		GetCharacterMovement()->MaxWalkSpeed = 900.f;
}

void AFirstVoxelCharacter::StopSprinting()
{
	if (GetCharacterMovement())
		GetCharacterMovement()->MaxWalkSpeed = 500.f;
}

void AFirstVoxelCharacter::Dig()
{
	// Dig/Build are kept for backward compatibility (Blueprint/Input) and route
	// into the unified tool system.
	CurrentTool = EVoxelToolMode::Dig;
	ApplyCurrentTool();
}

void AFirstVoxelCharacter::Build()
{
	CurrentTool = EVoxelToolMode::Build;
	ApplyCurrentTool();
}

void AFirstVoxelCharacter::IncreaseRadius()
{
	InteractionRadius = FMath::Clamp(InteractionRadius + 50.f, 50.f, 1200.f);
}

AVoxelWorld* AFirstVoxelCharacter::FindAndCacheVoxelWorld()
{
	// Return immediately if already cached — TActorIterator is too expensive to run every frame.
	if (CachedVoxelWorld) return CachedVoxelWorld;

	// Only the player controller has a convenient FindVoxelWorld() helper.
	if (AFirstVoxelPlayerController* PC = Cast<AFirstVoxelPlayerController>(GetController()))
	{
		CachedVoxelWorld = PC->FindVoxelWorld();
	}
	return CachedVoxelWorld;
}

void AFirstVoxelCharacter::DecreaseRadius()
{
	InteractionRadius = FMath::Clamp(InteractionRadius - 50.f, 50.f, 1200.f);
}

void AFirstVoxelCharacter::SelectToolByIndex(int32 ToolIndex)
{
	const int32 ClampedIndex = FMath::Clamp(ToolIndex, 0, 3);
	CurrentTool = static_cast<EVoxelToolMode>(ClampedIndex);
}

void AFirstVoxelCharacter::SelectToolDig()
{
	SelectToolByIndex(0);
}

void AFirstVoxelCharacter::SelectToolBuild()
{
	SelectToolByIndex(1);
}

void AFirstVoxelCharacter::SelectToolSmooth()
{
	SelectToolByIndex(2);
}

void AFirstVoxelCharacter::SelectToolFlatten()
{
	SelectToolByIndex(3);
}

void AFirstVoxelCharacter::ToggleAutoWalk()
{
	bAutoWalk = !bAutoWalk;
	if (bAutoWalk)
	{
		UE_LOG(LogTemplateCharacter, Log, TEXT("Auto-walk ENABLED"));
	}
	else
	{
		UE_LOG(LogTemplateCharacter, Log, TEXT("Auto-walk DISABLED"));
	}
}

void AFirstVoxelCharacter::ApplyCurrentTool()
{
	// Use a member variable (not static) so the timer resets correctly per
	// PIE session and per character instance.
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (Now - DigLastActionTime < 0.05f)
	{
		return;
	}
	DigLastActionTime = Now;

	const float DensityDelta = 1.f;
	float DensityToApply = 0.f;

	// NOTE: These initial tools are density-based. Smooth/Flatten are implemented
	// by applying smaller density changes to mimic softening and leveling.
	switch (CurrentTool)
	{
	case EVoxelToolMode::Dig:
		DensityToApply = -DensityDelta;
		break;
	case EVoxelToolMode::Build:
		DensityToApply = DensityDelta;
		break;
	case EVoxelToolMode::Smooth:
		DensityToApply = -DensityDelta * 0.25f;
		break;
	case EVoxelToolMode::Flatten:
		DensityToApply = DensityDelta * 0.25f;
		break;
	default:
		DensityToApply = -DensityDelta;
		break;
	}

	if (AFirstVoxelPlayerController* PC = Cast<AFirstVoxelPlayerController>(GetController()))
	{
		PC->ModifyVoxelTerrain(InteractionRadius, DensityToApply);
	}
}
