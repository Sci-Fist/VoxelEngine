// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FirstVoxelGameMode.generated.h"

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class FIRSTVOXEL_API AFirstVoxelGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	
	/** Constructor */
	AFirstVoxelGameMode();
};



