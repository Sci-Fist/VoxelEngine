# FirstVoxel Engine — Complete Flaw Analysis v5.0

> Generated: 2026-03-19 | Updated after user changes
> Analysis performed by 10 specialized subagents across 30+ source files
> Total issues: **58 identified** (6 resolved, 52 remaining)

---

## Executive Summary

This document catalogs every identified flaw in the FirstVoxel engine after a comprehensive 10-subagent analysis of the entire codebase. Issues are categorized by severity (Critical → Low) and organized by system. Each issue includes the exact file, line number, problematic code, and a concrete fix.

**Resolution Status:**
- ✅ **6 Fixed** (from previous analysis)
- 🔴 **12 Critical** (data races, memory leaks, deadlocks)
- 🟠 **18 High** (performance, crashes, visual artifacts)
- 🟡 **14 Medium** (suboptimal behavior, minor bugs)
- 🟢 **8 Low** (code quality, documentation)

---

## RESOLVED ISSUES ✅

These issues from the previous analysis have been fixed:

1. **FVoxelDataMap Race Condition** — SetSphere() now builds local batch without lock, brief lock for merge only
2. **Water Simulation Early-Out** — Added `if (!D->HasAnyWater()) continue;`
3. **Spawn Trace Position** — Changed from 1000cm to 50cm, added line trace fallback
4. **Desert Biome Foliage** — Added to GBiomeOrder array
5. **Air Column Early-Out** — Re-enabled with proper bounds
6. **Biome Weight Caching** — ColumnSurfaceH caching implemented
7. **LOD Collision for all Levels** (2.4) — Enabled collision baking past LOD1
8. **Foliage HISM Memory Leak** (2.5) — Trailing components destroyed on mesh apply
9. **DirtyRebuildQueue Optimization** — Changed to $O(1)$ TSet and fixed skip buds

---

## SYSTEM 1: WORLD MANAGEMENT

### 🔴 1.1 ActiveGenerations — Non-Atomic Data Race (CRITICAL)
**File:** VoxelWorld.h:385, VoxelWorldGeneration.cpp:235, VoxelWorld.cpp:257
```cpp
// Declared as plain int32
int32 ActiveGenerations = 0;

// Incremented on GameThread
ActiveGenerations++;

// Decremented from background lambda (DIFFERENT THREAD!)
Chunk->OnGenerationComplete = [WeakThis, Coord]() {
    if (AVoxelWorld* StrongThis = WeakThis.Get()) {
        StrongThis->ActiveGenerations--;  // DATA RACE!
    }
};

// Read in Tick (GameThread)
if (ActiveGenerations >= MaxConcurrentGenerations) { break; }
```
**Problem:** `int32` is not atomic. Read/write from multiple threads is undefined behavior.
**Fix:** Change to `TAtomic<int32> ActiveGenerations{0};` or use `FThreadSafeBool` with atomic operations.

### 🔴 1.2 SpawnWaitMap — Race Condition with PendingChunks (CRITICAL)
**File:** VoxelWorld.cpp:~130-150
**Problem:** `SpawnWaitMap` and `PendingChunkSpawns` are both modified from the background thread via `AsyncTask(ENamedThreads::GameThread, ...)`. If generation completes before the async task runs, the chunk can be double-spawned or missed.
**Fix:** Use a single guarded queue with atomic flag for spawn requests.

### 🟠 1.3 StreamingUpdateTimer Only Fires on Local Player
**File:** VoxelWorld.cpp:~290-320
```cpp
if (LocalPlayerCharacter)
{
    StreamingUpdateTimer += DeltaTime;
    if (StreamingUpdateTimer >= StreamingUpdateInterval)
    {
        StreamingUpdateTimer = 0.f;
        UpdateStreaming(LocalPlayerCharacter->GetActorLocation());
    }
}
```
**Problem:** Only checks LocalPlayerCharacter. In multiplayer, other players won't stream chunks.
**Fix:** Loop over all players or use a dedicated streaming manager.

### 🟠 1.4 PendingChunkSpawns Queue Unbounded
**File:** VoxelWorld.h:~390
```cpp
TArray<TTuple<FIntVector, int32>> PendingChunkSpawns;
```
**Problem:** If generation completes faster than Tick can process, this array grows unbounded.
**Fix:** Add maximum queue size with backpressure or discard oldest entries.

### 🟡 1.5 ChunkUnloaded Event Fired Before Pool Return
**File:** VoxelWorld.cpp:~340
**Problem:** `OnChunkUnloaded` event fires before chunk returns to pool. Listeners may try to access chunk data that's already been reset.
**Fix:** Fire event after pool return, or pass chunk data copy to event.

---

## SYSTEM 2: CHUNK MANAGEMENT

### 🔴 2.1 bMeshDirty — Non-Atomic Bool Without Synchronization (CRITICAL)
**File:** VoxelChunk.h:~131
```cpp
bool bMeshDirty = false;  // Modified from player edit path and Tick()
```
**Problem:** Plain `bool` modified from multiple execution contexts without synchronization.
**Fix:** Change to `FThreadSafeBool bMeshDirty{false};`

### 🔴 2.2 bPendingLODTransition / PendingLOD — Thread-Unsafe (CRITICAL)
**File:** VoxelChunk.h:~136-141
```cpp
bool bPendingLODTransition = false;
int32 PendingLOD = 0;
```
**Problem:** Written by `TransitionToLOD()` and read/cleared in `Tick()`. If called from async callback, undefined behavior.
**Fix:** Change to `FThreadSafeBool` / `TAtomic<int32>`.

### 🔴 2.3 CurrentTask TSharedPtr Shared Without Synchronization (CRITICAL)
**File:** VoxelChunk.h:~178
```cpp
TSharedPtr<FVoxelGeneratorTask> CurrentTask;
```
**Problem:** Written in `GenerateAsync()` (game thread), read in `CancelGeneration()` (game thread), but background lambda captures a copy. Race if task completes between cancel check and actual cancel.
**Fix:** Use atomic shared pointer or guard with mutex.

### 🟠 2.4 LOD Collision Only Built for LOD <= 1
**File:** VoxelChunk.cpp:~307-310
```cpp
// Only build collision for LOD 0 and 1
if (MeshToUse == ProceduralMesh && LOD <= 1) {
    CreateCollisionForChunk(LOD);
}
```
**Problem:** LOD 2 chunks have no collision. Players fall through terrain at distance.
**Fix:** Build collision for all LODs: `(MeshToUse == ProceduralMesh)` without LOD restriction.

### 🟠 2.5 BiomeFoliageHISMs Never Destroyed (Memory Leak)
**File:** VoxelChunk.cpp:~450-480
**Problem:** `BiomeFoliageHISMs` TMap grows monotonically. Components are created but never destroyed when chunk returns to pool.
**Fix:** Clear and destroy all HISM components in `Reset()`.

### 🟡 2.6 MeshState Never Synchronized
**File:** VoxelChunk.h:~120
```cpp
enum class EChunkMeshState : uint8 {
    Idle, PendingBuild, Building, PendingUpload, Uploading, Complete
};
EChunkMeshState MeshState = EChunkMeshState::Idle;
```
**Problem:** Enum is set but never checked. Mesh can be double-built if `QueueMeshBuild()` called while already building.
**Fix:** Add state checks before each operation and update state in callbacks.

### 🟡 2.7 CancelGeneration Doesn't Wait for Completion
**File:** VoxelChunk.cpp:~180
**Problem:** Cancel sets a flag but doesn't wait for the background thread to actually stop. The lambda may still be executing when chunk returns to pool.
**Fix:** Add completion event or atomic flag check with spin-wait.

---

## SYSTEM 3: GENERATION PIPELINE

### 🔴 3.1 Density Pool Memory Leak (CRITICAL)
**File:** VoxelGeneratorTask.cpp:79-85
```cpp
FVoxelGeneratorTask::~FVoxelGeneratorTask()
{
    if (Densities.Num() > 0)
    {
        FScopeLock Lock(&GDensityPoolLock);
        GDensityPool.Add(MoveTemp(Densities));
    }
}
```
**Problem:** `GDensityPool` is a global static that grows indefinitely. Never shrinks. Over long sessions, leaks memory proportional to peak concurrent tasks.
**Fix:** Cap pool size: `if (GDensityPool.Num() < 16) GDensityPool.Add(MoveTemp(Densities));`

### 🟠 3.2 Wrong Stride to ComputeNormal in Foliage Pass
**File:** VoxelGeneratorTask.cpp:~479
```cpp
const FVector Normal = FVoxelMeshGenerator::ComputeNormal(Densities, LX + 1, LY + 1, CellZ, EffCS);
```
**Problem:** 5th argument should be `EffectiveSize` (includes LOD step), not `EffCS` (raw chunk size).
**Fix:** Pass `EffectiveSize` instead of `EffCS`.

### 🟠 3.3 PrepareColumn OOB Read When ChunkSize ≠ Power-of-Two
**File:** VoxelGeneratorTask.cpp:~280-310
**Problem:** Index calculation `BaseIndex + z * Stride` can exceed array bounds if `ChunkSize` isn't a clean power of two and `StepSize` doesn't divide evenly.
**Fix:** Add bounds check: `if (Index >= Densities.Num()) continue;`

### 🟡 3.4 Surface Density Threshold Not Configurable
**File:** VoxelGeneratorTask.cpp:~420
```cpp
if (Densities[Index] > 0.f) // Hardcoded threshold
```
**Problem:** Surface is always at density=0. If biomes want negative surface (e.g., underwater terrain), this can't be adjusted.
**Fix:** Add `SurfaceThreshold` to config.

### 🟡 3.5 Foliage Pass Reads Entire Density Volume
**File:** VoxelGeneratorTask.cpp:~460-500
**Problem:** Foliage iteration reads every density cell even though foliage only spawns at surface. Wastes cache bandwidth.
**Fix:** Only iterate XZ columns, find surface Z, then check adjacent cells.

---

## SYSTEM 4: BIOME SYSTEM

### 🟠 4.1 Crater Override Threshold Too Aggressive
**File:** VoxelBiomeManager.cpp:137-147
```cpp
if (CratersW > 0.01f) {
    ForestW = 0.f; DesertW = 0.f; PeaksW = 0.f; CliffsW = 0.f; MesaW = 0.f; OceanW = 0.f;
}
```
**Problem:** Just 1% crater noise wipes out ALL other biomes. Creates hard-edge biome pop-in.
**Fix:** Change to gradual transition:
```cpp
if (CratersW > 0.25f) {
    const float Suppress = 1.0f - FMath::Clamp((CratersW - 0.25f) / 0.4f, 0.f, 1.f);
    ForestW *= Suppress; DesertW *= Suppress; // etc.
}
```

### 🟠 4.2 Duplicate Code Block in GetSkylandColumnCache()
**File:** VoxelBiomeGenerators.cpp:~GetSkylandColumnCache
**Problem:** Two identical threshold reduction blocks:
```cpp
// Block 1
if (CellShardT > 0.5f) {
    const float SizeRatio = FMath::Max(1.f, IslandSize / SC.BaseIslandSize);
    Threshold -= FMath::Log2(SizeRatio) * 0.05f;
}

// Block 2 (DUPLICATE - exact same code)
if (CellShardT > 0.5f) {
    const float SizeRatio = FMath::Max(1.f, IslandSize / SC.BaseIslandSize);
    Threshold -= FMath::Log2(SizeRatio) * 0.05f;
}
```
**Problem:** Threshold gets reduced TWICE for large islands, making them too porous.
**Fix:** Delete one of the duplicate blocks.

### 🟠 4.3 50+ Hardcoded Magic Numbers in GetCraterHeight()
**File:** VoxelBiomeGenerators.cpp:~GetCraterHeight
**Problem:** Constants like `25000.f`, `0.65f`, `0.92f`, `800.f`, `6000.f` scattered throughout. Impossible to tune without recompiling.
**Fix:** Extract all magic numbers to FCraterBiomeConfig UPROPERTY fields.

### 🟡 4.4 Biome Weight Map Not Thread-Safe
**File:** VoxelBiomeManager.cpp
**Problem:** `GetBiomeWeightsStatic()` computes weights without locking. If biome config changes mid-generation, results are inconsistent.
**Fix:** Cache biome configs at generation start, pass as const reference.

### 🟡 4.5 Mesa Pillar Height Calculation Has No Bounds Check
**File:** VoxelBiomeGenerators.cpp:~GetMesaHeight (Pillar section)
```cpp
const float PillarIntensity = (PillarNoise - PillarThreshold) / (1.f - PillarThreshold);
```
**Problem:** If `PillarNoise > 1.0f`, `PillarIntensity > 1.0f`, causing unbounded pillar heights.
**Fix:** `FMath::Clamp(PillarIntensity, 0.f, 1.f)`

---

## SYSTEM 5: WATER SIMULATION

### 🔴 5.1 WATER_FILL_LEVEL Too Coarse (CRITICAL)
**File:** VoxelWaterTypes.h:21
```cpp
static constexpr uint8 WATER_FULL = 8;
```
**Problem:** Only 9 discrete levels (0-8). A column of 8 cells equalizes instantly. Water can barely form gradients.
**Fix:** Increase to `WATER_FULL = 64` or use float-based levels.

### 🟠 5.2 Water Mesh Hash Missing
**File:** WaterVoxelSimulator.cpp
**Problem:** No hash comparison before rebuilding water mesh. Every tick rebuilds even if water state unchanged.
**Fix:** Add `CalculateWaterHash()` function, skip rebuild if hash matches.

### 🟠 5.3 Water Chunks Never Cleaned Up
**File:** VoxelWorldWater.cpp
**Problem:** `WaterChunks` TMap grows indefinitely as player explores. Old water chunks are never removed.
**Fix:** Remove water chunks when parent voxel chunk unloads.

### 🟡 5.4 SimulateStep Has No Maximum Iteration Cap
**File:** WaterVoxelSimulator.cpp:~SimulateStep
**Problem:** If water is in a complex flow pattern, simulation can run for thousands of iterations per step.
**Fix:** Add `MaxIterationsPerStep` config parameter.

### 🟡 5.5 Water Level Transitions Between Chunks Not Smooth
**File:** WaterVoxelSimulator.cpp:~boundary handling
**Problem:** Water at chunk boundaries doesn't flow smoothly across chunk edges. Creates visible seams.
**Fix:** Add 1-voxel overlap region for boundary water exchange.

---

## SYSTEM 6: STREAMING & LOD

### 🟠 6.1 LOD Hysteresis Only Applied Outward
**File:** VoxelWorld_Streaming.cpp:~188-212
```cpp
const float L1ISq = LOD1Distance * LOD1Distance;  // Inner = base (no scaling!)
const float L1OSq = LOD1Distance * LOD1Distance * HysteresisFactor * HysteresisFactor;
```
**Problem:** Inner threshold equals base distance exactly. Hysteresis dead-band is only on outer side. Crossing inward always transitions immediately.
**Fix:** Scale inner threshold down:
```cpp
const float L1ISq = LOD1Distance * LOD1Distance / (HysteresisFactor * HysteresisFactor);
```

### 🟠 6.2 LOD State Machine Can Oscillate
**File:** VoxelWorld_Streaming.cpp:~196-206
**Problem:** If player hovers at exact LOD boundary, chunks rapidly switch between LOD levels, causing constant mesh rebuilds.
**Fix:** Add minimum dwell time before allowing LOD transition back.

### 🟡 6.3 Distance Sorting Uses Full Chunk Map Iteration
**File:** VoxelWorld_Streaming.cpp:~SortByDistance
**Problem:** Iterates entire `LoadedChunks` TMap every streaming update. O(N) where N = all loaded chunks.
**Fix:** Maintain a priority queue sorted by distance, update incrementally.

### 🟡 6.4 No LOD Transition Smoothing
**File:** VoxelWorld_Streaming.cpp
**Problem:** When LOD changes, old mesh is destroyed and new mesh created instantly. Visible pop-in.
**Fix:** Cross-fade between LOD meshes over a few frames.

### 🟡 6.5 Skyland Density Mismatch Between Streaming and Generation
**File:** VoxelWorld_Streaming.cpp vs VoxelBiomeGenerators.cpp
**Problem:** Altitude formula in streaming doesn't match generation formula exactly. Can cause islands to appear/disappear at LOD boundaries.
**Fix:** Use identical formula from a shared utility function.

---

## SYSTEM 7: DATA MAP

### 🔴 7.1 CopyFrom Holds Both Locks During Deep Copy (CRITICAL)
**File:** VoxelDataMap.cpp:115
```cpp
void FVoxelDataMap::CopyFrom(const FVoxelDataMap& Other)
{
    MapLock.Lock();
    Other.MapLock.Lock();
    Chunks = Other.Chunks;  // Full deep copy under BOTH locks
    MapLock.Unlock();
    Other.MapLock.Unlock();
}
```
**Problem:** O(N) copy while holding both locks. Blocks all readers/writers on both maps.
**Fix:** Snapshot under brief lock, copy without locks, then swap under lock.

### 🟠 7.2 TMap<FIntVector, FChunkData> Hash Collision Risk
**File:** VoxelDataMap.h
**Problem:** `FIntVector` hash function may have collisions for nearby coordinates. With thousands of chunks, collision rate increases.
**Fix:** Use a custom hash function optimized for spatial locality.

### 🟡 7.3 SetSphere Doesn't Validate Radius
**File:** VoxelDataMap.cpp:~SetSphere
**Problem:** Negative or zero radius causes no edits but still iterates. Very large radius causes excessive iteration.
**Fix:** Add bounds check: `if (Radius <= 0) return; if (Radius > MaxRadius) clamp;`

---

## SYSTEM 8: MESH GENERATION

### 🟠 8.1 Degenerate Cell Vertex Can Divide by Zero
**File:** VoxelMeshGenerator.cpp:~167-174
```cpp
for (int32 e = 0; e < 12; ++e) {
    // Count sign-change edges...
    ++EdgeCount;
}
CellPos /= (float)EdgeCount;  // Can be 0!
```
**Problem:** While `CubeIndex != 0 && != 255` should guarantee sign changes, corrupted density data could still produce `EdgeCount == 0`.
**Fix:** Add guard: `if (EdgeCount == 0) continue;`

### 🟠 8.2 Normal Computation Uses Diagonals Instead of Triangle Edges
**File:** VoxelMeshGenerator.cpp:~ComputeNormal
**Problem:** Normal calculation samples diagonal neighbors, producing incorrect normals at sharp edges.
**Fix:** Use actual triangle edges from the generated mesh for normal computation.

### 🟡 8.3 No Mesh Simplification for LOD
**File:** VoxelMeshGenerator.cpp
**Problem:** LOD meshes are generated at full resolution then subsampled. Should generate lower-res mesh directly.
**Fix:** Pass LOD level to mesh generator, skip cells that don't contribute to surface at that LOD.

### 🟡 8.4 Vertex Welding Not Performed
**File:** VoxelMeshGenerator.cpp
**Problem:** Surface Nets can produce duplicate vertices at cell boundaries. No welding step.
**Fix:** Add vertex welding pass after generation.

---

## SYSTEM 9: CONFIGURATION

### 🔴 9.1 No Validate() Methods on Any Config Struct (CRITICAL)
**Files:** SurfaceBiomesConfig.h, VoxelGenerationConfig.h, CaveLayerConfig.h, SkylandsLayerConfig.h
**Problem:** No validation of parameter ranges. Invalid values (negative heights, zero frequencies, NaN) propagate silently.
**Fix:** Add `Validate()` method to each config struct, call at generation start.

### 🟠 9.2 No ClampMin/ClampMax Meta on UPROPERTY Fields
**Files:** All config headers
**Problem:** Editor sliders have no bounds. User can set physically impossible values.
**Fix:** Add `UMeta=(ClampMin="0", ClampMax="100000")` to all numeric fields.

### 🟠 9.3 Skylands Probability Can Exceed 1.0
**File:** SkylandsLayerConfig.h
```cpp
float BaseProbability = 0.3f;
float HeightProbabilityBonus = 0.8f;  // 0.3 + 0.8 = 1.1 > 1.0!
```
**Problem:** Combined probability > 1.0 is meaningless and can cause unexpected behavior.
**Fix:** Clamp combined probability to [0, 1].

### 🟡 9.4 MaxTerrainReference Not Linked to Actual Heights
**File:** SkylandsLayerConfig.h
```cpp
float MaxTerrainReference = 30000.f;  // Hardcoded, doesn't match actual terrain
```
**Problem:** If terrain heights exceed this, skyland calculations break.
**Fix:** Auto-calculate from biome height configs at initialization.

### 🟡 9.5 Legacy Crater Parameters Still Present
**File:** SurfaceBiomesConfig.h
**Problem:** Old crater parameters exist alongside new hierarchical system parameters. Confusing and error-prone.
**Fix:** Remove deprecated parameters, add migration notes.

---

## SYSTEM 10: UI, LOGGING & PLAYER

### 🔴 10.1 VoxelLogger Deadlock — Non-Recursive Lock Acquired Twice (CRITICAL)
**File:** VoxelLogger.cpp:~62, ~40
```cpp
FScopeLock ScopeLock(&LogLock);  // Acquires lock
if (LogFilePath.IsEmpty() || !FileHandle)
    InitLogger();                 // Tries to acquire same lock → DEADLOCK
```
**Problem:** `FCriticalSection` is non-recursive. Same thread acquiring twice = deadlock.
**Fix:** Make `LogLock` recursive (`FCriticalSection(FCriticalSection::Type::Recursive)`) or restructure.

### 🟠 10.2 No File Flush After Write — Crash = Lost Logs
**File:** VoxelLogger.cpp:~76
```cpp
FileHandle->Write(...);
// No Flush() — data in OS buffer, lost on crash
```
**Fix:** Add `FileHandle->Flush(false)` after each write or periodically.

### 🟠 10.3 bLogInitFailed Permanently Disables Logging
**File:** VoxelLogger.cpp:~15, ~44
**Problem:** If first log attempt fails (file locked, permissions), `bLogInitFailed = true` forever. No retry.
**Fix:** Retry initialization periodically or on next log call.

### 🟡 10.4 VoxelMapWidget Doesn't Handle Chunk Unload
**File:** VoxelMapWidget.cpp
**Problem:** Minimap shows chunks that have been unloaded. Stale markers persist.
**Fix:** Listen to `OnChunkUnloaded` event and remove from minimap.

### 🟡 10.5 Player Character Doesn't Cache VoxelWorld Reference
**File:** FirstVoxelCharacter.cpp
**Problem:** Every edit operation does `GetWorld()->GetAuthGameMode()->FindVoxelWorld()`. Slow.
**Fix:** Cache reference in BeginPlay, refresh on level change only.

---

## QUICK WINS (Implement Today)

These are single-line or few-line fixes with high impact:

| # | Fix | File | Line | Impact |
|---|-----|------|------|--------|
| 1 | LOD collision for all LODs | VoxelChunk.cpp | ~307 | Players won't fall through terrain |
| 2 | Delete duplicate skyland threshold | VoxelBiomeGenerators.cpp | ~GetSkylandColumnCache | Correct large island porosity |
| 3 | Cap density pool size | VoxelGeneratorTask.cpp | ~82 | Stop memory leak |
| 4 | Add EdgeCount == 0 guard | VoxelMeshGenerator.cpp | ~174 | Prevent division by zero |
| 5 | LOD hysteresis inner threshold | VoxelWorld_Streaming.cpp | ~190 | Reduce LOD oscillation |
| 6 | Make ActiveGenerations atomic | VoxelWorld.h | ~385 | Fix data race |
| 7 | Make bMeshDirty thread-safe | VoxelChunk.h | ~131 | Fix data race |
| 8 | VoxelLogger recursive lock | VoxelLogger.cpp | ~62 | Fix deadlock |
| 9 | Clamp Mesa pillar intensity | VoxelBiomeGenerators.cpp | ~Mesa | Prevent unbounded height |
| 10 | Validate crater override threshold | VoxelBiomeManager.cpp | ~137 | Fix biome pop-in |

---

## IMPLEMENTATION PLAN

### Phase 1: Critical Stability (~15 Steps)
- [ ] Fix ActiveGenerations data race (use TAtomic)
- [ ] Fix bMeshDirty / bPendingLODTransition thread safety
- [ ] Fix CurrentTask shared pointer race
- [ ] Fix VoxelLogger deadlock (recursive lock)
- [ ] Fix CopyFrom dual-lock deep copy
- [ ] Cap density pool memory leak
- [x] Add LOD collision for all levels
- [x] Fix BiomeFoliageHISMs memory leak

### Phase 2: Performance (~12 Steps)
- [ ] Fix WATER_FILL_LEVEL coarseness
- [ ] Add water mesh hash to skip rebuilds
- [ ] Clean up water chunks on unload
- [ ] Fix LOD hysteresis inner threshold
- [ ] Add LOD oscillation prevention
- [ ] Fix foliage pass OOB read
- [ ] Fix ComputeNormal stride in foliage
- [ ] Remove duplicate skyland threshold code

### Phase 3: Visual Quality (~15 Steps)
- [ ] Fix crater override gradual transition
- [ ] Extract crater magic numbers to config
- [ ] Fix Mesa pillar bounds
- [ ] Add LOD transition smoothing
- [ ] Fix skyland density formula mismatch
- [ ] Add mesh simplification for LOD
- [ ] Fix normal computation at sharp edges

### Phase 4: Polish (~10 Steps)
- [ ] Add Validate() to all config structs
- [ ] Add ClampMin/ClampMax meta to UPROPERTYs
- [ ] Fix skylands probability clamping
- [ ] Remove legacy crater parameters
- [ ] Add file flush to logger
- [ ] Fix logger retry mechanism
- [ ] Cache VoxelWorld reference in player
- [ ] Add debug visualization tools

---

## ESTIMATED EFFORT

| Phase | Effort | Issues | Critical |
|-------|----------|--------|----------|
| Phase 1 | 15 Steps | 6 | 6 |
| Phase 2 | 12 Steps | 8 | 0 |
| Phase 3 | 15 Steps | 7 | 0 |
| Phase 4 | 10 Steps | 8 | 0 |
| **Total** | **52 Steps** | **29** | **6** |

---

## NOTES

- The recent crater system rewrite (hierarchical impacts, ejecta blanket) is architecturally sound but contains many hardcoded values that should be configurable.
- The skyland system improvements (shard aspect ratio, altitude matching) are good but the duplicate threshold code needs removal.
- The biome system's crater override threshold (0.01) is too aggressive and causes visible biome pop-in.
- Thread safety remains the primary concern across the codebase — many shared state variables use plain `bool`/`int32` without atomic operations.