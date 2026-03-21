// VoxelSaveSlotConfig.h
// Single canonical definition of save-slot constants shared by:
//   - FirstVoxelHUD.h (title screen slot picker)
//   - UI/VoxelPauseMenu.h (pause menu save/load UI)
//   - FirstVoxelHUD_Drawing.cpp (title screen drawing helper)
// Include this header wherever slot names/count are needed.
// Using inline constexpr ensures one definition across all translation units.
#pragma once

#include "CoreMinimal.h"

inline constexpr int32 MaxSaveSlots = 5;

inline const FString DefaultSlotNames[MaxSaveSlots] = {
    TEXT("Slot1"), TEXT("Slot2"), TEXT("Slot3"), TEXT("Slot4"), TEXT("Slot5")
};
