// =============================================================================
// VoxelWaterTypes.h
//
// FIX #33 — HasAnyWater() was O(N) scan per chunk per step (4,096 comparisons
//            for ChunkSize=16, at 5 Hz = 1.2M comparisons/sec for 60 chunks).
//            Replaced with a WaterCellCount counter maintained by the simulator.
//
// FIX #37 — WATER_FULL=8 → WATER_FULL=16 for 17 discrete fill levels (was 9).
//            Reduces visible staircase steps on gently sloping water surfaces
//            from 12.5cm to 6.25cm per level (at VoxelSize=100cm).
//            Value kept power-of-2 for cheap modulo operations.
// =============================================================================
#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"

// ---------------------------------------------------------------------------
// Fill-level constants
// ---------------------------------------------------------------------------
static constexpr uint8 WATER_EMPTY  = 0;
static constexpr uint8 WATER_FULL   = 16;   // FIX #37: was 8 (too coarse)
static constexpr uint8 WATER_SOURCE = 255;  // permanent spring — never drained

// ---------------------------------------------------------------------------
// FVoxelWaterData — owned by AVoxelChunk, pointed to by FVoxelWaterSimulator
// ---------------------------------------------------------------------------
struct FVoxelWaterData
{
    /** Fill levels. Flat array, size = ChunkSize³.  Index: x + y*CS + z*CS². */
    TArray<uint8> Cells;

    /** Per-voxel solid flag from the terrain density field. Blocks water flow. */
    TBitArray<FDefaultBitArrayAllocator> SolidCells;

    /** Set when Cells changed since the last water mesh build. */
    bool bMeshDirty = false;

    /** FIX #33: count of non-empty cells — O(1) HasAnyWater, maintained by simulator. */
    int32 WaterCellCount = 0;

    void Init(int32 ChunkSize)
    {
        const int32 Total = ChunkSize * ChunkSize * ChunkSize;
        Cells.SetNumZeroed(Total);
        SolidCells.Init(false, Total);
        bMeshDirty     = false;
        WaterCellCount = 0;
    }

    void Reset()
    {
        FMemory::Memzero(Cells.GetData(), Cells.Num());
        SolidCells.SetRange(0, SolidCells.Num(), false);
        bMeshDirty     = false;
        WaterCellCount = 0;
    }

    // FIX #33: O(1) — use counter instead of O(N) scan
    bool HasAnyWater() const { return WaterCellCount > 0; }
};
