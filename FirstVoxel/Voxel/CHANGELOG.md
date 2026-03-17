# FirstVoxel — Changelog

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
