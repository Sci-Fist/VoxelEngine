// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FirstVoxelGameMode.generated.h"

/**
 * Main voxel game mode for the primary gameplay experience.
 * 
 * This game mode handles the core voxel world generation and player spawning.
 * 
 * Note: Combat and Side-Scrolling variants are currently unused development
 * variants. They are preserved in the codebase for potential future integration
 * but are not referenced by this main game mode.
 */
UCLASS(abstract)
class FIRSTVOXEL_API AFirstVoxelGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	
	/** Constructor */
	AFirstVoxelGameMode();
};



