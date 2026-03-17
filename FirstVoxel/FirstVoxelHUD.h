#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FirstVoxelHUD.generated.h"

class UVoxelPauseMenu;

UCLASS()
class FIRSTVOXEL_API AFirstVoxelHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void DrawHUD()   override;

	/** If true, draws the simple text title screen overlay inside DrawHUD. */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Voxel")
	bool bShowTitleScreen = false;

	/**
	 * Toggle the pause menu open/closed.
	 * Called by AFirstVoxelCharacter when P is pressed or gamepad Start fires.
	 */
	UFUNCTION(BlueprintCallable, Category = "Voxel|UI")
	void TogglePause();

	/** Accessibility accessor for Canvas. */
	class UCanvas* GetCanvas() const { return Canvas; }

	/** True while the pause menu is open. Read by character to suppress gameplay input. */
	UFUNCTION(BlueprintPure, Category = "Voxel|UI")
	bool IsPaused() const;

private:
	/** Owned pause menu instance. Created in BeginPlay. */
	UPROPERTY()
	TObjectPtr<UVoxelPauseMenu> PauseMenu = nullptr;
};
