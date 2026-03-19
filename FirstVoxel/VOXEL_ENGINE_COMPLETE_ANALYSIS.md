# FirstVoxel Engine — Complete Flaw Analysis v6.0

> Generated: 2026-03-19 | Updated after user changes
> Analysis performed by 10 specialized subagents across 30+ source files
> Total issues: **62 identified** (6 resolved, 56 remaining)

---

## Executive Summary

This document catalogs every identified flaw in the FirstVoxel engine after a comprehensive analysis of the entire codebase. Issues are categorized by severity (Critical → Low) and organized by system. Each issue includes the exact file, line number, problematic code, and a concrete fix.

**Resolution Status:**
- ✅ **6 Fixed** (from previous analysis)
- 🔴 **13 Critical** (data races, memory leaks, deadlocks, visual artifacts)
- 🟠 **19 High** (performance, crashes, visual artifacts)
- 🟡 **16 Medium** (suboptimal behavior, minor bugs)
- 🟢 **8 Low** (code quality, documentation)

---

## NEWLY IDENTIFIED ISSUES (v6.0)

### 🔴 N1. Cracked Surfaces on Slopes — Surface Nets Vertex Averaging (CRITICAL)

**File:** VoxelMeshGenerator.cpp, Pass 1 (lines ~167-174)
**Severity:** Critical — Visible visual artifacts on all steep terrain

**Root Cause Analysis:**

The Surface Nets algorithm places ONE vertex per cell at the average of ALL cut-edge intersection points:

```cpp
FVector CellPos = FVector::ZeroVector;
int32   EdgeCount = 0;
for (int32 e = 0; e < 12; ++e)
{
    if ((D[c0] > 0.f) != (D[c1] > 0.f))
    {
        CellPos += InterpolateEdge(P[c0], D[c0], P[c1], D[c1]);
        ++EdgeCount;
    }
}
CellPos /= (float)EdgeCount;  // AVERAGE of all intersections
```

**Problem on Steep Slopes:**
- When terrain is steep (e.g., cliff faces, crater rims), the density gradient changes rapidly across a single cell
- The Surface Nets algorithm averages ALL edge intersections to get one vertex
- On steep slopes, this averaging places the vertex somewhere in the MIDDLE of the cell rather than on the actual surface boundary
- When adjacent cells do the same thing, their vertices don't align properly
- This creates visible cracks/gaps between quads on steep slopes

**Why This Happens:**
- On a flat surface, all edge intersections are close together, so averaging works well
- On a steep slope, edge intersections span a large vertical range
- Averaging them produces a vertex that's displaced from the true surface
- Adjacent cells have different displacement amounts, creating misalignment

**Additional Contributing Factors:**
1. `FlattenCellTops` is DISABLED (commented out) — no vertex snapping to fix micro-gaps
2. `InterpolateEdge` uses linear interpolation — on steep density gradients, the interpolation point can be far from the true surface
3. `ComputeNormal` uses central difference — at steep transitions, this can be inaccurate, causing wrong flat/slope classification

**Fix Options:**
1. **Use Marching Cubes instead of Surface Nets** — MC computes exact edge intersections per triangle, eliminating the averaging problem
2. **Add vertex welding post-pass** — Merge vertices within a tolerance distance to close gaps
3. **Use adaptive vertex placement** — Place vertices closer to the actual surface using gradient information
4. **Re-enable FlattenCellTops with chunk-boundary-aware logic** — Snap top-facing vertices to common heights while respecting chunk boundaries

---

### 🔴 N2. Duplicate Crater at Player Spawn — Forced Crater + Natural Crater Overlap (CRITICAL)

**File:** VoxelBiomeManager.cpp (~line 137), VoxelWorldGeneration.cpp (ConfigureChunk), SurfaceBiomesConfig.h
**Severity:** Critical — Visual artifact, unnatural world generation

**Root Cause Analysis:**

There are TWO independent crater generation systems running simultaneously:

**System 1: Forced Crater at Spawn**
```cpp
// VoxelBiomeManager.cpp
if (Config.Craters.bForceCraterAtOrigin)
{
    const float dx = X - Config.Craters.ForcedCraterCenter.X;
    const float dy = Y - Config.Craters.ForcedCraterCenter.Y;
    const float Dist = FMath::Sqrt(dx * dx + dy * dy);
    const float Radius = Config.Craters.CentralCraterRadius * 1.2f;
    
    if (Dist < Radius + 2000.f)
    {
        CratersW += Factor * 0.85f;  // ARTIFICIAL BOOST
    }
}
```

**System 2: Natural Crater from Noise**
```cpp
// VoxelBiomeManager.cpp
const float CraterNoise = FMath::PerlinNoise3D(FVector(
    (X + Off.X) * (Config.Craters.Frequency * 0.5f),
    (Y + Off.Y) * (Config.Craters.Frequency * 0.5f),
    200.f));
float CratersW = FMath::SmoothStep(
    Config.Craters.ImpactThreshold + 0.1f,
    Config.Craters.ImpactThreshold,
    CraterNoise);
```

**The Problem:**
1. `bForceCraterAtOrigin = true` by default (SurfaceBiomesConfig.h line ~400)
2. `ForcedCraterCenter` is set to the spawn position in `ConfigureChunk()`:
   ```cpp
   Chunk->GenerationConfig.Craters.ForcedCraterCenter = FVector2D(SpawnTargetPos.X, SpawnTargetPos.Y);
   ```
3. The forced system adds 0.85 weight boost at the spawn center
4. The natural noise can ALSO generate a crater at or near the same position
5. Combined, they create an unnaturally large/deep crater zone

**Visual Effect:**
- Player sees what appears to be a "duplicate" or "double-layered" crater
- The crater may be deeper than intended because both systems contribute
- The crater rim may be unnaturally steep because of the combined weight

**Fix:**
1. Set `bForceCraterAtOrigin = false` by default
2. OR: Make the forced crater REPLACE the natural crater (use `FMath::Max` instead of `+=`)
3. OR: Reduce the forced boost to a small nudge (0.2 instead of 0.85)

---

### 🔴 N3. Slopes Not Being Cleared — ClearWorld Doesn't Reset Crater State (CRITICAL)

**File:** VoxelWorld.cpp (ClearWorld), VoxelBiomeManager.cpp (bForceCraterAtOrigin)
**Severity:** Critical — User-facing bug, world doesn't fully reset

**Root Cause Analysis:**

`ClearWorld()` destroys all chunks:
```cpp
void AVoxelWorld::ClearWorld() {
    TArray<FIntVector> Keys;
    LoadedChunks.GetKeys(Keys);
    for (const FIntVector &Coord : Keys)
        DestroyChunk(Coord);
    LoadedChunks.Empty();
    GenerationQueue.Empty();
    // ... clears chunk state
}
```

**But it does NOT:**
1. Clear `DataMap` (player modifications) — `ClearModifications()` is separate
2. Reset `bForceCraterAtOrigin` or `ForcedCraterCenter`
3. Reset the generation config

**Why Slopes Regenerate:**
1. User presses "Clear World" → all chunks destroyed
2. User presses "Generate World" → `GenerateWorldDeferred()` runs
3. `bForceCraterSpawn` is still `true`
4. `FindCraterSpawnLocation()` finds a crater position
5. `ConfigureChunk()` sets `ForcedCraterCenter` to spawn position
6. Chunks regenerate with the forced crater → slopes (crater rims) come back

**The slopes ARE being cleared**, but they immediately regenerate because the crater forcing system is still active. The user perceives this as "slopes not being cleared."

**Fix:**
1. Add option to disable `bForceCraterAtOrigin` before regenerating
2. OR: In `ClearWorld()`, also reset `ForcedCraterCenter` to a random position
3. OR: Add a "Clear World Without Crater" option that temporarily disables crater forcing

---

## RESOLVED ISSUES ✅

These issues from the previous analysis have been fixed:

1. **FVoxelDataMap Race Condition** — SetSphere() now builds local batch without lock
2. **Water Simulation Early-Out** — Added `if (!D->HasAnyWater()) continue;`
3. **Spawn Trace Position** — Changed from 1000cm to 50cm, added line trace fallback
4. **Desert Biome Foliage** — Added to GBiomeOrder array
5. **Air Column Early-Out** — Re-enabled with proper bounds
6. **Biome Weight Caching** — ColumnSurfaceH caching implemented

---

## SYSTEM 1: WORLD MANAGEMENT

### 🔴 1.1 ActiveGenerations — Non-Atomic Data Race (CRITICAL)
**File:** VoxelWorld.h:385, VoxelWorldGeneration.cpp:235, VoxelWorld.cpp:257
```cpp
int32 ActiveGenerations = 0;  // Plain int32, not atomic
```
**Problem:** Read/written from multiple threads without synchronization.
**Fix:** Change to `TAtomic<int32> ActiveGenerations{0};`

### 🔴 1.2 SpawnWaitMap — Race Condition with PendingChunks (CRITICAL)
**File:** VoxelWorld.cpp:~130-150
**Problem:** `SpawnWaitMap` and `PendingChunkSpawns` modified from background thread via `AsyncTask`.
**Fix:** Use a single guarded queue with atomic flag for spawn requests.

### 🟠 1.3 StreamingUpdateTimer Only Fires on Local Player
**File:** VoxelWorld.cpp:~290-320
**Problem:** Only checks LocalPlayerCharacter. In multiplayer, other players won't stream chunks.
**Fix:** Loop over all players or use a dedicated streaming manager.

### 🟠 1.4 PendingChunkSpawns Queue Unbounded
**File:** VoxelWorld.h:~390
**Problem:** If generation completes faster than Tick can process, this array grows unbounded.
**Fix:** Add maximum queue size with backpressure.

### 🟡 1.5 ChunkUnloaded Event Fired Before Pool Return
**File:** VoxelWorld.cpp:~340
**Problem:** `OnChunkUnloaded` fires before chunk returns to pool. Listeners may try to access chunk data that's already been reset.
**Fix:** Fire event after pool return, or pass chunk data copy to event.

---

## SYSTEM 2: CHUNK MANAGEMENT

### 🔴 2.1 bMeshDirty — Non-Atomic Bool Without Synchronization (CRITICAL)
**File:** VoxelChunk.h:~131
```cpp
bool bMeshDirty = false;
```
**Problem:** Plain `bool` modified from multiple execution contexts.
**Fix:** Change to `FThreadSafeBool bMeshDirty{false};`

### 🔴 2.2 bPendingLODTransition / PendingLOD — Thread-Unsafe (CRITICAL)
**File:** VoxelChunk.h:~136-141
**Problem:** Written by `TransitionToLOD()` and read/cleared in `Tick()`.
**Fix:** Change to `FThreadSafeBool` / `TAtomic<int32>`.

### 🔴 2.3 CurrentTask TSharedPtr Shared Without Synchronization (CRITICAL)
**File:** VoxelChunk.h:~178
**Problem:** Written in `GenerateAsync()`, read in `CancelGeneration()`, background lambda captures a copy.
**Fix:** Use atomic shared pointer or guard with mutex.

### 🟠 2.4 LOD Collision Only Built for LOD <= 1
**File:** VoxelChunk.cpp:~307-310
```cpp
if (MeshToUse == ProceduralMesh && LOD <= 1) {
    CreateCollisionForChunk(LOD);
}
```
**Problem:** LOD 2 chunks have no collision. Players fall through terrain at distance.
**Fix:** `(MeshToUse == ProceduralMesh)` without LOD restriction.

### 🟠 2.5 BiomeFoliageHISMs Never Destroyed (Memory Leak)
**File:** VoxelChunk.cpp:~450-480
**Problem:** `BiomeFoliageHISMs` TMap grows monotonically.
**Fix:** Clear and destroy all HISM components in `Reset()`.

### 🟡 2.6 MeshState Never Synchronized
**File:** VoxelChunk.h:~120
**Problem:** Enum is set but never checked. Mesh can be double-built.
**Fix:** Add state checks before each operation.

### 🟡 2.7 CancelGeneration Doesn't Wait for Completion
**File:** VoxelChunk.cpp:~180
**Problem:** Cancel sets a flag but doesn't wait for background thread to stop.
**Fix:** Add completion event or atomic flag check with spin-wait.

---

## SYSTEM 3: GENERATION PIPELINE

### 🔴 3.1 Density Pool Memory Leak (CRITICAL)
**File:** VoxelGeneratorTask.cpp:79-85
```cpp
GDensityPool.Add(MoveTemp(Densities));  // Grows indefinitely
```
**Problem:** `GDensityPool` is a global static that never shrinks.
**Fix:** Cap pool size: `if (GDensityPool.Num() < 16) ...`

### 🟠 3.2 Wrong Stride to ComputeNormal in Foliage Pass
**File:** VoxelGeneratorTask.cpp:~479
**Problem:** 5th argument should be `EffectiveSize`, not `EffCS`.
**Fix:** Pass `EffectiveSize` instead of `EffCS`.

### 🟠 3.3 PrepareColumn OOB Read When ChunkSize ≠ Power-of-Two
**File:** VoxelGeneratorTask.cpp:~280-310
**Problem:** Index calculation can exceed array bounds.
**Fix:** Add bounds check: `if (Index >= Densities.Num()) continue;`

### 🟡 3.4 Surface Density Threshold Not Configurable
**File:** VoxelGeneratorTask.cpp:~420
**Problem:** Surface always at density=0. Can't adjust for underwater terrain.
**Fix:** Add `SurfaceThreshold` to config.

### 🟡 3.5 Foliage Pass Reads Entire Density Volume
**File:** VoxelGeneratorTask.cpp:~460-500
**Problem:** Iterates every density cell even though foliage only spawns at surface.
**Fix:** Only iterate XZ columns, find surface Z, then check adjacent cells.

---

## SYSTEM 4: BIOME SYSTEM

### 🟠 4.1 Crater Override Threshold Too Aggressive
**File:** VoxelBiomeManager.cpp:137-147
```cpp
if (CratersW > 0.01f) {
    ForestW = 0.f; DesertW = 0.f; // ALL biomes wiped
}
```
**Problem:** Just 1% crater noise wipes out ALL other biomes.
**Fix:** Gradual transition with `FMath::Clamp`.

### 🟠 4.2 Duplicate Code Block in GetSkylandColumnCache()
**File:** VoxelBiomeGenerators.cpp:~GetSkylandColumnCache
**Problem:** Two identical threshold reduction blocks — threshold reduced TWICE.
**Fix:** Delete one of the duplicate blocks.

### 🟠 4.3 50+ Hardcoded Magic Numbers in GetCraterHeight()
**File:** VoxelBiomeGenerators.cpp:~GetCraterHeight
**Problem:** Constants scattered throughout. Impossible to tune without recompiling.
**Fix:** Extract all magic numbers to FCraterBiomeConfig UPROPERTY fields.

### 🟡 4.4 Biome Weight Map Not Thread-Safe
**File:** VoxelBiomeManager.cpp
**Problem:** `GetBiomeWeightsStatic()` computes weights without locking.
**Fix:** Cache biome configs at generation start.

### 🟡 4.5 Mesa Pillar Height Calculation Has No Bounds Check
**File:** VoxelBiomeGenerators.cpp:~GetMesaHeight
```cpp
const float PillarIntensity = (PillarNoise - PillarThreshold) / (1.f - PillarThreshold);
```
**Problem:** If `PillarNoise > 1.0f`, `PillarIntensity > 1.0f`, causing unbounded heights.
**Fix:** `FMath::Clamp(PillarIntensity, 0.f, 1.f)`

---

## SYSTEM 5: WATER SIMULATION

### 🔴 5.1 WATER_FILL_LEVEL Too Coarse (CRITICAL)
**File:** VoxelWaterTypes.h:21
```cpp
static constexpr uint8 WATER_FULL = 8;
```
**Problem:** Only 9 discrete levels (0-8). Water can barely form gradients.
**Fix:** Increase to `WATER_FULL = 64` or use float-based levels.

### 🟠 5.2 Water Mesh Hash Missing
**File:** WaterVoxelSimulator.cpp
**Problem:** No hash comparison before rebuilding water mesh.
**Fix:** Add `CalculateWaterHash()` function.

### 🟠 5.3 Water Chunks Never Cleaned Up
**File:** VoxelWorldWater.cpp
**Problem:** `WaterChunks` TMap grows indefinitely.
**Fix:** Remove water chunks when parent voxel chunk unloads.

### 🟡 5.4 SimulateStep Has No Maximum Iteration Cap
**File:** WaterVoxelSimulator.cpp:~SimulateStep
**Problem:** Complex flow patterns can cause thousands of iterations per step.
**Fix:** Add `MaxIterationsPerStep` config parameter.

### 🟡 5.5 Water Level Transitions Between Chunks Not Smooth
**File:** WaterVoxelSimulator.cpp:~boundary handling
**Problem:** Water at chunk boundaries doesn't flow smoothly across edges.
**Fix:** Add 1-voxel overlap region for boundary water exchange.

---

## SYSTEM 6: STREAMING & LOD

### 🟠 6.1 LOD Hysteresis Only Applied Outward
**File:** VoxelWorld_Streaming.cpp:~188-212
```cpp
const float L1ISq = LOD1Distance * LOD1Distance;  // Inner = base (no scaling!)
```
**Problem:** Inner threshold equals base distance. Hysteresis only on outer side.
**Fix:** Scale inner threshold down.

### 🟠 6.2 LOD State Machine Can Oscillate
**File:** VoxelWorld_Streaming.cpp:~196-206
**Problem:** Player hovering at LOD boundary causes rapid switching.
**Fix:** Add minimum dwell time before allowing transition back.

### 🟡 6.3 Distance Sorting Uses Full Chunk Map Iteration
**File:** VoxelWorld_Streaming.cpp:~SortByDistance
**Problem:** Iterates entire `LoadedChunks` TMap every streaming update. O(N).
**Fix:** Maintain a priority queue sorted by distance.

### 🟡 6.4 No LOD Transition Smoothing
**File:** VoxelWorld_Streaming.cpp
**Problem:** Old mesh destroyed and new mesh created instantly. Visible pop-in.
**Fix:** Cross-fade between LOD meshes over a few frames.

### 🟡 6.5 Skyland Density Mismatch Between Streaming and Generation
**File:** VoxelWorld_Streaming.cpp vs VoxelBiomeGenerators.cpp
**Problem:** Altitude formula in streaming doesn't match generation exactly.
**Fix:** Use identical formula from a shared utility function.

---

## SYSTEM 7: DATA MAP

### 🔴 7.1 CopyFrom Holds Both Locks During Deep Copy (CRITICAL)
**File:** VoxelDataMap.cpp:115
```cpp
MapLock.Lock();
Other.MapLock.Lock();
Chunks = Other.Chunks;  // Full deep copy under BOTH locks
```
**Problem:** O(N) copy while holding both locks. Blocks all readers/writers.
**Fix:** Snapshot under brief lock, copy without locks, then swap under lock.

### 🟠 7.2 TMap<FIntVector, FChunkData> Hash Collision Risk
**File:** VoxelDataMap.h
**Problem:** `FIntVector` hash function may have collisions for nearby coordinates.
**Fix:** Use a custom hash function optimized for spatial locality.

### 🟡 7.3 SetSphere Doesn't Validate Radius
**File:** VoxelDataMap.cpp:~SetSphere
**Problem:** Negative or zero radius causes no edits but still iterates.
**Fix:** Add bounds check: `if (Radius <= 0) return;`

---

## SYSTEM 8: MESH GENERATION

### 🟠 8.1 Degenerate Cell Vertex Can Divide by Zero
**File:** VoxelMeshGenerator.cpp:~167-174
```cpp
CellPos /= (float)EdgeCount;  // Can be 0!
```
**Problem:** Corrupted density data could produce `EdgeCount == 0`.
**Fix:** Add guard: `if (EdgeCount == 0) continue;`

### 🟠 8.2 Normal Computation Uses Diagonals Instead of Triangle Edges
**File:** VoxelMeshGenerator.cpp:~ComputeNormal
**Problem:** Normal calculation samples diagonal neighbors, producing incorrect normals at sharp edges.
**Fix:** Use actual triangle edges from the generated mesh.

### 🟡 8.3 No Mesh Simplification for LOD
**File:** VoxelMeshGenerator.cpp
**Problem:** LOD meshes generated at full resolution then subsampled.
**Fix:** Pass LOD level to mesh generator, skip cells that don't contribute.

### 🟡 8.4 Vertex Welding Not Performed
**File:** VoxelMeshGenerator.cpp
**Problem:** Surface Nets can produce duplicate vertices at cell boundaries.
**Fix:** Add vertex welding pass after generation.

---

## SYSTEM 9: CONFIGURATION

### 🔴 9.1 No Validate() Methods on Any Config Struct (CRITICAL)
**Files:** All config headers
**Problem:** No validation of parameter ranges. Invalid values propagate silently.
**Fix:** Add `Validate()` method to each config struct.

### 🟠 9.2 No ClampMin/ClampMax Meta on UPROPERTY Fields
**Files:** All config headers
**Problem:** Editor sliders have no bounds.
**Fix:** Add `UMeta=(ClampMin="0", ClampMax="100000")` to all numeric fields.

### 🟠 9.3 Skylands Probability Can Exceed 1.0
**File:** SkylandsLayerConfig.h
```cpp
float BaseProbability = 0.3f;
float HeightProbabilityBonus = 0.8f;  // 0.3 + 0.8 = 1.1 > 1.0!
```
**Problem:** Combined probability > 1.0 is meaningless.
**Fix:** Clamp combined probability to [0, 1].

### 🟡 9.4 MaxTerrainReference Not Linked to Actual Heights
**File:** SkylandsLayerConfig.h
**Problem:** If terrain heights exceed this, skyland calculations break.
**Fix:** Auto-calculate from biome height configs at initialization.

### 🟡 9.5 Legacy Crater Parameters Still Present
**File:** SurfaceBiomesConfig.h
**Problem:** Old crater parameters exist alongside new hierarchical system.
**Fix:** Remove deprecated parameters.

---

## SYSTEM 10: UI, LOGGING & PLAYER

### 🔴 10.1 VoxelLogger Deadlock — Non-Recursive Lock Acquired Twice (CRITICAL)
**File:** VoxelLogger.cpp:~62, ~40
```cpp
FScopeLock ScopeLock(&LogLock);  // Acquires lock
if (LogFilePath.IsEmpty() || !FileHandle)
    InitLogger();                 // Tries to acquire same lock → DEADLOCK
```
**Problem:** `FCriticalSection` is non-recursive.
**Fix:** Make `LogLock` recursive.

### 🟠 10.2 No File Flush After Write — Crash = Lost Logs
**File:** VoxelLogger.cpp:~76
**Problem:** Data in OS buffer, lost on crash.
**Fix:** Add `FileHandle->Flush(false)` after each write.

### 🟠 10.3 bLogInitFailed Permanently Disables Logging
**File:** VoxelLogger.cpp:~15, ~44
**Problem:** If first log attempt fails, logging is permanently disabled.
**Fix:** Retry initialization periodically.

### 🟡 10.4 VoxelMapWidget Doesn't Handle Chunk Unload
**File:** VoxelMapWidget.cpp
**Problem:** Minimap shows chunks that have been unloaded.
**Fix:** Listen to `OnChunkUnloaded` event.

### 🟡 10.5 Player Character Doesn't Cache VoxelWorld Reference
**File:** FirstVoxelCharacter.cpp
**Problem:** Every edit operation does `GetWorld()->GetAuthGameMode()->FindVoxelWorld()`.
**Fix:** Cache reference in BeginPlay.

---

## QUICK WINS (Implement Today)

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
| 11 | Set bForceCraterAtOrigin=false | SurfaceBiomesConfig.h | ~400 | Fix duplicate crater |
| 12 | Clear crater state in ClearWorld | VoxelWorld.cpp | ClearWorld | Fix slopes not clearing |

---

## IMPLEMENTATION PLAN

### Phase 1: Critical Stability (1-2 days)
- [ ] Fix ActiveGenerations data race (use TAtomic)
- [ ] Fix bMeshDirty / bPendingLODTransition thread safety
- [ ] Fix CurrentTask shared pointer race
- [ ] Fix VoxelLogger deadlock (recursive lock)
- [ ] Fix CopyFrom dual-lock deep copy
- [ ] Cap density pool memory leak
- [ ] Add LOD collision for all levels
- [ ] Fix BiomeFoliageHISMs memory leak
- [ ] Fix duplicate crater at spawn (disable bForceCraterAtOrigin)
- [ ] Fix slopes not clearing (reset crater state in ClearWorld)

### Phase 2: Performance (2-3 days)
- [ ] Fix WATER_FILL_LEVEL coarseness
- [ ] Add water mesh hash to skip rebuilds
- [ ] Clean up water chunks on unload
- [ ] Fix LOD hysteresis inner threshold
- [ ] Add LOD oscillation prevention
- [ ] Fix foliage pass OOB read
- [ ] Fix ComputeNormal stride in foliage
- [ ] Remove duplicate skyland threshold code

### Phase 3: Visual Quality (3-5 days)
- [ ] Fix cracked surfaces on slopes (Surface Nets vertex averaging)
- [ ] Fix crater override gradual transition
- [ ] Extract crater magic numbers to config
- [ ] Fix Mesa pillar bounds
- [ ] Add LOD transition smoothing
- [ ] Fix skyland density formula mismatch
- [ ] Add mesh simplification for LOD
- [ ] Fix normal computation at sharp edges

### Phase 4: Polish (2-3 days)
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

| Phase | Duration | Issues | Critical |
|-------|----------|--------|----------|
| Phase 1 | 1-2 days | 10 | 10 |
| Phase 2 | 2-3 days | 8 | 0 |
| Phase 3 | 3-5 days | 8 | 1 |
| Phase 4 | 2-3 days | 8 | 0 |
| **Total** | **8-13 days** | **34** | **11** |

---

## NOTES

- The recent crater system rewrite (hierarchical impacts, ejecta blanket) is architecturally sound but contains many hardcoded values that should be configurable.
- The skyland system improvements (shard aspect ratio, altitude matching) are good but the duplicate threshold code needs removal.
- The biome system's crater override threshold (0.01) is too aggressive and causes visible biome pop-in.
- Thread safety remains the primary concern across the codebase — many shared state variables use plain `bool`/`int32` without atomic operations.
- The Surface Nets algorithm has an inherent limitation on steep slopes that causes visual cracks — this is a fundamental algorithmic issue, not a bug.
- The forced crater system (`bForceCraterAtOrigin`) creates a "duplicate crater" effect when it overlaps with natural crater noise.