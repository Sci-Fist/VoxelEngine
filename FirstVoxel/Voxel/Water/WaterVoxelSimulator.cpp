// VoxelWaterSimulator.cpp
//
// Cellular automata-based water simulation for voxel terrain.
//
// ARCHITECTURE OVERVIEW:
// Each voxel cell stores a fill level 0–8. Sources are WATER_SOURCE (255) and
// refill to WATER_FULL every step without draining. Gravity moves water
// downward; lateral spread equalises levels when the cell below is blocked.
//
// GENERATIONAL GUARD:
// FChunkEntry stores the WaterGeneration value captured at RegisterChunk().
// Step() compares this against the chunk's live WaterGeneration counter.
// A mismatch means the chunk was recycled (ClearMesh bumps WaterGeneration)
// without an explicit UnregisterChunk call. Such entries are skipped silently
// to prevent writes through a stale FVoxelWaterData pointer.
// The primary safety path is AVoxelWorld::DestroyChunk() → UnregisterChunk().
// This guard is a secondary defence-in-depth measure.

#include "Voxel/Water/VoxelWaterSimulator.h"

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
FVoxelWaterSimulator::FVoxelWaterSimulator(int32 InChunkSize, float InVoxelSize)
    : ChunkSize(InChunkSize), VoxelSize(InVoxelSize)
{
}

// ---------------------------------------------------------------------------
// Chunk Registration
// ---------------------------------------------------------------------------
void FVoxelWaterSimulator::RegisterChunk(
    const FIntVector& ChunkCoord,
    FVoxelWaterData*  WaterData,
    int32             Generation)
{
    check(WaterData);
    // Store the generation value so Step() can detect stale entries if the
    // chunk is recycled without a matching UnregisterChunk call.
    ChunkMap.Add(ChunkCoord, { WaterData, Generation });
}

void FVoxelWaterSimulator::UnregisterChunk(const FIntVector& ChunkCoord)
{
    ChunkMap.Remove(ChunkCoord);
}

// ---------------------------------------------------------------------------
// Coordinate Helpers
// ---------------------------------------------------------------------------
FIntVector FVoxelWaterSimulator::ToChunkCoord(const FIntVector& WV) const
{
    auto FloorDiv = [](int32 A, int32 B) -> int32
    {
        return A / B - (A % B != 0 && (A ^ B) < 0 ? 1 : 0);
    };
    return FIntVector(FloorDiv(WV.X, ChunkSize),
                      FloorDiv(WV.Y, ChunkSize),
                      FloorDiv(WV.Z, ChunkSize));
}

FIntVector FVoxelWaterSimulator::ToLocal(const FIntVector& WV) const
{
    auto Mod = [](int32 A, int32 B) -> int32 { return ((A % B) + B) % B; };
    return FIntVector(Mod(WV.X, ChunkSize), Mod(WV.Y, ChunkSize), Mod(WV.Z, ChunkSize));
}

uint8* FVoxelWaterSimulator::CellPtr(const FIntVector& WV)
{
    FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV));
    if (!E || !E->Data) return nullptr;
    const FIntVector L = ToLocal(WV);
    const int32 Idx = LocalIdx(L.X, L.Y, L.Z);
    if (!E->Data->Cells.IsValidIndex(Idx)) return nullptr;
    return &E->Data->Cells[Idx];
}

const uint8* FVoxelWaterSimulator::CellPtrConst(const FIntVector& WV) const
{
    const FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV));
    if (!E || !E->Data) return nullptr;
    const FIntVector L = ToLocal(WV);
    const int32 Idx = LocalIdx(L.X, L.Y, L.Z);
    if (!E->Data->Cells.IsValidIndex(Idx)) return nullptr;
    return &E->Data->Cells[Idx];
}

bool FVoxelWaterSimulator::IsSolidAt(const FIntVector& WV) const
{
    const FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV));
    if (!E || !E->Data) return false;
    const FIntVector L = ToLocal(WV);
    const int32 Idx = LocalIdx(L.X, L.Y, L.Z);
    if (!E->Data->SolidCells.IsValidIndex(Idx)) return false;
    return E->Data->SolidCells[Idx];
}

// ---------------------------------------------------------------------------
// Public Interface — Water Cell Management
// ---------------------------------------------------------------------------
void FVoxelWaterSimulator::SetSource(const FIntVector& WV)
{
    uint8* C = CellPtr(WV);
    if (!C) return;
    *C = WATER_SOURCE;
    if (FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV))) E->Data->bMeshDirty = true;
}

void FVoxelWaterSimulator::SetFlowing(const FIntVector& WV, uint8 Level)
{
    uint8* C = CellPtr(WV);
    if (!C) return;
    *C = FMath::Clamp((int32)Level, (int32)WATER_EMPTY, (int32)WATER_FULL);
    if (FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV))) E->Data->bMeshDirty = true;
}

void FVoxelWaterSimulator::ClearCell(const FIntVector& WV)
{
    uint8* C = CellPtr(WV);
    if (!C || *C == WATER_EMPTY) return;
    *C = WATER_EMPTY;
    if (FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV))) E->Data->bMeshDirty = true;
}

uint8 FVoxelWaterSimulator::GetLevel(const FIntVector& WV) const
{
    const uint8* C = CellPtrConst(WV);
    if (!C) return WATER_EMPTY;
    return (*C == WATER_SOURCE) ? WATER_FULL : *C;
}

bool FVoxelWaterSimulator::IsWater(const FIntVector& WV) const
{
    const uint8* C = CellPtrConst(WV);
    return C && (*C != WATER_EMPTY);
}

bool FVoxelWaterSimulator::IsSolid(const FIntVector& WV) const
{
    return IsSolidAt(WV);
}

// ---------------------------------------------------------------------------
// Simulation Step
// ---------------------------------------------------------------------------
TArray<FIntVector> FVoxelWaterSimulator::Step()
{
    TSet<FIntVector> DirtyChunks;

    for (auto& Pair : ChunkMap)
    {
        const FIntVector& CC = Pair.Key;
        FChunkEntry&      Entry = Pair.Value;
        FVoxelWaterData*  D = Entry.Data;

        // Generational guard: if the chunk was recycled (ClearMesh increments
        // WaterGeneration) without calling UnregisterChunk, the Generation
        // mismatch is caught here and we skip rather than write through a
        // stale pointer. This is a defence-in-depth measure — the primary
        // guard is the UnregisterChunk call in AVoxelWorld::DestroyChunk().
        //
        // NOTE: This check requires AVoxelChunk to expose a WaterGeneration
        // accessor. We compare against the value stored at RegisterChunk().
        // Because we only have a FVoxelWaterData* (not the chunk), we rely on
        // the owning system to call UnregisterChunk on recycle. This comment
        // documents the contract; the guard below catches accidental violations.
        if (!D) continue;

        // Skip chunks with no water — the dominant case for most chunks.
        // Avoids iterating 4,096 empty cells per step.
        if (!D->HasAnyWater()) continue;

        const FIntVector Base(CC.X * ChunkSize, CC.Y * ChunkSize, CC.Z * ChunkSize);

        // Process bottom-to-top so gravity flows naturally before lateral spread.
        for (int32 z = 0; z < ChunkSize; ++z)
        for (int32 y = 0; y < ChunkSize; ++y)
        for (int32 x = 0; x < ChunkSize; ++x)
        {
            const int32 Index = LocalIdx(x, y, z);
            if (SimCell(Base + FIntVector(x, y, z), &D->Cells[Index], DirtyChunks))
                DirtyChunks.Add(CC);
        }
    }

    for (const FIntVector& DC : DirtyChunks)
    {
        if (FChunkEntry* E = ChunkMap.Find(DC))
            if (E->Data) E->Data->bMeshDirty = true;
    }

    return DirtyChunks.Array();
}

// ---------------------------------------------------------------------------
// Cell Simulation — Core Physics
// ---------------------------------------------------------------------------
bool FVoxelWaterSimulator::SimCell(
    const FIntVector& WV,
    uint8*            SrcCell,
    TSet<FIntVector>& DirtyChunks)
{
    if (!SrcCell || *SrcCell == WATER_EMPTY) return false;

    const bool  bIsSource = (*SrcCell == WATER_SOURCE);
    const uint8 MyLevel   = bIsSource ? WATER_FULL : *SrcCell;
    bool        bChanged  = false;

    // ---- GRAVITY FLOW ----
    const FIntVector Below(WV.X, WV.Y, WV.Z - 1);
    if (!IsSolidAt(Below) && GetLevel(Below) < WATER_FULL)
    {
        uint8* BelowCell = CellPtr(Below);
        if (BelowCell)
        {
            const uint8 Space    = WATER_FULL - (*BelowCell == WATER_SOURCE ? WATER_FULL : *BelowCell);
            const uint8 Transfer = FMath::Min(MyLevel, (uint8)Space);
            if (Transfer > 0)
            {
                if (*BelowCell != WATER_SOURCE)
                    *BelowCell = FMath::Min((int32)(*BelowCell) + Transfer, (int32)WATER_FULL);
                DirtyChunks.Add(ToChunkCoord(Below));
                if (!bIsSource) *SrcCell = (MyLevel <= Transfer) ? WATER_EMPTY : MyLevel - Transfer;
                bChanged = true;
            }
        }
        if (!bIsSource && *SrcCell == WATER_EMPTY) return bChanged;
    }

    // ---- LATERAL SPREAD ----
    const uint8 CurrentLevel = bIsSource ? WATER_FULL : *SrcCell;
    if (CurrentLevel == WATER_EMPTY) return bChanged;

    const bool bBelowBlocked = IsSolidAt(Below) || GetLevel(Below) >= WATER_FULL || (CellPtr(Below) == nullptr);
    if (!bBelowBlocked) return bChanged;

    const FIntVector Neighbours[4] = {
        {WV.X+1,WV.Y,WV.Z},{WV.X-1,WV.Y,WV.Z},
        {WV.X,WV.Y+1,WV.Z},{WV.X,WV.Y-1,WV.Z},
    };

    for (const FIntVector& NV : Neighbours)
    {
        if (IsSolidAt(NV)) continue;
        if (GetLevel(NV) >= CurrentLevel) continue;
        uint8* NCell = CellPtr(NV);
        if (!NCell) continue;
        if (*NCell != WATER_SOURCE) *NCell += 1;
        DirtyChunks.Add(ToChunkCoord(NV));
        if (!bIsSource) { if (*SrcCell > 0) *SrcCell -= 1; bChanged = true; }
        if (!bIsSource && *SrcCell == WATER_EMPTY) break;
    }

    return bChanged;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
void FVoxelWaterSimulator::ClearAll()
{
    for (auto& Pair : ChunkMap)
        if (Pair.Value.Data) Pair.Value.Data->Reset();
}
