// VoxelWaterTypes.h
// Shared water constants and the FVoxelWaterData chunk-level struct.
// Extracted from VoxelWaterSimulator.h so any file that only needs the data
// layout (e.g. VoxelChunk.h) does not have to pull in the full simulator.
//
// WATER LEVELS
//   WATER_EMPTY  (0)   = no water
//   1 - 8              = flowing water fill fraction (8 = full)
//   WATER_SOURCE (255) = permanent spring — refills every tick, never consumed
#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"

// ---------------------------------------------------------------------------
// Fill-level constants
// ---------------------------------------------------------------------------
static constexpr uint8 WATER_EMPTY  = 0;
static constexpr uint8 WATER_FULL   = 8;
static constexpr uint8 WATER_SOURCE = 255;   // permanent — never drained

// ---------------------------------------------------------------------------
// FVoxelWaterData
// Owned by AVoxelChunk.  Passed to FVoxelWaterSimulator by pointer on
// chunk registration.  Must outlive its registration.
// ---------------------------------------------------------------------------
struct FVoxelWaterData
{
    /** Fill levels. Flat array, size = ChunkSize^3.
     *  Index: x + y*CS + z*CS*CS */
    TArray<uint8> Cells;

    /** Per-voxel solid flag derived from the terrain density field.
     *  true where density > 0.  Blocks water flow. */
    TBitArray<>   SolidCells;

    /** Set when Cells changed since the last water mesh build. */
    bool bMeshDirty = false;

    void Init(int32 ChunkSize)
    {
        const int32 Total = ChunkSize * ChunkSize * ChunkSize;
        Cells.SetNumZeroed(Total);
        SolidCells.Init(false, Total);
        bMeshDirty = false;
    }

    void Reset()
    {
        FMemory::Memzero(Cells.GetData(), Cells.Num());
        SolidCells.SetRange(0, SolidCells.Num(), false);
        bMeshDirty = false;
    }

    bool HasAnyWater() const
    {
        for (uint8 V : Cells) { if (V != WATER_EMPTY) return true; }
        return false;
    }
};
