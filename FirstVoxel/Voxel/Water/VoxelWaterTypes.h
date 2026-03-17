// =============================================================================
// VoxelWaterTypes.h
// =============================================================================
//
// Shared water constants and the FVoxelWaterData chunk-level data struct.
// Split from VoxelWaterSimulator.h so that VoxelChunk.h (included everywhere)
// can hold a FVoxelWaterData without dragging in the full simulator.
//
// -- FILL LEVEL ENCODING ------------------------------------------------------
//
//   WATER_EMPTY   0     No water in this voxel
//   1 - 7               Flowing water at fractional fill (1=nearly empty)
//   WATER_FULL    8     Full voxel of flowing water
//   9 - 254             Reserved (treat as flowing for forward-compat)
//   WATER_SOURCE  255   Permanent spring -- forced to WATER_FULL every step,
//                       never drained by the simulation
//
// -- FVoxelWaterData LAYOUT ---------------------------------------------------
//
//   Cells[]      uint8[ChunkSize^3]  fill levels, index = x + y*CS + z*CS^2
//   SolidCells[] TBitArray           true where terrain density > 0
//   bMeshDirty   bool                set when Cells changed since last mesh build
//
//   SolidCells is built from the density array in AVoxelChunk::ApplyMesh().
//   It is NOT updated when the player edits terrain -- bMeshDirty will trigger
//   a full chunk regeneration which rebuilds SolidCells from scratch.
// =============================================================================
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
