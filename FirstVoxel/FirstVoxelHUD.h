#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FirstVoxelHUD.generated.h"

UCLASS()
class FIRSTVOXEL_API AFirstVoxelHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
