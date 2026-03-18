# FirstVoxel — Architecture Reference

> Last updated: 2026-03-18  
> Engine: Unreal Engine 5 · Language: C++17

---

## Table of Contents

1. [Directory Layout](#1-directory-layout)
2. [Module Map](#2-module-map)
3. [Generation Pipeline](#3-generation-pipeline)
4. [Threading Model](#4-threading-model)
5. [Chunk Lifecycle](#5-chunk-lifecycle)
6. [Biome System](#6-biome-system)
7. [Water System](#7-water-system)
8. [LOD System](#8-lod-system)
9. [Editor Integration](#9-editor-integration)
10. [How To Guide](#10-how-to-guide)

---

## 1. Directory Layout

```
Source/FirstVoxel/
│
├── Voxel/                             ← all voxel-engine code lives here
│   │
│   ├── Config/                        ← pure-data config structs (no logic)
│   │   ├── VoxelGenerationConfig.h        ROOT config — includes the three below
│   │   ├── SurfaceBiomesConfig.h          per-biome height-field parameters
│   │   ├── SkylandsLayerConfig.h          floating island system
│   │   └── CaveLayerConfig.h              worm tunnels + crystal caverns
│   │
│   ├── Biomes/                        ← biome weights + shape functions
│   │   ├── VoxelBiome.h                   EVoxelBiome enum + FVoxelBiomeWeightMap
│   │   ├── VoxelBiomeDataAsset.h          UDataAsset preset wrapper
│   │   ├── VoxelBiomeManager.h/.cpp       2D weight sampling + surface height blend
│   │   └── VoxelBiomeGenerators.h/.cpp    per-biome height functions + skylands
│   │
│   ├── Generation/                    ← per-chunk async generation pipeline
│   │   ├── IVoxelDensityProvider.h        abstract density interface
│   │   ├── VoxelDensityGenerator.h/.cpp   3-layer density composer
│   │   ├── VoxelGeneratorTask.h/.cpp      bg task: density → mesh → foliage
│   │   └── VoxelMeshGenerator.h/.cpp      Surface Nets mesh builder
│   │
│   ├── Core/                          ← runtime chunk actors + data structures
│   │   ├── VoxelChunk.h/.cpp              AVoxelChunk actor
│   │   ├── VoxelChunkPool.h/.cpp          object pool for chunk reuse
│   │   ├── VoxelDataMap.h/.cpp            sparse player-edit store
│   │   └── World/                         AVoxelWorld split across 4 .cpp files
│   │       ├── VoxelWorld.h               class declaration + editor actions
│   │       ├── VoxelWorld.cpp             constructor, BeginPlay, Tick, helpers
│   │       ├── VoxelWorldGeneration.cpp   GenerateWorldDeferred, SpawnChunk, …
│   │       ├── VoxelWorldModification.cpp SetVoxelSphere, Save/Load, tests
│   │       ├── VoxelWorld_Streaming.cpp   UpdateChunkStreaming, LOD transitions
│   │       └── Water/
│   │           └── VoxelWorldWater.h/.cpp UVoxelWorldWaterComponent
│   │
│   ├── Water/                         ← voxel water simulation
│   │   ├── VoxelWaterTypes.h              constants (WATER_EMPTY, WATER_FULL, …)
│   │   ├── VoxelWaterSimulator.h          FVoxelWaterSimulator cellular flow
│   │   ├── VoxelWaterComponent.h          UVoxelWaterComponent (ocean mesh)
│   │   ├── WaterVoxelSimulator.cpp
│   │   └── WaterVoxelComponent.cpp
│   │
│   ├── Components/                    ← forwarding stubs (canonical in Water/)
│   │   ├── VoxelWaterComponent.h          → Voxel/Water/VoxelWaterComponent.h
│   │   └── VoxelWaterSimulator.h          → Voxel/Water/VoxelWaterSimulator.h
│   │
│   ├── VoxelLogger.h/.cpp             ← thread-safe session log writer
│   ├── ARCHITECTURE.md                ← this file
│   └── CHANGELOG.md                   ← version history
│
├── UI/
│   └── VoxelMapWidget.h/.cpp          ← debug overhead map
│
├── FirstVoxelCharacter.h/.cpp
├── FirstVoxelGameMode.h/.cpp
└── FirstVoxel.Build.cs
```

---

## 2. Module Map

```
┌──────────────────────────────────────────────────────────────────┐
│  AVoxelWorld  (Core/World/VoxelWorld.h)                          │
│  Owns: DensityGenerator, ChunkPool, DataMap, WaterSystemComponent│
│  Drives: SpawnChunk / DestroyChunk / streaming / LOD             │
└────────┬─────────────────────────────────────────────────────────┘
         │ creates / configures
         ▼
┌──────────────────────────────────────────────────────────────────┐
│  AVoxelChunk  (Core/VoxelChunk.h)                                │
│  Holds: ProceduralMeshComponent, WaterMesh, BiomeFoliageHISMs    │
│  Fires: FVoxelGeneratorTask on a background thread               │
│  Ticks: UpdateMeshState() (Only enabled during active transition)│
└────────┬─────────────────────────────────────────────────────────┘
         │ dispatches
         ▼
┌──────────────────────────────────────────────────────────────────┐
│  FVoxelGeneratorTask  (Generation/VoxelGeneratorTask.h)          │
│  Background thread only. Runs four sub-passes:                   │
│    1. BuildDensityField  ← FVoxelDensityGenerator (ParallelFor)  │
│    2. BuildMesh          ← FVoxelMeshGenerator (Surface Nets)    │
│    3. CalculateFoliage   ← per-biome HISM transform arrays       │
│    4. PlaceWaterSources  ← depression scan for pools / lakes     │
└────────┬─────────────────────────────────────────────────────────┘
         │ reads
         ▼
┌──────────────────────────────────────────────────────────────────┐
│  FVoxelDensityGenerator  (Generation/VoxelDensityGenerator.h)    │
│  Three-layer composition:                                        │
│    Layer 1 (Surface):   FVoxelBiomeManager height field          │
│    Layer 2 (Skylands):  FVoxelBiomeGenerators::GetSkylandDensity │
│    Layer 3 (Caves):     SampleCaveNoise + CrystalCavernDelta     │
└────────┬─────────────────────────────────────────────────────────┘
         │ uses
         ▼
┌──────────────────────────────────────────────────────────────────┐
│  Config layer  (Config/*.h)                                      │
│  Pure data structs. No UObject dependencies. Copy-safe.          │
│  Serialized via UVoxelBiomeDataAsset presets.                    │
└──────────────────────────────────────────────────────────────────┘
```

---

## 3. Generation Pipeline

One tick's path from queue to visible mesh:

```
AVoxelWorld::Tick()
  ├─ StreamingTimer threshold reached → UpdateChunkStreaming()
  │    ├─ Build Desired set  (ground volume + skylands volume)
  │    ├─ DestroyChunk() + EmptyChunks.Remove() for out-of-range chunks
  │    ├─ LOD update  (DistSq vs pre-squared thresholds — no sqrt)
  │    └─ Rebuild + re-sort GenerationQueue nearest-first
  │
  ├─ DrainGenerationQueue()  (up to 8 chunks/tick in game, 2 in editor)
  │    └─ SpawnChunk(Coord)
  │         ├─ ChunkPool.RetrieveOrCreateChunk()    reuse or allocate
  │         ├─ ConfigureChunk(Chunk)                inject config + materials
  │         └─ Chunk->GenerateAsync()
  │              └─ AsyncTask(BackgroundThread)
  │                   └─ FVoxelGeneratorTask::Execute()
  │                        ├─ BuildDensityField()   ParallelFor XY×Y columns
  │                        │    ├─ Column early-outs (bedrock / pure air)
  │                        │    └─ GetDensityFull() per voxel (O(n³) noise)
  │                        ├─ PostProcessDensities()  (hook for future passes)
  │                        ├─ BuildMesh()             Surface Nets
  │                        │    └─ FlattenMeshTops()  O(V) In-place grid flatten
  │                        ├─ CalculateFoliage()      column-cached scatter
  │                        └─ PlaceWaterSources()     depression scan
  │             ↓ (game thread callback via GenerationId guard)
  │         AVoxelChunk::ApplyMesh()
  │              ├─ ClearAllMeshSections()
  │              ├─ UploadSection(0, FlatMesh, FlatMat)
  │              ├─ UploadSection(1, SlopeMesh, SlopeMat)
  │              ├─ Populate BiomeFoliageHISMs
  │              ├─ Build FVoxelWaterData.SolidCells
  │              ├─ Fire OnChunkWaterReady callback
  │              └─ ProceduralMesh->SetVisibility(true)
  │
  └─ DirtyRebuildQueue drain  (O(dirty) not O(all loaded chunks))
       └─ GenerateAsync() for each dirty coord when slot available
```

### Density field indexing

Array size: `(EffectiveSize + 3)³` where `EffectiveSize = ChunkSize / StepSize`.

The `+3` padding means local voxel `(lx, ly, lz)` lives at padded index `(lx+1, ly+1, lz+1)`.
Adjacent chunks sample the same world function for border voxels, so seams are seamless without stitching.

---

## 4. Threading Model

| Thread | Responsibilities |
|--------|-----------------|
| **Game Thread** | AVoxelWorld Tick, SpawnChunk, ApplyMesh, all UObject/component API |
| **Background (AnyNormalTask)** | FVoxelGeneratorTask::Execute (density + mesh + foliage + water) |
| **Any thread** | FVoxelDataMap reads/writes (protected by FCriticalSection MapLock) |

**Core rule:** `FVoxelGeneratorTask` never touches UObjects. All outputs are plain C++ structs uploaded on the game thread inside `ApplyMesh()`.

**Generation ID guard:** `GenerateAsync()` increments `AVoxelChunk::GenerationId` (TAtomic). The background callback checks `TaskId == GenerationId` before calling `ApplyMesh`. Stale results from cancelled tasks are silently discarded.

**Callback safety:** `OnGenerationComplete` is moved (`MoveTemp`) before calling and nulled first to prevent double-fire if the callback triggers a re-entrant `GenerateAsync` via `bMeshDirty`.

**Dirty-chunk queue:** Player edits call `AVoxelWorld::MarkChunkDirty(Coord)` which appends to `DirtyRebuildQueue`. The Tick loop drains this queue (O(dirty)) instead of scanning all `LoadedChunks` (O(N)) every frame.

---

## 5. Chunk Lifecycle

```
         ┌───────────┐
         │   Pooled  │  hidden, no mesh, collision off
         └─────┬─────┘
               │ RetrieveOrCreateChunk()
               ▼
         ┌───────────┐
         │ Configured│  still hidden (SetActorHiddenInGame true)
         └─────┬─────┘
               │ GenerateAsync()
               ▼
         ┌───────────┐
         │Generating │  bGenerating=true  EChunkMeshState::Generating
         │(bg thread)│
         └─────┬─────┘
               │ ApplyMesh() on game thread
               ▼
         ┌───────────┐
         │   Ready   │  bMeshApplied=true  visible  collision on
         └─────┬─────┘
     ┌─────────┴─────────┐
player edit         out of range
     │                   │
     ▼                   ▼
bMeshDirty=true    DestroyChunk()
     │                   │
     ▼                   ▼
GenerateAsync()    ReturnChunk() → Pool
```

The `FVoxelChunkPool` prevents repeated `SpawnActor/Destroy` round-trips. `ReturnChunk` calls `CancelGeneration + ClearMesh + SetActorHiddenInGame`; `RetrieveOrCreateChunk` pops from pool or falls back to `World->SpawnActor`.

---

## 6. Biome System

### Weight calculation (per XY column — O(n²))

```
GetBiomeWeightsStatic(X, Y, Config)
  1. Temperature = PerlinNoise2D(X·TempFreq, Y·TempFreq)  → [0,1]
  2. Erosion     = PerlinNoise2D(X·EroFreq,  Y·EroFreq)   → [0,1]
  3. ForestW  = SmoothStep(flat)  × SmoothStep(cool-to-warm)
     DesertW  = SmoothStep(flat)  × SmoothStep(hot)
     PeaksW   = SmoothStep(rough) × SmoothStep(cool) × PeaksStrength
     CliffsW  = SmoothStep(rough) × SmoothStep(warm) × CliffsStrength
     MesaW    = SmoothStep(hot)   × SmoothStep(mid-erosion) × MesaStrength
     CratersW = SmoothStep(crater noise field) × 0.45
  4. Map.Normalize() → weights always sum to 1.0
```

### Surface height (per XY column — O(n²))

```
GetSurfaceHeightStatic(X, Y, Weights, Config)
  = Σ GetBiomeHeight(b, X, Y) × Weights[b]   for every biome with Weight > 0.01
```

Each biome height function uses FBM (fractional Brownian motion) with per-biome frequency, octave count, sharpness, and optional domain warp.

**Crater height** uses a C1-continuous bell-curve formula (no hard zone switches):
```
NormDepth ∈ [0,1]  (0 = outside crater, 1 = centre)
RimT      = bell curve centred at NormDepth=0.25  (smooth quadratic)
FloorT    = SmoothStep(0.40, 1.0, NormDepth)
Height    = Lerp(BasePlains, RimPeak, SmoothStep(0, 0.25, NormDepth))  when NormDepth ≤ 0.25
          = Lerp(RimPeak,   Floor,   SmoothStep(0.25, 1.0, NormDepth)) when NormDepth > 0.25
```
This guarantees a C1 height field with no kink discontinuities that would otherwise produce vertical pillar geometry at zone boundaries.

### Density composition (per voxel — O(n³))

```
GetDensityFull(WorldPos, Weights, SurfH, NeutralSurfH, Config)

  ── Layer 1: Surface ──────────────────────────────────────
  SurfD = (SurfH - Z) / SurfaceGradientScale       ← signed distance ramp
  SurfD += overhang noise  (if Performance.bEnableOverhangs)

  ── Layer 2: Caves (only inside solid terrain: SurfD > 0.05) ──
  SurfD -= CaveTunnel carving × fade
  SurfD += CrystalCavern delta

  ── Layer 3: Bedrock ──────────────────────────────────────
  if Z < BedrockDepth: SurfD = 2.0

  ── Layer 4: Skylands ─────────────────────────────────────
  SkyD = GetSkylandDensity(...)   [skipped for ground-level chunks]
  SkyD faded to -2 below SurfaceHeight + MinAltitudeAboveTerrain

  return max(SkyD, SurfD)         skylands override air above surface
```

### Skyland / shard shape system

Skylands use a two-type blend controlled by `CellShardT` (0 = shard/rock, 1 = island/platform):

| Property | Shard (CellShardT=0) | Island (CellShardT=1) |
|----------|---------------------|----------------------|
| **EffThickness** | 0.65 × IslandSize | ThicknessRatio × IslandSize |
| **MaxThicknessRatio** | 0.75 | MaxThicknessRatio config |
| **Falloff shape** | Spherical (no flat zone) | Flat-top 35% + smooth taper |
| **Z noise frequency** | 0.50× Freq | 0.05× Freq |
| **3D noise strength** | 0.55 | 0.25 |
| **BreakUp strength** | 0.10 | up to 2.80 × HeightNorm |
| **Size scaling** | by altitude gap above terrain | fixed by HeightSizeBonus |

`GetSkylandColumnCache()` runs once per XY column (9-cell grid neighbourhood), caches `SkyAlt`, `HalfThick`, `ShardT`, `Freq`, `Threshold`.  
`GetSkylandDensityFromCache()` runs per voxel using the cache — only evaluates shape noise + falloff.

---

## 7. Water System

Two independent subsystems coexist:

| Subsystem | Component | Description |
|-----------|-----------|-------------|
| **Static Ocean** | UVoxelWaterComponent | Flat plane at `SeaLevel`. Fast, no simulation. Driven by a StaticMeshComponent. |
| **Voxel Water** | FVoxelWaterSimulator | Cellular automaton fills depressions. Creates lakes, crater pools, skyland ponds. |

### Voxel water data per chunk

`FVoxelWaterData` (inside `AVoxelChunk`) stores:
- `SolidCells[]` — built from the density array in `ApplyMesh()`
- `Cells[]` — per-voxel fill level (`uint8`: 0=empty, 255=source, 1–254=flowing)

`UVoxelWorldWaterComponent` ticks the simulator at `Water.SimStepInterval` seconds, then calls `RebuildWaterMesh()` on each dirty chunk.

### Water source detection

`FVoxelGeneratorTask::PlaceWaterSources()` scans for air voxels sitting on solid ground with ≥2 solid cardinal horizontal neighbours (enclosure check). Probability is gated per biome via `FVoxelBiomeWaterConfig::LakeSpawnProbability`. Results are passed to `AVoxelWorld` via `AVoxelChunk::OnChunkWaterReady`.

---

## 8. LOD System

| LOD | StepSize | Effective voxel size | When active |
|-----|----------|----------------------|-------------|
| 0 | 1 | VoxelSize (100 cm) | < LOD1Distance |
| 1 | 2 | 200 cm | LOD1Distance – LOD2Distance |
| 2 | 4 | 400 cm | > LOD2Distance |

`StepSize = 1 << LOD`. Larger step → fewer density samples → simpler mesh.

**Hysteresis:** Distance comparisons use pre-squared thresholds (`L1ISq`, `L2ISq`) and `DistSq` directly — no `sqrt()` per chunk. A 10% outer band prevents flip-flopping at boundaries.

**Transition tick:** `AVoxelChunk::Tick()` is only enabled during active transitions and disables itself when complete (`MeshState == Ready`). `TransitionToLOD()` fires `GenerateAsync()` for the new LOD and stores the old mesh in `PreviousMesh` for optional blending.

**Skylands streaming** uses a separate volume centred on the estimated skyland altitude (`CachedSkyAltWorld`). The sky altitude is recomputed only when the player moves > `SkyAltSnapDist` (1000 cm) to avoid per-tick biome noise sampling.

---

## 9. Editor Integration

### Details panel button layout

**Voxel** (top category)

| Button | What it does |
|--------|--------------|
| **Generate World** | `RandomizeSeed → ClearWorld → GenerateWorldDeferred`. Prints active seed on screen (8 s). |
| **Clear World** | Destroys all chunks. Does not regenerate. |
| **Snap Player to Ground** | Teleports PlayerStart/pawn to `GetTerrainHeight(X,Y) + SafeSpawnHeightOffset`. |

**Voxel\|Actions**

| Button | What it does |
|--------|--------------|
| **Rebuild World** | `ClearWorld → GenerateWorldDeferred` with the **current** seed (no randomization). Use this when testing config changes on the same world layout. |
| **Clear Modifications** | Wipes all player voxel edits from `DataMap`. |
| **Run Tests** | Density / biome diagnostic checks printed to the output log. |

### Editor spawn simulation

`GenerateWorldDeferred` under `#if WITH_EDITOR` runs the full crater-search → PlayerStart relocation → spawn-chunk-prioritization sequence even in the editor viewport. The editor therefore shows the actual spawn area without requiring PIE.

---

## 10. How To Guide

### Add a new biome

1. Add config struct to `Config/SurfaceBiomesConfig.h` (e.g. `FSwampBiomeConfig`)
2. Add `EVoxelBiome::Swamp` to `Biomes/VoxelBiome.h`, increment `MaxBiomes = 7`
3. Add `EVoxelBiome::Swamp` to `GBiomeOrder[]` in `Generation/VoxelGeneratorTask.cpp` — the compile-time `static_assert` catches mismatches
4. Add `GetSwampHeight()` to `Biomes/VoxelBiomeGenerators.h/.cpp`
5. Wire weight calculation into `FVoxelBiomeManager::GetBiomeWeightsStatic`
6. Add `FVoxelBiomeRenderConfig SwampRender` and `FVoxelBiomeWaterConfig SwampWater` to `AVoxelWorld` and `FVoxelGenerationConfig`
7. Update `GetBiomeRender()` and `GetBiomeWater()` switch statements in `VoxelGenerationConfig.h`
8. Update `FVoxelBiomeWeightMap` in `VoxelBiome.h` — add a `Swamp` float member and update `SetWeight`, `GetWeight`, `GetDominantBiome`, `Normalize`, `operator[]`

### Add a new foliage type

In the AVoxelWorld Details panel → expand the target biome section → FoliageTypes → click `+`. Assign a static mesh and configure `SpawnChance`, `MinSlopeAlignment`, `MinWorldZ/MaxWorldZ`. Click **Generate World** to preview.

### Reproduce a specific world

Note the seed shown on screen after clicking **Generate World**, or read `GenerationConfig.Seed` in the Details panel. Set `bRandomizeSeedOnStartup = false` before PIE and assign the seed. The world will be identical every run.

### Reduce generation hitching

- Lower `MaxConcurrentGenerations` (default 12) — reduces CPU burst per tick
- Set `Performance.MaxNoiseOctaves = 2` — roughly halves density evaluation time
- Reduce `RenderDistanceXY` / `RenderDistanceZ` — fewer total chunks

### Mark a chunk dirty after a player edit

Call `AVoxelWorld::MarkChunkDirty(ChunkCoord)` instead of setting `bMeshDirty` directly. This appends to the `DirtyRebuildQueue` which drains at O(dirty-count) per tick rather than scanning all loaded chunks.

### Player terrain editing

```cpp
// Carve a sphere (negative density = air)
VoxelWorld->SetVoxelSphere(HitLocation, 200.f, -1.f, true);

// Fill a sphere (positive density = solid)
VoxelWorld->SetVoxelSphere(HitLocation, 200.f,  1.f, true);
```

Edits are stored in `FVoxelDataMap` and survive session via `SaveToFile` / `LoadFromFile`. Setting `bRebuildChunks = true` marks overlapping chunks `bMeshDirty`; they regenerate on the next `Tick()`.
