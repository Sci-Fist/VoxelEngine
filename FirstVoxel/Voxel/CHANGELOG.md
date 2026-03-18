# FirstVoxel — Changelog

---

## [Unreleased] — 2026-03-18

### Skylands & Shards
- **Shard shape rewrite** (`VoxelBiomeGenerators.cpp`): sky-shards now look like floating boulders instead of vertical pillar slabs.
  - `EffThickness`: shard value raised from `0.10` → `0.65` so `HalfThick ≈ 65%` of radius — a near-spherical boulder aspect instead of a 1-voxel-thin pancake.
  - `MaxThicknessRatio`: shards use `0.75` (sphere-friendly); islands keep `SC.MaxThicknessRatio` (flat disc). Was a single value for both types.
  - **Falloff shape** (`GetSkylandDensityFromCache`): shards now use a symmetric spherical falloff (`SmoothStep(1 - |tCenter|^0.6)`) with no flat zone. Islands keep the old flat-top (35% plateau + smooth taper). Blended by `Cache.ShardT`.
  - **Z noise frequency** for shards: `0.50×` Freq (was always `0.05×`). Higher Z variation gives irregular lumpy boulder surfaces.
  - **BreakUp strength** for shards: `0.10` (was `0.50`). Excess breakup on thin shapes stripped material off all sides leaving spike tips.
  - **Size by altitude**: shards now scale with how far they float above local terrain (`AltSizeScale = clamp(AltGap/MinAlt, 0.4, 3.0)`), so high-flying shards are dramatically large and low shards are pebbles.
  - **`Cache.ShardT`** added to `FSkylandColumnCache` header to drive all of the above blends.
- **Clearance buffers removed** (`GetSkylandColumnCache`): both 200 cm clearance checks commented out. They were double-offsetting altitude (stacking on top of `MinAltitudeAboveTerrain`) and clamping `HalfThick` too aggressively, causing shards to revert to thin slabs and islands to clip into high terrain.

### Crater Pillar Fix
- **`GetCraterHeight` fully rewritten** (`VoxelBiomeGenerators.cpp`): eliminated the hard `if/else` zone switches at `NormDepth > 0.45 / 0.30 / 0.15` that produced C0 discontinuities in the height field. Surface Nets generates a vertical column at every such kink — the tall thin pillars visible at spawn.
  - New formula: bell-curve rim (`SmoothStep` quadratic centred at `NormDepth=0.25`) + `SmoothStep` floor descent. Height field is now C1-continuous everywhere.
  - `Depth * 8.0f` multiplier removed — was turning `Depth=-4000` into a 320 m deep crater. Now `Depth` is used 1:1.
- **`FCraterBiomeConfig` defaults tightened** (`SurfaceBiomesConfig.h`):
  - `Depth`: `-4000` → `-1200` (12 m with 1:1 multiplier)
  - `RimHeight`: `6000` → `1800`
  - `RimNoiseAmplitude`: `4000` → `500` (was the direct cause of 40 m rim spikes)
  - `ShapeDistortion`: `0.5` → `0.20`; `BorderIrregularity`: `0.8` → `0.30`
  - `BuildingNoiseAmplitude`: `300` → `200`

### Mesh Winding Fix — checkerboard on slopes
- **Deterministic quad winding** (`VoxelMeshGenerator.cpp` `EmitQuad`): replaced runtime cross-product winding detection with a purely deterministic rule based on `bD0Solid` and `Axis`.
  - **Root cause**: the canonical quad vertex order `(i0, i1, i2, i3)` produces `CrossProduct(v1-v0, v2-v0)` pointing in `-Axis` in the ideal grid case. On curved terrain Surface Nets vertices deviate from grid positions enough to flip this cross-product, randomly emitting some quads back-face-forward — the checkerboard pattern visible on steep slopes.
  - **Fix**: `bD0Solid=true` → always emit `(v2,v1,v0)` (reversed); `bD0Solid=false` → always emit `(v0,v1,v2)` (canonical). No vertex positions involved.

### Performance
- **O(N) dirty-chunk scan eliminated** (`VoxelWorld.cpp`): replaced full `LoadedChunks` loop every frame with a `DirtyRebuildQueue` (`TArray<FIntVector>`). Only populated via `MarkChunkDirty()`. Normal play costs zero per frame.
- **`MarkChunkDirty(Coord)`** added to `AVoxelWorld` (`VoxelWorld.h/.cpp`) as the correct API for dirtying a chunk from player edits.
- **Double `StreamingTimer` increment fixed** (`VoxelWorld_Streaming.cpp`): `UpdateChunkStreaming` was incrementing `StreamingTimer` itself in addition to the increment in `Tick`, firing streaming at ~2× the intended rate.
- **Spawn wait radius**: `RadiusXY=8` (867 chunks, 30+ s load screen) → `RadiusXY=1` (27 chunks, near-instant).
- **Queue compaction threshold**: `50` → `256` — reduces O(N) `RemoveAt` frequency from every ~6 ticks to every ~32 ticks.
- **LOD without `sqrt`** (`VoxelWorld_Streaming.cpp`): pre-squared `L1ISq`, `L2ISq` thresholds; compare `DistSq` directly. Eliminates ~500 `sqrt()` calls per streaming update.
- **Cached `SkyAltWorld`** (`VoxelWorld.h/.cpp + VoxelWorld_Streaming.cpp`): biome noise for skyland altitude now only recomputed when player moves > 1000 cm. Was running full `GetWeightsAndSurfaceHeightStatic` every 0.25 s regardless.
- **`EmptyChunks` set pruned** on chunk removal (`VoxelWorld_Streaming.cpp`): was growing unboundedly across long sessions.
- **`FlattenMeshTops` O(V²) → O(V)** (`VoxelMeshGenerator.cpp`): replaced brute-force neighbour search with a 2D spatial grid (bucket map). Reduces 4 M comparisons per 2000-vertex chunk to ~18 lookups per vertex.
- **Foliage surface height cached** (`VoxelGeneratorTask.h/.cpp`): `CalculateFoliage` now reads `ColumnSurfaceH[]` built during the density pass instead of re-calling `GetSurfaceHeightStatic` per column — eliminates a full biome noise evaluation per column in the foliage path.
- **Spawn height raised**: `SafeSpawnHeightOffset` default `3000` → `8000` cm (80 m) so the player always drops from above terrain on steep peaks and crater rims.

### Documentation
- **`ARCHITECTURE.md`** updated: generation pipeline diagram expanded to show `DirtyRebuildQueue`, `UpdateChunkStreaming` sub-steps, `FlattenMeshTops`, and `UploadSection` for both flat and slope meshes. New sections: crater C1 formula, skyland/shard property table, `MarkChunkDirty` how-to. LOD section updated to mention `DistSq` approach and `CachedSkyAltWorld`. Threading section updated with dirty-queue note. Date updated to 2026-03-18.
- **`VoxelMeshGenerator.h`** header updated with winding-fix note.
- **`VoxelBiomeGenerators.h`** header updated with shard/island shape system documentation.

---

## [Unreleased] — 2026-03-17 (continued)

### Pause Menu (`UI/VoxelPauseMenu.h/.cpp`)
- **New** `UVoxelPauseMenu` — full-screen canvas-drawn pause menu. No UMG required.
  - Main panel: Resume / Save World / Load World / Return to Title
  - Save Slot panel: 5 named slots with live `[SAVED]` badge when file exists
  - Load Slot panel: empty slots are dimmed and non-selectable
  - Confirm panel: "Are you sure?" before returning to title
  - Full keyboard navigation (↑↓ Enter Esc) and gamepad (DPad A B Start)
- **Critical fix** — did NOT use `SetGamePaused(true)`. UE's engine pause freezes
  the input system making `WasInputKeyJustPressed` return false for everything.
  Instead, `Open()` calls `CMC->DisableMovement()` + `FInputModeUIOnly` to freeze
  the pawn without stopping the input tick. `Close()` restores `MOVE_Walking`.
- `FKey` forward-declaration replaced with full `#include "InputCoreTypes.h"`.
- `JustPressed()` signature changed to `const FKey` to match UE convention.
- Added `#include "GameFramework/Character.h"` and `CharacterMovementComponent.h`
  to VoxelPauseMenu.cpp for the movement freeze/restore code.

### HUD (`FirstVoxelHUD.h/.cpp`)
- Added `BeginPlay()` — creates `UVoxelPauseMenu` via `NewObject` and calls `Init`.
- Added `TogglePause()` / `IsPaused()` — `BlueprintCallable` + `BlueprintPure`.
- `DrawHUD()` calls `PauseMenu->Draw()` first and early-returns when menu is open.
- Title screen upgraded:
  - Gold title drawn at 2× scale, properly centred.
  - Live save-slot detection: `[L] Load World (Slot 1)` shown when a save exists,
    greyed-out `(no saves)` when none exist.
  - Gamepad A = Generate New World, Gamepad X = Load World.
  - Hint line for `[P] or [Start]  Pause / Settings`.
- HUD control overlay corrected: `[Y]` = Map, `[Start]` = Pause (was `[Y]/Start` = Map).

### Character (`FirstVoxelCharacter.h/.cpp`)
- Added `PauseAction` `UInputAction` property (auto-loads `IA_Pause` asset).
- Added `TogglePauseMenu()` — casts to `AFirstVoxelHUD` and calls `TogglePause()`.
- **P key** and **Gamepad Start** (`Gamepad_Special_Right`) bound to `TogglePauseMenu`.
  Start was previously bound to ToggleMap; Map stays on Y button only.
- `Tick()` early-returns when `HUD->IsPaused()` is true, preventing all raw
  key-polling movement / look / dig from firing while the menu is open.

### Save System
- 5 named save slots ("Slot 1" through "Slot 5") defined in `VoxelPauseMenu.h`
  as `DefaultSlotNames[]` — shared by pause menu and title screen.
- Save files: `Saved/VoxelSaves/<WorldActorName>_<SlotName>.sav`
  Format: `[Version float] [Seed int32] [FVoxelDataMap binary]`
- Title screen quick-loads the first occupied slot on `[L]` / Gamepad X.
- Pause menu Save Slot panel saves current terrain edits + seed per slot.
- Pause menu Load Slot panel clears, loads, and calls `GenerateWorldDeferred()`
  then auto-resumes.
- Return to Title → Confirm → `bShowTitleScreen = true` on the HUD.

### Documentation
- Added `Voxel/ARCHITECTURE.md` — full 10-section reference covering directory
  layout, module map, generation pipeline, threading model, chunk lifecycle,
  biome system, water system, LOD system, editor integration, and how-to guide.
- Rewrote file-level header comments for all 20 voxel headers:
  - `Config/VoxelGenerationConfig.h` — file layout, layer diagrams, tweak guide,
    performance reference, seed math
  - `Config/SurfaceBiomesConfig.h` — biome distribution table, new-biome guide
  - `Config/SkylandsLayerConfig.h` — cellular grid overview, altitude formula,
    island vs shard design rules
  - `Config/CaveLayerConfig.h` — worm tunnel + crystal cavern algorithm
  - `Biomes/VoxelBiome.h` — weight map design rationale, extension steps
  - `Biomes/VoxelBiomeManager.h` — responsibility boundary, call frequency
  - `Biomes/VoxelBiomeGenerators.h` — height functions, skyland column cache,
    crystal cavern delta, FBM parameters
  - `Generation/IVoxelDensityProvider.h` — calling contract, density convention,
    performance notes
  - `Generation/VoxelDensityGenerator.h` — 4-layer composition, optimisations,
    thread safety
  - `Generation/VoxelGeneratorTask.h` — pipeline passes, foliage slot layout,
    cancellation protocol, thread safety
  - `Generation/VoxelMeshGenerator.h` — Surface Nets algorithm, winding order,
    vertex colour encoding
  - `Core/VoxelChunk.h` — responsibilities, state machine, callback protocol,
    generation ID guard, foliage component pool
  - `Core/VoxelChunkPool.h` — lifecycle, stale entry handling, thread safety
  - `Core/VoxelDataMap.h` — data layout, override application, thread safety,
    serialization format
  - `Core/World/VoxelWorld.h` — implementation files map, tick responsibilities,
    ActiveGenerations counter, config merge, editor spawn simulation
  - `Water/VoxelWaterTypes.h` — fill level encoding, FVoxelWaterData layout
  - `Water/VoxelWaterSimulator.h` — simulation rules, registration, Step() return,
    thread safety
  - `VoxelLogger.h` — log file location, thread safety, usage, log categories

### Editor UX
- **Removed `RandomizeSeed` button** from the Details panel.
  Seed randomization now happens automatically inside `GenerateWorld()`.
- **`Generate World` button** now:
  1. Picks a new random seed
  2. Clears the world
  3. Runs `GenerateWorldDeferred` (includes editor spawn simulation)
  4. Prints the active seed on screen for 8 seconds (cyan, top-left)
- Added `ToolTip` meta to `GenerateWorld`, `ClearWorld`, and `RebuildWorld`
  buttons so their purpose is clear in the Details panel.
- `RebuildWorld` clarified as "regenerate with CURRENT seed" — useful for
  testing config changes without changing the world layout.
- `RandomizeSeed()` made private; no longer a `UFUNCTION(CallInEditor)`.

---

## [0.9.0] — 2026-03-10  *(bug fix sprint)*

### Bug Fixes
- **`VoxelMeshGenerator.cpp` — compile error** (`MakeUV` scope bug): a stray
  `};` was closing `GenerateMesh()` before `EmitTriangle` and `EmitQuad` were
  defined, causing those lambdas to have dangling captures. Fixed by removing
  the extraneous brace.
- **`VoxelMeshGenerator.cpp` — double-sided quads (50% GPU saving)**: `EmitQuad`
  previously emitted 4 triangles (both windings) regardless of normal direction.
  Now emits 2 triangles with correct winding derived from `bD0Solid`, halving
  triangle count and GPU cost for all terrain meshes.
- **`VoxelChunk.cpp` — `ActiveGenerations` counter stuck**: `CancelGeneration()`
  and `ApplyMesh()` now use `MoveTemp + null-before-call` on `OnGenerationComplete`
  to prevent double-fire and re-entrant counter corruption.
- **`VoxelChunk.cpp` — LOD transitions never ticked**: `PrimaryActorTick.bCanEverTick`
  was not set in the constructor. Added Tick override that calls `UpdateMeshState()`
  while `MeshState == Transitioning`.
- **`VoxelBiomeManager.cpp` — hard-coded origin crater**: The `FORCE CRATER BIOME
  AT ORIGIN (0,0)` block corrupted the biome weight map at world (0,0) for any
  world whose crater spawn landed elsewhere (the default). Removed entirely —
  `FindCraterSpawnLocation()` already handles this correctly.
- **`VoxelGeneratorTask.cpp` — `PostProcessDensities` never called**: The density
  safety-clamp function was implemented but never invoked. Now called after
  `CountDensityStates()` in `BuildDensityField()`.
- **`VoxelGeneratorTask.cpp` — wrong log category**: `TrimFoliageToCap()` used
  `LogTemp` instead of `LogVoxelChunk`, hiding foliage cap warnings from the
  voxel log filter. Fixed.
- **`VoxelBiomeManager.h/.cpp` — dead non-`WithSeed` overloads**: `GetTemperature()`
  and `GetErosion()` (non-`WithSeed` variants) were declared but never called.
  Removed from header and implementation.
- **`VoxelWorldGeneration.cpp` — `ConfigureChunk` missing `SlopeThreshold`**:
  `Chunk->SlopeThreshold` was never assigned, so all chunks silently used the
  chunk's default value (0.7) ignoring the world actor's setting. Fixed.
- **`VoxelWorldGeneration.cpp` — dead `OnChunkGenerationComplete()`**: This method
  was never called (all callbacks use inline lambdas). Removed from header and
  implementation to avoid confusion.

### Performance
- **`VoxelWorld_Streaming.cpp` — eliminated per-tick O(N) `TSet` copy**: The old
  code copied the entire `GenerationQueue` into a `TSet` every 0.25 seconds.
  Replaced with a direct `LoadedChunks.Contains()` check (O(1)) per candidate
  chunk. Eliminates the allocation on every streaming tick.
- **`VoxelWorld_Streaming.cpp` — LOD hysteresis**: Chunks now require a 10% buffer
  beyond a LOD boundary before transitioning, preventing repeated `GenerateAsync()`
  calls when the player walks along a boundary line.
- **`VoxelDataMap.cpp` — lock held during sphere loop**: `SetSphere()` now builds
  a local batch without holding `MapLock`, acquiring it only once at the end to
  merge. Background `GetChunkData()` calls from generation threads are no longer
  blocked during terrain editing.
- **`VoxelWorldGeneration.cpp` — queue compaction threshold**: Reduced from 100
  to 50 entries to keep each compaction operation cheaper.

---

## [0.8.0] — 2026-02-15  *(generation system)*

### Added
- Surface Nets mesh generation (`VoxelMeshGenerator`) replacing Marching Cubes.
  Produces smoother terrain with fewer triangles and cleaner normals.
- Three-layer density composition: Surface + Skylands + Caves.
- Per-biome foliage system with HISM component pooling.
  `BiomeFoliageHISMs[]` grows on demand; `ClearMesh()` clears instances but
  keeps components alive for fast reuse on pool return.
- Voxel water cellular automaton (`FVoxelWaterSimulator`).
  Fills terrain depressions and skyland pools; lake probability is per-biome.
- `FVoxelDataMap` sparse player-edit store with binary serialization.
- Chunk pool (`FVoxelChunkPool`) — eliminates `SpawnActor/Destroy` overhead.
- LOD system (3 levels, step sizes 1/2/4) with async transitions.
- Crater spawn system: `FindCraterSpawnLocation()` searches for a high-weight
  crater before generation, centering the world on it.
- Editor spawn simulation: `GenerateWorldDeferred()` under `#if WITH_EDITOR`
  runs the crater search + spawn chunk prioritization in the viewport.
- `bRandomizeSeedOnStartup` property — if true, `BeginPlay` picks a new seed
  each PIE session.
- `UVoxelBiomeDataAsset` preset system for swapping world configs as data assets.

### Changed
- `GenerationQueue` uses a `QueueHead` cursor instead of O(N) `RemoveAt(0)`.
- Streaming tick throttled to `StreamingInterval = 0.25s` with early-out when
  player is stationary (avoids building the desired set every frame).
- Foliage slot schema built once in `FVoxelGeneratorTask` constructor rather
  than per-triangle inside `CalculateFoliage()`.
- Biome column weights cached in `ColumnWeights[]` during density pass and
  reused for foliage scatter — eliminates O(n²) repeated Perlin calls.
- `FSkylandColumnCache` built once per XY column and reused for all Z values,
  reducing the 9-cell neighbourhood query cost from O(n³) to O(n²).
- `GetSeedOffset()` LCG hash range extended from ±32 768 to ±131 071 cm (4×
  wider) so seeds differing only in low bits produce visually distinct worlds.

---

## [0.1.0] — 2025-10-01  *(initial prototype)*

### Added
- Basic AVoxelWorld actor with manual `GenerateWorld()` call.
- Single-layer density using `FVoxelBiomeManager` height field only.
- Naive Marching Cubes mesh generation.
- Flat global foliage (TreeMesh / GrassMesh legacy path).
- `UVoxelLogger` session log writer.
