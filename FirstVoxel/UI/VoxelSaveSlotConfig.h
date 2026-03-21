// VoxelSaveSlotConfig.h
// FIX N16 — Save-slot constants extracted from VoxelPauseMenu.h so
//            FirstVoxelHUD.cpp doesn't need to include all of VoxelPauseMenu.h
//            just to use DefaultSlotNames. Both files now include this header.
#pragma once

/** Number of save slots. Increase here to add more slots throughout the UI. */
static constexpr int32 MaxSaveSlots = 5;

/** Human-readable slot names used for file paths and UI labels. */
static const FString DefaultSlotNames[MaxSaveSlots] =
{
    TEXT("Slot1"), TEXT("Slot2"), TEXT("Slot3"), TEXT("Slot4"), TEXT("Slot5")
};
