// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
// Forward-declare UBoxComponent — the full definition is only needed in the .cpp.
// Including Components/BoxComponent.h here would drag it into every TU
// that includes this header.
class UBoxComponent;
#include "CombatCheckpointVolume.generated.h"

UCLASS(abstract)
class FIRSTVOXEL_API ACombatCheckpointVolume : public AActor
{
	GENERATED_BODY()
	
	/** Collision box volume */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Components, meta = (AllowPrivateAccess = "true"))
	UBoxComponent* Box;

public:	
	
	/** Constructor */
	ACombatCheckpointVolume();

protected:

	/** Set to true after use to avoid accidentally resetting the checkpoint */
	bool bCheckpointUsed = false;

	/** Handles overlaps with the box volume */
	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
