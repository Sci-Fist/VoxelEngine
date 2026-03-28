// =============================================================================
// VoxelWaterSimulator.h
// Updated for FIX #33 (WaterCellCount), #38 (Step returns const TArray&),
// #40 (asymmetric flow fix in SimCell).
// =============================================================================
#pragma once

#include "CoreMinimal.h"
#include "Math/IntVector.h"
#include "HAL/CriticalSection.h"
#include "Voxel/Water/VoxelWaterTypes.h"

/**
 * @class FVoxelWaterSimulator
 * @brief High-performance cellular automata water simulation for the voxel world.
 *
 * This simulator manages a sparse grid of FVoxelWaterData blocks, executing 
 * bit-packed flow logic (CA) on the game thread. It tracks "settled" chunks 
 * to skip idle volumes and minimize CPU overhead.
 * 
 * Key Features:
 * - Constant-time (O(1)) cell access via ChunkMap.
 * - Dirty tracking for efficient mesh rebuilding in UVoxelWorldWaterComponent.
 * - Thread-safe registration allowing background tasks to submit water sources.
 */
class FIRSTVOXEL_API FVoxelWaterSimulator
{
public:
    explicit FVoxelWaterSimulator(int32 InChunkSize, float InVoxelSize);

    // Registration
    void RegisterChunk  (const FIntVector& ChunkCoord, FVoxelWaterData* WaterData, int32 Generation);
    void UnregisterChunk(const FIntVector& ChunkCoord);
    bool IsRegistered   (const FIntVector& ChunkCoord) const;

    // Water cell management
    void  SetSource  (const FIntVector& WorldVoxel);
    void  SetFlowing (const FIntVector& WorldVoxel, uint8 Level = WATER_FULL);
    void  ClearCell  (const FIntVector& WorldVoxel);
    uint8 GetLevel   (const FIntVector& WorldVoxel) const;
    bool  IsWater    (const FIntVector& WorldVoxel) const;
    bool  IsSolid    (const FIntVector& WorldVoxel) const;

    // FIX #38: returns const ref to reused member — MUST be called from GT
    // after an async task is confirmed done.
    // MUST be called from GT after an async task is confirmed done.
    const TArray<FIntVector>& GetLastDirtyChunks() const { return DirtyArray; }

    const TArray<FIntVector>& Step();

    void ClearAll();

private:
    int32 ChunkSize;
    float VoxelSize;

    struct FChunkEntry
    {
        FVoxelWaterData* Data       = nullptr;
        int32            Generation = -1;
        // FIX-3: when true, Step() skips the 16³ voxel loop for this chunk.
        bool             bSettled   = false;
    };

    /** Lock for ChunkMap access during Step() vs registration.
     *  FCriticalSection on Windows wraps CRITICAL_SECTION which is re-entrant by the OS,
     *  so nested acquisition from SimCell helpers inside Step() is safe. */
    FCriticalSection MapLock;

    TMap<FIntVector, FChunkEntry> ChunkMap;

    /** Reused each Step() to avoid per-tick TSet::Array() allocation */
    TArray<FIntVector> DirtyArray;

    FORCEINLINE FIntVector ToChunkCoord(const FIntVector& WV) const;
    FORCEINLINE FIntVector ToLocal     (const FIntVector& WV) const;
    FORCEINLINE int32      LocalIdx(const FIntVector& L) const
    {
        return L.X + L.Y * ChunkSize + L.Z * ChunkSize * ChunkSize;
    }

    uint8*       CellPtr     (const FIntVector& WV);
    const uint8* CellPtrConst(const FIntVector& WV) const;
    bool         IsSolidAt   (const FIntVector& WV) const;

    // FIX #33: accepts SrcData so SimCell can maintain WaterCellCount
    bool SimCell(const FIntVector& WV, uint8* SrcCell,
                 FVoxelWaterData* SrcData, TSet<FIntVector>& DirtyChunks,
                 int32 x, int32 y, int32 z);
};
