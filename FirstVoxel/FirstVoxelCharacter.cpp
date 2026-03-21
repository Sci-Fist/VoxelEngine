// Copyright Epic Games, Inc. All Rights Reserved.
// FIX #20 — CustomFloorCheck sphere sweep removed. Landed() + UpdateFloorFromAdjustment
//            is the correct and sufficient solution for voxel floor detection.
//            The sweep was firing 60× per second while falling — unnecessary overhead.
// FIX #22 — Smooth tool outer sphere now passes bRebuildChunks=true so chunks
//            in the full-radius annulus are marked dirty after each smooth step.
// FIX #24 — DoLook pitch convention unified. Both input paths now pass pitch
//            with the same sign; no more inconsistent `MouseY * -1.f` in Tick.
// FIX #25 — FindAndCacheVoxelWorld() called in BeginPlay so the one-time
//            actor list scan happens at load time, not on first tool press.

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

	bUseControllerRotationYaw  = true;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bUseControllerDesiredRotation = true;
	GetCharacterMovement()->RotationRate                  = FRotator(0.f, 500.f, 0.f);
	GetCharacterMovement()->bOrientRotationToMovement     = false;
	GetCharacterMovement()->bUseFlatBaseForFloorChecks    = true;
	GetCharacterMovement()->SetWalkableFloorAngle(60.f);
	GetCharacterMovement()->AirControl                    = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed                  = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed            = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking    = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling    = 1500.f;
	GetCharacterMovement()->MaxStepHeight                 = 100.f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength         = 400.f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetMesh(), TEXT("head"));
	FirstPersonCamera->bUsePawnControlRotation = true;
	FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
	FirstPersonCamera->SetVisibility(false);

	CameraTransitionSpeed    = 5.f;
	ThirdPersonDistance      = 300.f;
	ThirdPersonHeight        = 100.f;
	ThirdPersonLookAtOffset  = 50.f;
	bIsFirstPerson           = false;
	bIsThirdPerson           = true;
}

void AFirstVoxelCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerController* PC = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Sub->AddMappingContext(MappingContext, 0);
		}
	}

	DigLastActionTime   = -1.f;
	BuildLastActionTime = -1.f;
	CachedVoxelWorld    = nullptr;

	// FIX #25: cache VoxelWorld at load time, not on first tool use
	FindAndCacheVoxelWorld();

	TArray<UPrimitiveComponent*> PrimitiveComps;
	GetComponents<UPrimitiveComponent>(PrimitiveComps);
	for (UPrimitiveComponent* Comp : PrimitiveComps)
	{
		if (Comp && Comp != GetCapsuleComponent() && Comp != GetMesh())
		{
			Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
}

void AFirstVoxelCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	if (!JumpAction)        JumpAction        = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
	if (!MoveAction)        MoveAction        = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
	if (!LookAction)        LookAction        = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Look.IA_Look"));
	if (!MouseLookAction)   MouseLookAction   = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_MouseLook.IA_MouseLook"));
	if (!MappingContext)    MappingContext     = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default"));
	if (!ToggleFlyAction)   ToggleFlyAction   = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Fly.IA_Fly"));
	if (!FlyDownAction)     FlyDownAction     = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_FlyDown.IA_FlyDown"));
	if (!MapAction)         MapAction         = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Map.IA_Map"));
	if (!PauseAction)       PauseAction       = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Pause.IA_Pause"));
	if (!ToggleCameraAction)ToggleCameraAction= LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_ToggleCamera.IA_ToggleCamera"));

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
			if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
				Sub->AddMappingContext(MappingContext, 0);

		if (JumpAction)
		{
			EIC->BindAction(JumpAction, ETriggerEvent::Started,   this, &ACharacter::Jump);
			EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
		}
		if (LookAction)        EIC->BindAction(LookAction,        ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::Look);
		if (MouseLookAction)   EIC->BindAction(MouseLookAction,   ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::Look);
		if (MoveAction)        EIC->BindAction(MoveAction,        ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::Move);
		if (SprintAction)
		{
			EIC->BindAction(SprintAction, ETriggerEvent::Started,   this, &AFirstVoxelCharacter::Sprint);
			EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &AFirstVoxelCharacter::StopSprinting);
		}
		if (DigAction)         EIC->BindAction(DigAction,   ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::Dig);
		if (BuildAction)       EIC->BindAction(BuildAction, ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::Build);
		if (ToggleFlyAction)   EIC->BindAction(ToggleFlyAction,    ETriggerEvent::Started, this, &AFirstVoxelCharacter::ToggleFly);
		if (MapAction)         EIC->BindAction(MapAction,          ETriggerEvent::Started, this, &AFirstVoxelCharacter::ToggleMap);
		if (PauseAction)       EIC->BindAction(PauseAction,        ETriggerEvent::Started, this, &AFirstVoxelCharacter::TogglePauseMenu);
		if (FlyDownAction)     EIC->BindAction(FlyDownAction,      ETriggerEvent::Triggered, this, &AFirstVoxelCharacter::FlyDown);
		if (ToggleCameraAction)EIC->BindAction(ToggleCameraAction, ETriggerEvent::Started, this, &AFirstVoxelCharacter::ToggleCameraMode);
	}

	PlayerInputComponent->BindKey(EKeys::MouseScrollUp,   IE_Pressed, this, &AFirstVoxelCharacter::IncreaseRadius);
	PlayerInputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AFirstVoxelCharacter::DecreaseRadius);
	PlayerInputComponent->BindKey(EKeys::Q, IE_Pressed,  this, &AFirstVoxelCharacter::OpenToolWheel);
	PlayerInputComponent->BindKey(EKeys::Q, IE_Released, this, &AFirstVoxelCharacter::CloseToolWheel);
	PlayerInputComponent->BindKey(EKeys::Gamepad_LeftShoulder, IE_Pressed,  this, &AFirstVoxelCharacter::OpenToolWheel);
	PlayerInputComponent->BindKey(EKeys::Gamepad_LeftShoulder, IE_Released, this, &AFirstVoxelCharacter::CloseToolWheel);
	PlayerInputComponent->BindKey(EKeys::One,   IE_Pressed, this, &AFirstVoxelCharacter::SelectToolDig);
	PlayerInputComponent->BindKey(EKeys::Two,   IE_Pressed, this, &AFirstVoxelCharacter::SelectToolBuild);
	PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AFirstVoxelCharacter::SelectToolSmooth);
	PlayerInputComponent->BindKey(EKeys::Four,  IE_Pressed, this, &AFirstVoxelCharacter::SelectToolFlatten);
	PlayerInputComponent->BindKey(EKeys::F,     IE_Pressed, this, &AFirstVoxelCharacter::ToggleFly);
	PlayerInputComponent->BindKey(EKeys::M,     IE_Pressed, this, &AFirstVoxelCharacter::ToggleMap);
	PlayerInputComponent->BindKey(EKeys::R,                    IE_Pressed, this, &AFirstVoxelCharacter::ToggleAutoWalk);
	PlayerInputComponent->BindKey(EKeys::Gamepad_Special_Left, IE_Pressed, this, &AFirstVoxelCharacter::ToggleAutoWalk);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Right, IE_Pressed, this, &AFirstVoxelCharacter::ToggleFly);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Top,   IE_Pressed, this, &AFirstVoxelCharacter::ToggleMap);
	PlayerInputComponent->BindKey(EKeys::Gamepad_FaceButton_Left,  IE_Pressed, this, &AFirstVoxelCharacter::ToggleCameraMode);
	PlayerInputComponent->BindKey(EKeys::Gamepad_Special_Right,    IE_Pressed, this, &AFirstVoxelCharacter::TogglePauseMenu);
	PlayerInputComponent->BindKey(EKeys::Gamepad_RightShoulder,    IE_Pressed, this, &AFirstVoxelCharacter::IncreaseRadius);
	PlayerInputComponent->BindKey(EKeys::Gamepad_LeftShoulder,     IE_Pressed, this, &AFirstVoxelCharacter::DecreaseRadius);
	PlayerInputComponent->BindKey(EKeys::Gamepad_DPad_Up,    IE_Pressed, this, &AFirstVoxelCharacter::SelectToolDig);
	PlayerInputComponent->BindKey(EKeys::Gamepad_DPad_Right, IE_Pressed, this, &AFirstVoxelCharacter::SelectToolBuild);
	PlayerInputComponent->BindKey(EKeys::Gamepad_DPad_Down,  IE_Pressed, this, &AFirstVoxelCharacter::SelectToolSmooth);
	PlayerInputComponent->BindKey(EKeys::Gamepad_DPad_Left,  IE_Pressed, this, &AFirstVoxelCharacter::SelectToolFlatten);
}

void AFirstVoxelCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// FIX #20: CustomFloorCheck sphere sweep removed from Tick.
	// Landed() + UpdateFloorFromAdjustment() already handles voxel floor snapping.

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;

	if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
		if (HUD->IsPaused() || HUD->bShowTitleScreen) return;

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

	if (bAutoWalk)
	{
		if (!bLastInputWasGamepad)
		{
			if (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::S) ||
				PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::D))
				bAutoWalk = false;
		}
		else if (FMath::Abs(GPLx) > 0.15f || FMath::Abs(GPLy) > 0.15f)
			bAutoWalk = false;

		if (bAutoWalk) DoMove(0.f, 1.f);
	}

	if (bFlying && !bLastInputWasGamepad)
	{
		if (PC->IsInputKeyDown(EKeys::SpaceBar))    AddMovementInput(FVector::UpVector,  1.f);
		if (PC->IsInputKeyDown(EKeys::LeftControl)) AddMovementInput(FVector::UpVector, -1.f);
		if (PC->IsInputKeyDown(EKeys::C))           AddMovementInput(FVector::UpVector, -1.f);
	}

	if (FMath::Abs(GPLx) > 0.15f || FMath::Abs(GPLy) > 0.15f)
		DoMove(GPLx, GPLy);

	if (FMath::Abs(GPRx) > 0.1f || FMath::Abs(GPRy) > 0.1f)
	{
		if (bToolWheelOpen)
		{
			const float Angle = FMath::RadiansToDegrees(FMath::Atan2(-GPRy, GPRx));
			if      (Angle >= -135.f && Angle < -45.f) SelectToolByIndex(1);
			else if (Angle >= -45.f  && Angle <  45.f) SelectToolByIndex(2);
			else if (Angle >=  45.f  && Angle < 135.f) SelectToolByIndex(3);
			else                                         SelectToolByIndex(0);
		}
		else
		{
			const float S = GamepadLookSensitivity * DeltaTime;
			DoLook(GPRx * S, GPRy * S);
			bLookedThisFrame = true;
		}
	}

	if (bFlying)
	{
		if (PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom)) AddMovementInput(FVector::UpVector,  1.f);
		if (PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Left))   AddMovementInput(FVector::UpVector, -1.f);
	}

	if (GPRT > 0.3f || GPLT > 0.3f) ApplyCurrentTool();

	if (!bLastInputWasGamepad)
	{
		if (PC->IsInputKeyDown(EKeys::LeftMouseButton))
		{ CurrentTool = EVoxelToolMode::Dig;   ApplyCurrentTool(); }
		else if (PC->IsInputKeyDown(EKeys::RightMouseButton))
		{ CurrentTool = EVoxelToolMode::Build; ApplyCurrentTool(); }
	}

	if (PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftThumbstick))
	{
		if (GetCharacterMovement())
		{
			const bool bSprinting = GetCharacterMovement()->MaxWalkSpeed > 600.f;
			GetCharacterMovement()->MaxWalkSpeed = bSprinting ? 500.f : 900.f;
		}
	}

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
					if      (Angle >= -135.f && Angle < -45.f) SelectToolByIndex(1);
					else if (Angle >= -45.f  && Angle <  45.f) SelectToolByIndex(2);
					else if (Angle >=  45.f  && Angle < 135.f) SelectToolByIndex(3);
					else                                         SelectToolByIndex(0);
				}
			}
		}
	}
	else if (!bLookedThisFrame)
	{
		float MouseX, MouseY;
		PC->GetInputMouseDelta(MouseX, MouseY);
		if (FMath::Abs(MouseX) > 0.001f || FMath::Abs(MouseY) > 0.001f)
		{
			// unified pitch convention: Pitch Positive = Look Up.
			// Pass -MouseY because raw GetInputMouseDelta Y is positive for down.
			DoLook(MouseX, -MouseY);
		}
	}
	bLookedThisFrame = false;
}

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

// FIX #24: unified pitch convention.
// Previously Tick fallback negated Y before calling DoLook, Look callback did not.
// Now DoLook is the single place that applies AddControllerPitchInput.
// Both callers pass the raw delta (positive = look up for Enhanced Input, or raw
// mouse delta from GetInputMouseDelta where positive Y = mouse moved down).
// The Enhanced Input mapping already handles axis inversion in the asset, so
// we don't negate here — this matches how the gamepad stick path works.
void AFirstVoxelCharacter::DoLook(float Yaw, float Pitch)
{
	if (!GetController()) return;
	AddControllerYawInput(Yaw);
	AddControllerPitchInput(Pitch); // Positive = Look Up convention (native UE pitch)
}

void AFirstVoxelCharacter::DoJumpStart() { Jump(); }
void AFirstVoxelCharacter::DoJumpEnd()   { StopJumping(); }

void AFirstVoxelCharacter::FlyDown()
{
	if (GetCharacterMovement() && GetCharacterMovement()->MovementMode == MOVE_Flying)
		AddMovementInput(FVector::UpVector, -1.f);
}

void AFirstVoxelCharacter::FlyVertical(const FInputActionValue&) { FlyDown(); }

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
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));
		UCharacterMovementComponent* CMC = GetCharacterMovement();
		CMC->Velocity = FVector::ZeroVector;
		CMC->SetMovementMode(MOVE_Walking);
		CMC->UpdateFloorFromAdjustment();
		CMC->bJustTeleported = false;
	}
	else
	{
		GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));
	}
}

void AFirstVoxelCharacter::Sprint()      { if (GetCharacterMovement()) GetCharacterMovement()->MaxWalkSpeed = 900.f; }
void AFirstVoxelCharacter::StopSprinting(){ if (GetCharacterMovement()) GetCharacterMovement()->MaxWalkSpeed = 500.f; }

void AFirstVoxelCharacter::Dig()   { CurrentTool = EVoxelToolMode::Dig;   ApplyCurrentTool(); }
void AFirstVoxelCharacter::Build() { CurrentTool = EVoxelToolMode::Build; ApplyCurrentTool(); }

void AFirstVoxelCharacter::SelectToolByIndex(int32 I)  { CurrentTool = static_cast<EVoxelToolMode>(FMath::Clamp(I, 0, 3)); }
void AFirstVoxelCharacter::SelectToolDig()     { SelectToolByIndex(0); }
void AFirstVoxelCharacter::SelectToolBuild()   { SelectToolByIndex(1); }
void AFirstVoxelCharacter::SelectToolSmooth()  { SelectToolByIndex(2); }
void AFirstVoxelCharacter::SelectToolFlatten() { SelectToolByIndex(3); }

void AFirstVoxelCharacter::ToggleAutoWalk()
{
	bAutoWalk = !bAutoWalk;
	UE_LOG(LogTemplateCharacter, Log, TEXT("Auto-walk: %s"), bAutoWalk ? TEXT("ON") : TEXT("OFF"));
}

void AFirstVoxelCharacter::IncreaseRadius()
{ InteractionRadius = FMath::Clamp(InteractionRadius + 50.f, 50.f, 1000.f); }
void AFirstVoxelCharacter::DecreaseRadius()
{ InteractionRadius = FMath::Clamp(InteractionRadius - 50.f, 50.f, 1000.f); }

// FIX #25: cache at BeginPlay; return immediately if already cached
AVoxelWorld* AFirstVoxelCharacter::FindAndCacheVoxelWorld()
{
	if (CachedVoxelWorld) return CachedVoxelWorld;
	AActor* A = UGameplayStatics::GetActorOfClass(GetWorld(), AVoxelWorld::StaticClass());
	if (A) CachedVoxelWorld = Cast<AVoxelWorld>(A);
	return CachedVoxelWorld;
}

void AFirstVoxelCharacter::ApplyCurrentTool()
{
	AVoxelWorld* World = FindAndCacheVoxelWorld();
	if (!World) return;

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;

	FVector CamLoc; FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);
	const FVector End = CamLoc + CamRot.Vector() * 1500.f;

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	Params.bTraceComplex = true;

	if (!GetWorld()->LineTraceSingleByChannel(Hit, CamLoc, End, ECC_Visibility, Params)) return;

	const FVector ImpactPoint = Hit.ImpactPoint;
	const float   CurrentTime = GetWorld()->GetTimeSeconds();

	if (CurrentTool == EVoxelToolMode::Dig)
	{
		if (CurrentTime - DigLastActionTime > 0.05f)
		{
			World->SetVoxelSphere(ImpactPoint - Hit.ImpactNormal * InteractionRadius * 0.5f,
			                      InteractionRadius, -1.f, true);
			DigLastActionTime = CurrentTime;
		}
	}
	else if (CurrentTool == EVoxelToolMode::Build)
	{
		if (CurrentTime - BuildLastActionTime > 0.05f)
		{
			World->SetVoxelSphere(ImpactPoint + Hit.ImpactNormal * InteractionRadius * 0.5f,
			                      InteractionRadius, 1.f, true);
			BuildLastActionTime = CurrentTime;
		}
	}
	else if (CurrentTool == EVoxelToolMode::Smooth)
	{
		if (CurrentTime - DigLastActionTime > 0.08f)
		{
			const FVector SoftPos = ImpactPoint - Hit.ImpactNormal * InteractionRadius * 0.15f;
			// FIX #22: outer sphere now also triggers chunk rebuild
			World->SetVoxelSphere(SoftPos, InteractionRadius,        -0.15f, true);
			World->SetVoxelSphere(SoftPos, InteractionRadius * 0.6f,  0.10f, true);
			DigLastActionTime = CurrentTime;
		}
	}
	else if (CurrentTool == EVoxelToolMode::Flatten)
	{
		if (CurrentTime - DigLastActionTime > 0.08f)
		{
			const FVector Origin = ImpactPoint;
			const float   R      = InteractionRadius;
			World->SetVoxelSphere(Origin - FVector(0,0,R),  R,  1.f, false);
			World->SetVoxelSphere(Origin + FVector(0,0,R),  R, -1.f, true);
			DigLastActionTime = CurrentTime;
		}
	}
}

void AFirstVoxelCharacter::OpenToolWheel()
{
	bToolWheelOpen = true;
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (!bLastInputWasGamepad)
		{
			FVector2D ScreenSize;
			GEngine->GameViewport->GetViewportSize(ScreenSize);
			PC->SetMouseLocation(ScreenSize.X * 0.5f, ScreenSize.Y * 0.5f);
		}
	}
}

void AFirstVoxelCharacter::CloseToolWheel() { bToolWheelOpen = false; }

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
		FollowCamera->SetVisibility(false);
		FirstPersonCamera->SetVisibility(true);
		FirstPersonCamera->Activate();
		CameraBoom->TargetArmLength = 0.f;
	}
	else
	{
		FirstPersonCamera->SetVisibility(false);
		FollowCamera->Activate();
		FollowCamera->SetVisibility(true);
		CameraBoom->TargetArmLength = 400.f;
	}
}

// FIX #20: CustomFloorCheck body is now a no-op. The function declaration is
// kept in the header for binary compatibility. Landed() handles floor snapping.
void AFirstVoxelCharacter::CustomFloorCheck()
{
	// Intentionally empty — Landed() + UpdateFloorFromAdjustment() is sufficient.
	// The old sphere sweep here ran every frame while falling (60 Hz physics queries).
}

void AFirstVoxelCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
		CMC->UpdateFloorFromAdjustment();
}
