// WaterVoxelSimulator.cpp
//
// FIX #33 — Maintain FVoxelWaterData::WaterCellCount in SimCell so
//            HasAnyWater() is O(1) instead of O(N).
// FIX #38 — Step() now reuses a persistent TArray<FIntVector> DirtyArray
//            member to avoid a per-tick heap allocation from TSet::Array().
// FIX #40 — Neighbour check order is hashed per-voxel to eliminate the
//            directional bias that made water spread east before west.

#include "Voxel/Water/VoxelWaterSimulator.h"

FVoxelWaterSimulator::FVoxelWaterSimulator(int32 InChunkSize, float InVoxelSize)
    : ChunkSize(InChunkSize), VoxelSize(InVoxelSize)
{}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
void FVoxelWaterSimulator::RegisterChunk(const FIntVector& CC, FVoxelWaterData* D, int32 Gen)
{
    FScopeLock Lock(&MapLock);
    check(D);
    ChunkMap.Add(CC, { D, Gen, /*bSettled=*/false });
}

void FVoxelWaterSimulator::UnregisterChunk(const FIntVector& CC)
{
    FScopeLock Lock(&MapLock);
    ChunkMap.Remove(CC);
}

bool FVoxelWaterSimulator::IsRegistered(const FIntVector& CC) const
{
    FScopeLock Lock(&const_cast<FVoxelWaterSimulator*>(this)->MapLock);
    return ChunkMap.Contains(CC);
}

// ---------------------------------------------------------------------------
// Coordinate helpers
// ---------------------------------------------------------------------------
FIntVector FVoxelWaterSimulator::ToChunkCoord(const FIntVector& WV) const
{
    auto FD = [](int32 A, int32 B) { return A / B - (A % B != 0 && (A ^ B) < 0 ? 1 : 0); };
    return { FD(WV.X, ChunkSize), FD(WV.Y, ChunkSize), FD(WV.Z, ChunkSize) };
}

FIntVector FVoxelWaterSimulator::ToLocal(const FIntVector& WV) const
{
    auto M = [](int32 A, int32 B) { return ((A % B) + B) % B; };
    return { M(WV.X, ChunkSize), M(WV.Y, ChunkSize), M(WV.Z, ChunkSize) };
}

uint8* FVoxelWaterSimulator::CellPtr(const FIntVector& WV)
{
    FScopeLock Lock(&MapLock);
    FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV));
    if (!E || !E->Data) return nullptr;
    const int32 Idx = LocalIdx(ToLocal(WV));
    return E->Data->Cells.IsValidIndex(Idx) ? &E->Data->Cells[Idx] : nullptr;
}

const uint8* FVoxelWaterSimulator::CellPtrConst(const FIntVector& WV) const
{
    FScopeLock Lock(&const_cast<FVoxelWaterSimulator*>(this)->MapLock);
    const FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV));
    if (!E || !E->Data) return nullptr;
    const int32 Idx = LocalIdx(ToLocal(WV));
    return E->Data->Cells.IsValidIndex(Idx) ? &E->Data->Cells[Idx] : nullptr;
}

bool FVoxelWaterSimulator::IsSolidAt(const FIntVector& WV) const
{
    const FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV));
    if (!E || !E->Data) return false;
    const int32 Idx = LocalIdx(ToLocal(WV));
    return E->Data->SolidCells.IsValidIndex(Idx) && E->Data->SolidCells[Idx];
}

// ---------------------------------------------------------------------------
// Public water cell management
// Maintain WaterCellCount for O(1) HasAnyWater (FIX #33)
// ---------------------------------------------------------------------------
void FVoxelWaterSimulator::SetSource(const FIntVector& WV)
{
    uint8* C = CellPtr(WV);
    if (!C) return;
    const bool bWasEmpty = (*C == WATER_EMPTY);
    *C = WATER_SOURCE;
    if (FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV)))
    {
        E->Data->bMeshDirty = true;
        E->bSettled = false; // FIX-3: wake settled chunk when source changes
        if (bWasEmpty) E->Data->WaterCellCount++;
    }
}

void FVoxelWaterSimulator::SetFlowing(const FIntVector& WV, uint8 Level)
{
    uint8* C = CellPtr(WV);
    if (!C) return;
    const bool bWasEmpty = (*C == WATER_EMPTY);
    *C = FMath::Clamp((int32)Level, (int32)WATER_EMPTY, (int32)WATER_FULL);
    if (FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV)))
    {
        E->Data->bMeshDirty = true;
        E->bSettled = false; // Wake up chunk for continuous simulation flow propagation downwards !!
        if (bWasEmpty && *C != WATER_EMPTY)      E->Data->WaterCellCount++;
        else if (!bWasEmpty && *C == WATER_EMPTY) E->Data->WaterCellCount--;
    }
}

void FVoxelWaterSimulator::ClearCell(const FIntVector& WV)
{
    uint8* C = CellPtr(WV);
    if (!C || *C == WATER_EMPTY) return;
    *C = WATER_EMPTY;
    if (FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV)))
    {
        E->Data->bMeshDirty = true;
        E->Data->WaterCellCount = FMath::Max(0, E->Data->WaterCellCount - 1);
    }
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

bool FVoxelWaterSimulator::IsSolid(const FIntVector& WV) const { return IsSolidAt(WV); }

// ---------------------------------------------------------------------------
// Simulation step
// FIX #38: reuse DirtyArray member, no per-tick TSet::Array() allocation
// ---------------------------------------------------------------------------
const TArray<FIntVector>& FVoxelWaterSimulator::Step()
{
    FScopeLock Lock(&MapLock);
    DirtyArray.Reset();
    TSet<FIntVector> DirtySet;

    for (auto& Pair : ChunkMap)
    {
        FChunkEntry&     Entry = Pair.Value;
        FVoxelWaterData* D     = Entry.Data;
        if (!D) continue;
        if (!D->HasAnyWater()) continue;
        // FIX-3: skip settled chunks — no cells changed last step
        if (Entry.bSettled) continue;

        const FIntVector Base(Pair.Key.X * ChunkSize,
                              Pair.Key.Y * ChunkSize,
                              Pair.Key.Z * ChunkSize);

        const int32 PrevDirtyCount = DirtySet.Num();
        for (int32 z = 0; z < ChunkSize; ++z)
        for (int32 y = 0; y < ChunkSize; ++y)
        for (int32 x = 0; x < ChunkSize; ++x)
        {
            const int32 Index = LocalIdx(FIntVector(x, y, z));
            if (SimCell(Base + FIntVector(x, y, z), &D->Cells[Index], D, DirtySet, x, y, z))
                DirtySet.Add(Pair.Key);
        }

        const bool bChunkMoved = DirtySet.Num() > PrevDirtyCount || DirtySet.Contains(Pair.Key);
        if (!bChunkMoved) Entry.bSettled = true;
    }

    for (const FIntVector& DC : DirtySet)
    {
        if (FChunkEntry* E = ChunkMap.Find(DC))
            if (E->Data) E->Data->bMeshDirty = true;
        DirtyArray.Add(DC);
    }

    return DirtyArray;
}

// ---------------------------------------------------------------------------
// Core cell physics
// FIX #33: maintain WaterCellCount on transitions
// FIX #40: randomize neighbour order using voxel position hash
// ---------------------------------------------------------------------------
bool FVoxelWaterSimulator::SimCell(const FIntVector& WV, uint8* SrcCell,
                                    FVoxelWaterData* SrcData,
                                    TSet<FIntVector>& DirtyChunks,
                                    int32 x, int32 y, int32 z)
{
    if (!SrcCell || *SrcCell == WATER_EMPTY) return false;
 
    const bool  bIsSource = (*SrcCell == WATER_SOURCE);
    const uint8 MyLevel   = bIsSource ? WATER_FULL : *SrcCell;
    bool        bChanged  = false;
 
    const int32 S  = ChunkSize;
    const int32 S2 = S * S;
 
    auto GetLevelLocal = [&](int32 lx, int32 ly, int32 lz, const FIntVector& Abs) -> uint8 {
        if (lx>=0&&lx<S&&ly>=0&&ly<S&&lz>=0&&lz<S && SrcData) 
            return (SrcData->Cells[lx+ly*S+lz*S2] == WATER_SOURCE) ? WATER_FULL : SrcData->Cells[lx+ly*S+lz*S2];
        return GetLevel(Abs);
    };
    auto IsSolidLocal = [&](int32 lx, int32 ly, int32 lz, const FIntVector& Abs) -> bool {
        if (lx>=0&&lx<S&&ly>=0&&ly<S&&lz>=0&&lz<S && SrcData) 
            return SrcData->SolidCells[lx+ly*S+lz*S2];
        return IsSolidAt(Abs);
    };
    auto GetCellLocal = [&](int32 lx, int32 ly, int32 lz, const FIntVector& Abs) -> uint8* {
        if (lx>=0&&lx<S&&ly>=0&&ly<S&&lz>=0&&lz<S && SrcData) 
            return &SrcData->Cells[lx+ly*S+lz*S2];
        return CellPtr(Abs);
    };
 
    // ---- GRAVITY ----
    const FIntVector Below(WV.X, WV.Y, WV.Z - 1);
    if (!IsSolidLocal(x, y, z - 1, Below) && GetLevelLocal(x, y, z - 1, Below) < WATER_FULL)
    {
        uint8* BC = GetCellLocal(x, y, z - 1, Below);
        if (BC)
        {
            const uint8 BelowLevel = (*BC == WATER_SOURCE) ? WATER_FULL : *BC;
            const uint8 Space      = WATER_FULL - BelowLevel;
            const uint8 Transfer   = FMath::Min(MyLevel, Space);
            if (Transfer > 0)
            {
                if (z > 0)
                {
                    const bool bBelowWasEmpty = (*BC == WATER_EMPTY);
                    if (*BC != WATER_SOURCE)
                        *BC = FMath::Min((int32)(*BC) + Transfer, (int32)WATER_FULL);
                    if (bBelowWasEmpty && *BC != WATER_EMPTY && SrcData)
                        SrcData->WaterCellCount++;
                }
                else if (FChunkEntry* BE = ChunkMap.Find(ToChunkCoord(Below)))
                {
                    const bool bBelowWasEmpty = (*BC == WATER_EMPTY);
                    if (*BC != WATER_SOURCE)
                        *BC = FMath::Min((int32)(*BC) + Transfer, (int32)WATER_FULL);
                    DirtyChunks.Add(ToChunkCoord(Below));
                    if (bBelowWasEmpty && *BC != WATER_EMPTY && BE->Data)
                        BE->Data->WaterCellCount++;
                }
 
                if (!bIsSource)
                {
                    const bool bWasNonEmpty = (*SrcCell != WATER_EMPTY);
                    *SrcCell = (MyLevel <= Transfer) ? WATER_EMPTY : MyLevel - Transfer;
                    if (bWasNonEmpty && *SrcCell == WATER_EMPTY && SrcData)
                        SrcData->WaterCellCount = FMath::Max(0, SrcData->WaterCellCount - 1);
                }
                bChanged = true;
            }
        }
        if (!bIsSource && *SrcCell == WATER_EMPTY) return bChanged;
    }
 
    // ---- LATERAL SPREAD ----
    const uint8 CurrentLevel = bIsSource ? WATER_FULL : *SrcCell;
    if (CurrentLevel == WATER_EMPTY) return bChanged;
 
    const bool bBelowBlocked = IsSolidLocal(x, y, z - 1, Below) || GetLevelLocal(x, y, z - 1, Below) >= WATER_FULL || !GetCellLocal(x, y, z - 1, Below);
    if (!bBelowBlocked) return bChanged;
 
    const FIntVector Neighbours[4] = {
        {WV.X+1,WV.Y,WV.Z},{WV.X-1,WV.Y,WV.Z},
        {WV.X,WV.Y+1,WV.Z},{WV.X,WV.Y-1,WV.Z},
    };
    const int32 NeighCoords[4][2] = {
        {x+1, y}, {x-1, y}, {x, y+1}, {x, y-1}
    };
 
    int32 Order[4] = {0, 1, 2, 3};
    {
        const uint32 H = (uint32)(WV.X * 2654435761u ^ WV.Y * 2246822519u ^ WV.Z * 3266489917u);
        const int32 A = H & 3;
        const int32 B = (H >> 2) & 3;
        Swap(Order[A], Order[3]);
        Swap(Order[B], Order[2]);
    }
 
    for (int32 i = 0; i < 4; ++i)
    {
        const int32 idx = Order[i];
        const FIntVector& NV = Neighbours[idx];
        const int32 nx = NeighCoords[idx][0], ny = NeighCoords[idx][1];
 
        if (IsSolidLocal(nx, ny, z, NV)) continue;
        if (GetLevelLocal(nx, ny, z, NV) >= CurrentLevel) continue;
 
        uint8* NC = GetCellLocal(nx, ny, z, NV);
        if (!NC) continue;
 
        const bool bInChunk = (nx >= 0 && nx < S && ny >= 0 && ny < S);
        if (bInChunk)
        {
            const bool bNWasEmpty = (*NC == WATER_EMPTY);
            if (*NC != WATER_SOURCE) *NC += 1;
            if (bNWasEmpty && *NC != WATER_EMPTY && SrcData) 
                SrcData->WaterCellCount++;
        }
        else if (FChunkEntry* NE = ChunkMap.Find(ToChunkCoord(NV)))
        {
            const bool bNWasEmpty = (*NC == WATER_EMPTY);
            if (*NC != WATER_SOURCE) *NC += 1;
            DirtyChunks.Add(ToChunkCoord(NV));
            if (bNWasEmpty && *NC != WATER_EMPTY) NE->Data->WaterCellCount++;
        }
 
        if (!bIsSource)
        {
            const bool bWasNonEmpty = (*SrcCell != WATER_EMPTY);
            if (*SrcCell > 0) *SrcCell -= 1;
            if (bWasNonEmpty && *SrcCell == WATER_EMPTY && SrcData)
                SrcData->WaterCellCount = FMath::Max(0, SrcData->WaterCellCount - 1);
            bChanged = true;
        }
        if (!bIsSource && *SrcCell == WATER_EMPTY) break;
    }
 
    return bChanged;
}

void FVoxelWaterSimulator::ClearAll()
{
    for (auto& Pair : ChunkMap)
        if (Pair.Value.Data) Pair.Value.Data->Reset(); // also zeroes WaterCellCount
}
