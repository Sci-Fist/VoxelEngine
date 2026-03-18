// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "FirstVoxelCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 * Terrain modification tool modes.
 * Keep order aligned with tool selection bindings (1-4 / D-Pad).
 */
UENUM(BlueprintType)
enum class EVoxelToolMode : uint8
{
	Dig UMETA(DisplayName = "Dig"),
	Build UMETA(DisplayName = "Build"),
	Smooth UMETA(DisplayName = "Smooth"),
	Flatten UMETA(DisplayName = "Flatten")
};

/**
 * Main character class for the voxel-based gameplay.
 * 
 * This character provides voxel terrain interaction capabilities including
 * terrain modification and world map functionality.
 * 
 * Note: Combat and Side-Scrolling variants have their own specialized character
 * classes (CombatCharacter and SideScrollingCharacter) that are currently unused
 * but preserved for future development.
 */
UCLASS()
class FIRSTVOXEL_API AFirstVoxelCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;
	
protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

	/** Sprint Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* SprintAction;

	/** Dig Input Action (Remove terrain) */
	UPROPERTY(EditAnywhere, Category="Input|Voxel")
	UInputAction* DigAction;

	/** Build Input Action (Add terrain) */
	UPROPERTY(EditAnywhere, Category="Input|Voxel")
	UInputAction* BuildAction;

	/** Fly Toggle Input Action (F) */
	UPROPERTY(EditAnywhere, Category="Input|Voxel")
	UInputAction* ToggleFlyAction;

	/** Map Toggle Input Action (M) — open/close the world map. */
	UPROPERTY(EditAnywhere, Category="Input|Voxel",
		meta=(ToolTip="Input Action for toggling the world map. Assign IA_Map here or let BeginPlay auto-load it."))
	UInputAction* MapAction;

	/** Pause Toggle Input Action (P / Start) — open/close the pause menu. */
	UPROPERTY(EditAnywhere, Category="Input|Voxel",
		meta=(ToolTip="Input Action for toggling the pause menu. Assign IA_Pause here or leave null to use key polling."))
	UInputAction* PauseAction;

	/** Fly Down Input Action (Ctrl) */
	UPROPERTY(EditAnywhere, Category="Input|Voxel")
	UInputAction* FlyDownAction;

	/** Camera Toggle Input Action (V) */
	UPROPERTY(EditAnywhere, Category="Input|Voxel")
	UInputAction* ToggleCameraAction;

	/** Camera component for first person view */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCamera;

	/** Input Mapping Context */
	UPROPERTY(EditAnywhere, Category="Input")
	class UInputMappingContext* MappingContext;

private:
	/** Track whether Input supplied looking vectors this frame */
	bool bLookedThisFrame = false;

public:

	/** Constructor */
	AFirstVoxelCharacter();

protected:


	virtual void BeginPlay() override;


	/** Override to ensure proper landing animation on voxel terrain */
	virtual void Landed(const FHitResult& Hit) override;


public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

	/** Sprinting */
	void Sprint();
	void StopSprinting();

public:

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Trigger terrain digging from camera */
	void Dig();

	/** Trigger terrain building from camera */
	void Build();

	/** Switches the active terrain tool (0-3). */
	void SelectToolByIndex(int32 ToolIndex);

	/** Convenience bindings for tool selection inputs. */
	void SelectToolDig();
	void SelectToolBuild();
	void SelectToolSmooth();
	void SelectToolFlatten();

	/** Apply the currently selected terrain tool. */
	void ApplyCurrentTool();


	/** Handles jump pressed inputs from either controls or UI interfaces */

	UFUNCTION(BlueprintCallable, Category="Input")

	virtual void DoJumpStart();


	/** Custom floor detection for voxel terrain - more robust than engine default */
	UFUNCTION()
	void CustomFloorCheck();


	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

	/** Toggle flight mode */
	void ToggleFly();

	/** Toggle world map */
	void ToggleMap();

	/** Toggle pause menu (P keyboard / Start gamepad). */
	void TogglePauseMenu();

	/**
	 * Toggle auto-walk on/off.
	 * While active the character walks forward at current speed every frame.
	 * Cancelled automatically when the player pushes the move stick/keys in any direction.
	 */
	UFUNCTION(BlueprintCallable, Category="Input")
	void ToggleAutoWalk();

	/** True while auto-walk is active. Read by HUD to show the indicator. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Input")
	bool bAutoWalk = false;

	/** Fly Downward – called by Input FlyDown action and by Tick gamepad polling */
	void FlyDown();

	/** Kept for backward-compat with any existing Blueprint bindings; calls FlyDown() */
	void FlyVertical(const FInputActionValue& Value);

	void IncreaseRadius();
	void DecreaseRadius();

	/** Radius of voxel modifications. Adjusted with MouseWheel / LB+RB. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category="Voxel|Brush")
	float InteractionRadius = 300.f;

	/**
	 * Lazily-cached pointer to the first AVoxelWorld in the level.
	 * Populated on first use by FindAndCacheVoxelWorld(); null until then.
	 * Shared by all subclasses (Combat, SideScrolling, Platforming) so the
	 * TActorIterator scan only ever runs once per character lifetime.
	 */
	UPROPERTY()
	class AVoxelWorld* CachedVoxelWorld = nullptr;

	/**
	 * Locates and caches AVoxelWorld via the player controller.
	 * Safe to call every Tick — returns immediately once the cache is warm.
	 * Returns the cached pointer (may be null if no VoxelWorld exists).
	 */
	AVoxelWorld* FindAndCacheVoxelWorld();

	/** True when the last detected input came from a gamepad. Updated every Tick.
	 *  Read by AFirstVoxelHUD to show controller vs keyboard labels. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Input")
	bool bLastInputWasGamepad = false;

	/** True while the radial tool selection wheel is held open. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Voxel|Tools")
	bool bToolWheelOpen = false;

	UFUNCTION(BlueprintCallable, Category="Voxel|Tools")
	void OpenToolWheel();

	UFUNCTION(BlueprintCallable, Category="Voxel|Tools")
	void CloseToolWheel();

	/** Look sensitivity for gamepad right stick (degrees per second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input", meta=(ClampMin="10.0", ClampMax="500.0"))
	float GamepadLookSensitivity = 160.f;

	/** Currently selected terrain tool (defaults to Dig). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Voxel|Tools")
	EVoxelToolMode CurrentTool = EVoxelToolMode::Dig;

	/** Toggle between first person and third person camera modes */
	void ToggleCameraMode();

	/** Switch between first person and third person camera */
	void SwitchCamera();

	/** Toggle between first person and third person camera */
	void ToggleCamera();

	/** Set camera to first person view */
	void SetFirstPersonView();

	/** Set camera to third person view */
	void SetThirdPersonView();

	/** Update camera position and rotation for third person view */
	void UpdateThirdPersonCamera(float DeltaTime);

	/** True when in first person mode */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Camera")
	bool bIsFirstPerson = false;

	/** Whether the character is in third person view */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	bool bIsThirdPerson;

	/** Camera transition speed */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CameraTransitionSpeed;

	/** Third person camera distance */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float ThirdPersonDistance;

	/** Third person camera height */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float ThirdPersonHeight;

	/** Third person camera look-at offset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float ThirdPersonLookAtOffset;

private:
	/** Per-instance dig/build throttle timestamps (replaces static locals to work correctly across PIE sessions). */
	float DigLastActionTime   = -1.f;
	float BuildLastActionTime = -1.f;

public:

	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};

