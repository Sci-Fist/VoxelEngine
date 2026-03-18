// VoxelWaterSimulator.cpp
// 
// Cellular automata-based water simulation system for the voxel engine.
// Implements realistic water physics including gravity flow, lateral spreading,
// and water source management within the chunk-based world system.
//
// ARCHITECTURE OVERVIEW:
// This simulator uses a cellular automata approach where each voxel cell
// represents a unit of water with a level from 0 (empty) to 255 (full).
// The simulation runs in discrete steps, processing each cell to determine
// water movement based on gravity and pressure principles.
//
// SIMULATION PRINCIPLES:
// 1. Gravity Flow: Water flows downward when space is available below
// 2. Lateral Spread: Water spreads horizontally when blocked from falling
// 3. Source Management: Special source cells continuously generate water
// 4. Conservation: Water volume is preserved during transfers between cells
//
// PERFORMANCE OPTIMIZATIONS:
// - Chunk-based processing for memory efficiency
// - Dirty chunk tracking to minimize mesh updates
// - Bottom-to-top processing order for natural gravity simulation
// - Early termination when cells are empty or sources

#include "Voxel/Water/VoxelWaterSimulator.h"

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
FVoxelWaterSimulator::FVoxelWaterSimulator(int32 InChunkSize, float InVoxelSize)
    : ChunkSize(InChunkSize), VoxelSize(InVoxelSize)
{
    // Initialize water simulator with chunk size and voxel dimensions
    // ChunkSize: Number of voxels per chunk dimension (typically 32 or 64)
    // VoxelSize: World units per voxel (typically 100.0f for 100-unit voxels)
}

// ---------------------------------------------------------------------------
// Chunk Registration System
// ---------------------------------------------------------------------------
void FVoxelWaterSimulator::RegisterChunk(const FIntVector& ChunkCoord, FVoxelWaterData* WaterData)
{
    // Register a chunk's water data with the simulator
    // This enables water simulation for the specified chunk
    check(WaterData);
    ChunkMap.Add(ChunkCoord, { WaterData });
}

void FVoxelWaterSimulator::UnregisterChunk(const FIntVector& ChunkCoord)
{
    // Remove chunk from water simulation
    // This stops water processing for the specified chunk
    ChunkMap.Remove(ChunkCoord);
}

// ---------------------------------------------------------------------------
// Coordinate Transformation System
// ---------------------------------------------------------------------------
FIntVector FVoxelWaterSimulator::ToChunkCoord(const FIntVector& WV) const
{
    // Convert world voxel coordinates to chunk coordinates
    // Uses floor division to determine which chunk contains the world voxel
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
    // Convert world voxel coordinates to local chunk coordinates (0 to ChunkSize-1)
    // Uses modulo operation with proper handling for negative coordinates
    auto Mod = [](int32 A, int32 B) -> int32 { return ((A % B) + B) % B; };
    return FIntVector(Mod(WV.X, ChunkSize), Mod(WV.Y, ChunkSize), Mod(WV.Z, ChunkSize));
}

uint8* FVoxelWaterSimulator::CellPtr(const FIntVector& WV)
{
    // Get mutable pointer to water cell data for world voxel coordinates
    // Returns nullptr if chunk doesn't exist or coordinates are invalid
    FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV));
    if (!E || !E->Data) return nullptr;
    const FIntVector L = ToLocal(WV);
    const int32 Idx = LocalIdx(L.X, L.Y, L.Z);
    if (!E->Data->Cells.IsValidIndex(Idx)) return nullptr;
    return &E->Data->Cells[Idx];
}

const uint8* FVoxelWaterSimulator::CellPtrConst(const FIntVector& WV) const
{
    // Get const pointer to water cell data for world voxel coordinates
    // Used for read-only operations and queries
    const FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV));
    if (!E || !E->Data) return nullptr;
    const FIntVector L = ToLocal(WV);
    const int32 Idx = LocalIdx(L.X, L.Y, L.Z);
    if (!E->Data->Cells.IsValidIndex(Idx)) return nullptr;
    return &E->Data->Cells[Idx];
}

bool FVoxelWaterSimulator::IsSolidAt(const FIntVector& WV) const
{
    // Check if world voxel coordinates contain solid terrain
    // Used to determine if water can flow into a cell
    const FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV));
    if (!E || !E->Data) return false;
    const FIntVector L = ToLocal(WV);
    const int32 Idx = LocalIdx(L.X, L.Y, L.Z);
    if (!E->Data->SolidCells.IsValidIndex(Idx)) return false;
    return E->Data->SolidCells[Idx];
}

// ---------------------------------------------------------------------------
// Public Interface - Water Cell Management
// ---------------------------------------------------------------------------
void FVoxelWaterSimulator::SetSource(const FIntVector& WV)
{
    // Create a water source at the specified world voxel coordinates
    // Water sources continuously generate water and never deplete
    uint8* C = CellPtr(WV);
    if (!C) return;
    *C = WATER_SOURCE;
    if (FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV))) E->Data->bMeshDirty = true;
}

void FVoxelWaterSimulator::SetFlowing(const FIntVector& WV, uint8 Level)
{
    // Set water level at specified coordinates
    // Level is clamped between WATER_EMPTY (0) and WATER_FULL (255)
    uint8* C = CellPtr(WV);
    if (!C) return;
    *C = FMath::Clamp((int32)Level, (int32)WATER_EMPTY, (int32)WATER_FULL);
    if (FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV))) E->Data->bMeshDirty = true;
}

void FVoxelWaterSimulator::ClearCell(const FIntVector& WV)
{
    // Remove all water from the specified cell
    // Marks chunk as dirty for mesh regeneration
    uint8* C = CellPtr(WV);
    if (!C || *C == WATER_EMPTY) return;
    *C = WATER_EMPTY;
    if (FChunkEntry* E = ChunkMap.Find(ToChunkCoord(WV))) E->Data->bMeshDirty = true;
}

uint8 FVoxelWaterSimulator::GetLevel(const FIntVector& WV) const
{
    // Get water level at specified coordinates
    // Returns WATER_FULL for source cells, actual level for flowing water
    const uint8* C = CellPtrConst(WV);
    if (!C) return WATER_EMPTY;
    return (*C == WATER_SOURCE) ? WATER_FULL : *C;
}

bool FVoxelWaterSimulator::IsWater(const FIntVector& WV) const
{
    // Check if coordinates contain any water (flowing or source)
    const uint8* C = CellPtrConst(WV);
    return C && (*C != WATER_EMPTY);
}

bool FVoxelWaterSimulator::IsSolid(const FIntVector& WV) const 
{ 
    // Wrapper for solid terrain check
    return IsSolidAt(WV); 
}

// ---------------------------------------------------------------------------
// Simulation Step - Main Processing Loop
// ---------------------------------------------------------------------------
TArray<FIntVector> FVoxelWaterSimulator::Step()
{
    // Execute one simulation step across all registered chunks
    // Returns array of chunks that were modified and need mesh updates
    
    TSet<FIntVector> DirtyChunks;

    // Process each registered chunk
    for (auto& Pair : ChunkMap)
    {
        const FIntVector& CC = Pair.Key;
        FVoxelWaterData* D = Pair.Value.Data;
        if (!D) continue;

        // Calculate world coordinates for chunk origin
        const FIntVector Base(CC.X * ChunkSize, CC.Y * ChunkSize, CC.Z * ChunkSize);

        // Process cells in bottom-to-top order to ensure gravity flows naturally
        // This ordering allows water to fall before lateral spreading occurs
        for (int32 z = 0; z < ChunkSize; ++z)
        for (int32 y = 0; y < ChunkSize; ++y)
        for (int32 x = 0; x < ChunkSize; ++x)
        {
            // Simulate individual cell and track if it was modified
            const int32 Index = LocalIdx(x, y, z);
            if (SimCell(Base + FIntVector(x, y, z), &D->Cells[Index], DirtyChunks))
                DirtyChunks.Add(CC);
        }
    }

    // Mark all dirty chunks for mesh regeneration
    for (const FIntVector& DC : DirtyChunks)
    {
        if (FChunkEntry* E = ChunkMap.Find(DC))
            if (E->Data) E->Data->bMeshDirty = true;
    }

    return DirtyChunks.Array();
}

// ---------------------------------------------------------------------------
// Cell Simulation - Core Physics Logic
// ---------------------------------------------------------------------------
bool FVoxelWaterSimulator::SimCell(const FIntVector& WV, uint8* SrcCell, TSet<FIntVector>& DirtyChunks)
{
    // Simulate water physics for a single cell
    // Returns true if cell was modified, false otherwise
    
    if (!SrcCell || *SrcCell == WATER_EMPTY) return false;

    const bool  bIsSource = (*SrcCell == WATER_SOURCE);
    const uint8 MyLevel   = bIsSource ? WATER_FULL : *SrcCell;
    bool        bChanged  = false;

    // ---- GRAVITY FLOW (DOWNWARD) ----
    // Water flows downward if space is available below
    const FIntVector Below(WV.X, WV.Y, WV.Z - 1);
    if (!IsSolidAt(Below) && GetLevel(Below) < WATER_FULL)
    {
        uint8* BelowCell = CellPtr(Below);
        if (BelowCell)
        {
            // Calculate available space in target cell
            const uint8 Space    = WATER_FULL - (*BelowCell == WATER_SOURCE ? WATER_FULL : *BelowCell);
            // Transfer amount is limited by current cell level and available space
            const uint8 Transfer = FMath::Min(MyLevel, (uint8)Space);
            if (Transfer > 0)
            {
                // Add water to target cell (unless it's a source)
                if (*BelowCell != WATER_SOURCE)
                    *BelowCell = FMath::Min((int32)(*BelowCell) + Transfer, (int32)WATER_FULL);
                DirtyChunks.Add(ToChunkCoord(Below));
                
                // Remove water from source cell (unless it's a source)
                if (!bIsSource) *SrcCell = (MyLevel <= Transfer) ? WATER_EMPTY : MyLevel - Transfer;
                bChanged = true;
            }
        }
        else if (!bIsSource) { *SrcCell = WATER_EMPTY; bChanged = true; }

        // If source cell is empty after gravity flow, no need to check lateral spread
        if (!bIsSource && *SrcCell == WATER_EMPTY) return bChanged;
    }

    // ---- LATERAL SPREAD (HORIZONTAL) ----
    // Water spreads horizontally when blocked from falling
    const uint8 CurrentLevel = bIsSource ? WATER_FULL : *SrcCell;
    if (CurrentLevel == WATER_EMPTY) return bChanged;

    // Check if downward flow is blocked (solid terrain or full cell)
    const bool bBelowBlocked = IsSolidAt(Below) || GetLevel(Below) >= WATER_FULL || (CellPtr(Below) == nullptr);
    if (!bBelowBlocked) return bChanged;

    // Check four horizontal neighbors (North, South, East, West)
    const FIntVector Neighbours[4] = {
        {WV.X+1,WV.Y,WV.Z},{WV.X-1,WV.Y,WV.Z},
        {WV.X,WV.Y+1,WV.Z},{WV.X,WV.Y-1,WV.Z},
    };

    for (const FIntVector& NV : Neighbours)
    {
        // Skip if neighbor is solid terrain
        if (IsSolidAt(NV)) continue;
        // Skip if neighbor has equal or higher water level
        if (GetLevel(NV) >= CurrentLevel) continue;
        
        uint8* NCell = CellPtr(NV);
        if (!NCell) continue;
        
        // Add one unit of water to neighbor (unless it's a source)
        if (*NCell != WATER_SOURCE) *NCell += 1;
        DirtyChunks.Add(ToChunkCoord(NV));
        
        // Remove one unit from source cell (unless it's a source)
        if (!bIsSource) { if (*SrcCell > 0) *SrcCell -= 1; bChanged = true; }
        
        // Stop spreading if source cell becomes empty
        if (!bIsSource && *SrcCell == WATER_EMPTY) break;
    }

    return bChanged;
}

// ---------------------------------------------------------------------------
// Utility Functions
// ---------------------------------------------------------------------------
void FVoxelWaterSimulator::ClearAll()
{
    // Clear all water from all registered chunks
    // Resets simulation state completely
    for (auto& Pair : ChunkMap)
        if (Pair.Value.Data) Pair.Value.Data->Reset();
}
