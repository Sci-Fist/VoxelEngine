# Changelog

## [Unreleased]

### Added — Voxel Water Simulation
- **FVoxelWaterSimulator** (`Components/VoxelWaterSimulator.h/.cpp`): New cellular-automata
  water engine. Each voxel stores a fill level 0–8. Simulation runs at a fixed interval
  (default 5 steps/sec) entirely on the GameThread after async mesh work completes.
  - **Gravity**: water falls into the cell directly below if it is air.
  - **Lateral spread**: water equalises with horizontal neighbours when below is blocked.
  - **Ledge flow**: water detects downhill diagonal paths and flows over edges.
  - **Permanent sources** (`WATER_SOURCE = 255`): marked cells refill to full every tick and
    never drain — used for springs, pool beds, and skyland waterfalls.
- **FVoxelWaterData** (part of `VoxelWaterSimulator.h`): per-chunk struct owning the cell
  array and solid-cell bit-field. Lives on `AVoxelChunk` so the simulator holds only a
  raw pointer; lifetime is guaranteed by the chunk pool.
- **PlaceWaterSources()** in `FVoxelGeneratorTask`: runs after mesh generation on the
  background thread. Scans the density field for:
  - Surface depressions (air on solid, ≥2 solid cardinal neighbours) — respects per-biome
    `LakeSpawnProbability` and enclosure factor (4-wall = 1.5×, 3-wall = 1.1×).
  - Skyland flat surfaces (air on 2+ voxels of solid, above `MinAltitudeAboveTerrain`) —
    uses `SkylandsWater.LakeSpawnProbability × 0.08` so islands don't flood.
  - All placement uses a deterministic hash (no `FRand` on background threads).
  - Cells at or below `SeaLevel` are skipped; the ocean handles those.
- **AVoxelChunk::OnChunkWaterReady**: new `TFunction` callback fired from `ApplyMesh()`
  with the detected source coords. `AVoxelWorld::InitChunkWater()` binds it and forwards
  sources to `FVoxelWaterSimulator::SetSource()`.
- **AVoxelChunk::RebuildWaterMesh() / BuildWaterMeshInternal()**: builds a separate
  translucent `UProceduralMeshComponent` (`WaterMesh`) for water surfaces. Top faces only;
  fill level drives the vertex height so partially-filled cells taper correctly.
  Material is assigned from `GenerationConfig.Water.OceanMaterial`.
- **AVoxelWorld::InitChunkWater()**: registers chunk with simulator and binds
  `OnChunkWaterReady`. Called from `SpawnChunk()` before `GenerateAsync()`.
- **AVoxelWorld::TickWater()**: advances sim one step per `WaterSimInterval` (0.2 s),
  collects dirty chunk coords, calls `RebuildWaterMesh()` on each. Only runs in gameplay
  (editor skips simulation to avoid viewport noise).
- **Invisible-mesh pop fix** (`AVoxelChunk`): `ProceduralMesh` and `WaterMesh` are now
  created with `SetVisibility(false)`. `ProceduralMesh` is revealed only at the very end
  of `ApplyMesh()` once all sections and materials are fully uploaded. `ClearMesh()` hides
  both again so returning a chunk to the pool can't flash an empty hull.
- **Solid-cell map**: `ApplyMesh()` now populates `WaterData.SolidCells` from the task's
  raw density array so the simulator knows the terrain topology without re-running noise.

### Fixed — Density Field Water Bug (chunk-filling / mesh overlap)
- **Root cause**: A "LAYER 5: WATER" block had been injected directly into `GetDensityFull()`
  with two critical bugs:
  1. `WaterD = 1.5f` was applied to every voxel at `Z <= SeaLevel (0)`, then composed
     via `FMath::Max(Final, WaterD)`.  This re-solidified any voxel that cave or crystal
     carving had just hollowed out, filling entire underground chunks back to 100% solid
     and producing the mass of clipping procedural meshes.
  2. The skyland depression check ran a 3×3 XY neighbour scan calling
     `GetBiomeWeightsStatic` + `GetSurfaceHeightStatic` **8 times per skyland voxel** —
     an O(n³ × 8 × 2) noise evaluation that compounded the generation stall.
- **Fix**: Removed LAYER 5 entirely from `GetDensityFull()`.  Water belongs to the
  *render* and *simulation* layers, not the density field:
  - Ocean surface → `UVoxelWaterComponent` flat plane (already working).
  - In-world pools and flow → `FVoxelWaterSimulator` (cellular automata, post-generation).
  - The density field now returns `FMath::Clamp(FMath::Max(SkyD, SurfD), -2, 2)` with
    a comment explaining why water must never be added here.

### Design intent
Water should feel physically present: pools form in natural depressions, rain-filled
crater lakes fill to the rim, skyland waterfalls pour over ledge edges and vanish when
the chunk below is unloaded, and desert oases sit rare and still. Sources are permanent
springs that guarantee steady replenishment even as flowing water spreads outward and
eventually drains off open edges.
### Fixed
- **Compilation error**: Fixed missing `TestSmoothLODTransitions` function declaration in VoxelWorld.h
- **Compilation error**: Fixed private access to `TransitionToLOD` function by moving it to public section in VoxelChunk.h  
- **Compilation error**: Fixed private access to `MeshState` enum by moving it to public section in VoxelChunk.h
- **Compilation error**: Fixed incorrect range-based for loop syntax in VoxelWorld.cpp
- Prevented editor shutdown freezes by cancelling voxel chunk generation, avoiding post-PIE rebuilds during engine exit, and skipping voxel tick work after shutdown begins.
- Added shutdown-safe guards to the voxel map widget to prevent async refresh work during teardown.
- Added shutdown checks around voxel world startup/timer callbacks to avoid scheduling work while exiting.
- Fixed build errors by updating engine-exit checks and ensuring box component headers are included.
- Corrected right-stick look inversion for gamepad input.
- **Fixed unresolved external symbol error for AFirstVoxelCharacter::ToggleAutoWalk()** by implementing the missing method in FirstVoxelCharacter.cpp. Added auto-walk functionality that moves the character forward automatically when enabled, with automatic cancellation when manual movement inputs are detected (WASD or gamepad left stick).
- **Resolved build errors** including template-related compilation errors (UEStaticAssertCompleteType_Private, TIsContiguousContainer) and file system corruption issues that were preventing successful project compilation.
- **Fixed water mesh visibility issue** where procedural water meshes were invisible until the player entered the chunk. Changed water mesh initialization to be visible by default in AVoxelChunk constructor, resolving the "problematic meshes invisible until i enter them" issue.

### Added
- Added 4 selectable terrain tools (Dig/Build/Smooth/Flatten) with keyboard 1-4 and D-pad bindings.

### Updated
- Enforced crater biome spawning with voxel-grid snapping and safe spawn height offsets to avoid unsafe placements.
- Tuned Skylands generation with sharper low-terrain falloff, updated altitude/size scaling, and new low-terrain altitude boosts for island shards.
- Tightened skylands probability defaults and reduced cave/cavern carve strength for less over-carving.


### Fixed

- Prevented the editor "Generate World" action from freezing by draining the generation queue with a non-blocking ticker instead of a tight loop.
- **Player stuck in falling animation when landing on voxel terrain**: Fixed unreliable floor detection by implementing custom fallback landing detection, overriding Landed() to force immediate floor validation, and improving movement component settings.

- **Voxel terrain edge artifacts causing non-walkable surfaces**: Added FlattenMeshTops() post-processing that flattens top-facing vertices (normal.Z > 0.9) to the highest point in their local neighborhood, eliminating edge variations that produced non-walkable floor normals.

- **Flight mode transition leaving character in falling state**: Fixed ToggleFly() to use UpdateFloorFromAdjustment() after switching to MOVE_Walking, ensuring proper animation state transition.



### Improved — Voxel Terrain Walkability & Climbing
- **Character movement settings**: Increased MaxStepHeight to 100 cm (1 voxel) to allow climbing height differences, extended FloorSweepTestDistance to 250 cm for better ground detection on uneven terrain, enabled CCD to prevent tunneling, and adjusted MinFloorSweepTestDistance for improved edge detection.
- **Terrain generation**: Added post-processing to flatten top-facing surfaces, ensuring truly horizontal walking surfaces with consistent upward normals. This eliminates subtle edge variations that caused the capsule floor sweep to fail.
- **Landing detection**: Implemented CustomFloorCheck() using sphere sweep (like VoxelWorld initial spawn) as a fallback when physics-based landing is delayed. Called every Tick while falling to ensure prompt ground detection.
- **Landed() override**: Forces immediate floor validation via UpdateFloorFromAdjustment() whenever physics detects a landing, ensuring animation state updates without delay.

- Prevented skylands from blanketing low terrain by adding a low-terrain early-out and scaling skyland probability by terrain falloff.