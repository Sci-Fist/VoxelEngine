// Copyright Epic Games, Inc. All Rights Reserved.


#include "SideScrollingGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "SideScrollingUI.h"
#include "SideScrollingPickup.h"

void ASideScrollingGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Create the game UI. UserInterfaceClass must be assigned in a Blueprint subclass.
	APlayerController* OwningPlayer = UGameplayStatics::GetPlayerController(GetWorld(), 0);

	if (UserInterfaceClass && OwningPlayer)
	{
		UserInterface = CreateWidget<USideScrollingUI>(OwningPlayer, UserInterfaceClass);
	}

	// If the widget failed to create, log a clear diagnostic rather than crashing
	// with an unhelpful ensure. Designers often forget to assign the class in BP.
	ensureMsgf(UserInterface != nullptr,
		TEXT("ASideScrollingGameMode: UserInterface failed to create. "
		     "Assign UserInterfaceClass in your Blueprint subclass."));
}

void ASideScrollingGameMode::ProcessPickup()
{
	++PickupsCollected;

	// Guard against a missing widget (e.g. UserInterfaceClass was not assigned in BP).
	if (!UserInterface) return;

	// Show the UI panel on the first pickup collected.
	if (PickupsCollected == 1)
	{
		UserInterface->AddToViewport(0);
	}

	UserInterface->UpdatePickups(PickupsCollected);
}