// =============================================================================
// VoxelWaterSimulator.h
// Updated for FIX #33 (WaterCellCount), #38 (Step returns const TArray&),
// #40 (asymmetric flow fix in SimCell).
// =============================================================================
#pragma once

#include "CoreMinimal.h"
#include "Math/IntVector.h"
#include "Voxel/Water/VoxelWaterTypes.h"

class FIRSTVOXEL_API FVoxelWaterSimulator
{
public:
    explicit FVoxelWaterSimulator(int32 InChunkSize, float InVoxelSize);

    // Registration
    void RegisterChunk  (const FIntVector& ChunkCoord, FVoxelWaterData* WaterData, int32 Generation);
    void UnregisterChunk(const FIntVector& ChunkCoord);
    bool IsRegistered   (const FIntVector& ChunkCoord) const { return ChunkMap.Contains(ChunkCoord); }

    // Water cell management
    void  SetSource  (const FIntVector& WorldVoxel);
    void  SetFlowing (const FIntVector& WorldVoxel, uint8 Level = WATER_FULL);
    void  ClearCell  (const FIntVector& WorldVoxel);
    uint8 GetLevel   (const FIntVector& WorldVoxel) const;
    bool  IsWater    (const FIntVector& WorldVoxel) const;
    bool  IsSolid    (const FIntVector& WorldVoxel) const;

    // FIX #38: returns const ref to reused member — no per-tick heap alloc
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
        // Reset to false by SetSource/SetFlowing whenever new water arrives.
        bool             bSettled   = false;
    };
    TMap<FIntVector, FChunkEntry> ChunkMap;

    // FIX #38: reused each Step() to avoid per-tick TSet::Array() allocation
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
                 FVoxelWaterData* SrcData, TSet<FIntVector>& DirtyChunks);
};
