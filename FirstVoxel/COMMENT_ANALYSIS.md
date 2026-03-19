# FirstVoxel Engine — Missing Comments Analysis

> Generated: 2026-03-19
> Analysis of missing/inadequate comments across all systems
> NO CODE CHANGES — Documentation only

---

## Summary

Total issues identified: **187 missing/inadequate comments** across 25+ files.

| System | Files | Missing Comments | Priority |
|--------|-------|------------------|----------|
| World Management | 5 | 42 | High |
| Chunk Management | 5 | 31 | High |
| Generation Pipeline | 6 | 38 | High |
| Biome System | 5 | 29 | Medium |
| Water System | 7 | 24 | Medium |
| Configuration | 4 | 15 | Low |
| UI/Logging | 8 | 8 | Low |

---

## SYSTEM 1: WORLD MANAGEMENT

### VoxelWorld.h

**Missing @param/@return descriptions:**
- `FIntVector WorldToChunkCoord(const FVector &WorldPos) const;` — Missing `@param WorldPos`, `@return` description
- `FVector ChunkCoordToWorld(const FIntVector &Coord) const;` — Missing `@param Coord`, `@return` description
- `FVoxelDataMap *GetVoxelDataMap()` — No comment explaining what DataMap stores or when to use it
- `int32 GetQueueCount() const` — No comment explaining this is pending chunks in generation queue
- `int32 GetQueueHead() const` — No comment explaining QueueHead is a read index
- `float GetSurfaceZ(float X, float Y) const;` — No comment explaining this is an alias for GetTerrainHeight
- `void MarkChunkDirty(const FIntVector& Coord);` — Missing `@param Coord`

**UPROPERTY missing ToolTip:**
- `CraterSpawnSearchStep = 4000.f` — No explanation of interaction with search radius
- `CraterSpawnMinWeight = 0.25f` — No explanation of what 0.25 threshold means
- `SafeSpawnHeightOffset = 1500.f` — No explanation of why 1500cm (capsule half-height + clearance)

### VoxelWorld.cpp

**Functions missing comments:**
- `ClearWorld()` — No comment explaining it destroys chunks but preserves DataMap
- `MarkChunkDirty()` — No comment explaining it adds to DirtyRebuildQueue AND sets bMeshDirty
- `SnapPlayerToGround()` — No comment explaining it uses FindCraterSpawnLocation when bForceCraterSpawn is true

**Magic numbers without explanation:**
- `HoverPos.Z = TargetCoordsZ` in Tick — No comment explaining TargetCoordsZ is set by ProcessInitialPlayerSpawn
- `SpawnWaitAccum > 90.f` — No comment explaining why 90 seconds is the timeout
- `CollisionReadyCount / TotalCount` — No comment explaining why 0.99 cap while waiting

### VoxelWorldGeneration.cpp

**Functions missing comments:**
- `PerformWorldDiscoveryAndBoundsCalculation()` — No comment explaining this runs on ThreadPool
- `FinalizeGenerationSetup()` — No comment explaining this returns to GameThread
- `ConfigureChunk()` — No comment explaining it injects ForcedCraterCenter and SlopeThreshold

**Complex algorithms without explanation:**
- Chunk sorting by distance — No comment explaining the Manhattan distance sort
- Spawn area chunk prioritization — No comment explaining crater biome priority logic

### VoxelWorld_Streaming.cpp

**Functions missing comments:**
- `UpdateChunkStreaming()` — No comment explaining the LOD selection algorithm
- `CheckCloseRangeVisibility()` — No comment explaining why this proactively restores visibility

**Magic numbers:**
- `LOD1Distance * LOD1Distance` — No comment explaining why squared distance is used
- `HysteresisFactor * HysteresisFactor` — No comment explaining why hysteresis is squared

### VoxelWorldModification.cpp

**Functions missing comments:**
- `SetVoxelSphere()` — No comment explaining it uses WorldToChunkCoord for dirty chunk range
- `FindCraterSpawnLocation()` — No comment explaining the plateau tying algorithm
- `GetSafeSpawnHeightOffset()` — No comment explaining why 1500cm (capsule half-height 96cm + clearance)

---

## SYSTEM 2: CHUNK MANAGEMENT

### VoxelChunk.h

**Missing documentation:**
- `AVoxelChunk();` — No comment explaining what initialization it performs
- `~AVoxelChunk();` — No comment explaining what cleanup it performs
- `static FIntVector WorldToChunkIndex(const FVector& WorldLocation);` — Missing coordinate space explanation
- `static FVector ChunkIndexToWorld(const FIntVector& ChunkIndex);` — Missing return value meaning

**Thread safety not documented:**
- `bool bMeshDirty = false;` — No comment about thread safety (modified from multiple contexts)
- `bool bPendingLODTransition = false;` — No comment about thread safety
- `TSharedPtr<FVoxelGeneratorTask> CurrentTask;` — No comment about shared pointer lifecycle

### VoxelChunk.cpp

**Functions missing comments:**
- `GenerateAsync()` — No comment explaining it spawns a background task and wires OnGenerationComplete
- `CancelGeneration()` — No comment explaining it sets a flag but doesn't wait for completion
- `ApplyMesh()` — No comment explaining it runs on GameThread after async generation
- `Reset()` — No comment explaining it returns chunk to pool-ready state

**Magic numbers:**
- `LOD <= 1` for collision — No comment explaining why LOD 2+ has no collision

### VoxelChunkPool.cpp

**Functions missing comments:**
- `RetrieveOrCreateChunk()` — No comment explaining pool reuse vs creation logic
- `ReturnChunk()` — No comment explaining it resets chunk and adds to available pool

### VoxelChunkManager.h

**Missing documentation:**
- `GetOrCreateChunk()` — No comment explaining it creates DenseChunk for density storage

---

## SYSTEM 3: GENERATION PIPELINE

### VoxelGeneratorTask.cpp

**Magic numbers without explanation:**
- `const int32 MaxMeshesPerChunk = 4000;` — No comment explaining why 4000 is the GPU ceiling
- `if (WorldZ <= Config.SeaLevel + 200.f)` — The 200.f safety offset is unexplained
- `if (ForestW > 0.5f` — Why is 0.5 the forest weight threshold for tree spawning?
- `SpawnChance = FMath::Clamp(BWC.LakeSpawnProbability * 0.12f, ...)` — Why 0.12 multiplier for skyland lakes?
- `SpawnChance = FMath::Clamp(BWC.LakeSpawnProbability * EnclosureFactor * 0.15f, ...)` — Why 0.15 for surface lakes?
- `const float EnclosureFactor = (SolidNeighbours == 4) ? 1.5f : (SolidNeighbours == 3) ? 1.1f : 0.7f;` — What do these weights represent?

**Functions missing inline comments:**
- Data Map Pre-Cache loop (3×3 neighboring chunks) — No comment explaining why neighboring chunks are needed
- Skyland column cache precomputation — No comment explaining why ParallelFor is used
- Column preparation section — No comment explaining why NeutralSurfaceHeight is calculated separately

### VoxelGeneratorTask.h

**Missing @param descriptions:**
- Constructor parameters — Some parameters lack `@param` documentation
- `Execute()` — No comment explaining the pipeline order (Density → Mesh → Foliage)

### VoxelDensityGenerator.cpp

**Functions missing comments:**
- `GetDensity()` — No comment explaining the 3-layer merge order (Surface → Caves → Skylands)
- `FVoxelSurfacePass::EvaluateVoxel()` — No comment explaining how biome weights affect density

### VoxelMeshGenerator.cpp

**Complex algorithms without explanation:**
- Surface Nets Pass 1 — No comment explaining why averaging edge intersections works
- Surface Nets Pass 2 — No comment explaining the quad emission logic for X/Y/Z edges
- `EmitQuad()` — No comment explaining the winding order determination (bD0Solid)
- `FlattenCellTops()` — No comment explaining why it's disabled (chunk boundary tearing)

**Magic numbers:**
- `EdgeCount < 3` — No comment explaining why 3 is the minimum for a valid quad
- `AreaSq < 0.01f` — No comment explaining what 0.01 represents (1mm² degenerate check)

---

## SYSTEM 4: BIOME SYSTEM

### VoxelBiomeManager.cpp

**Functions missing comments:**
- `GetWeightsAndSurfaceHeightStatic()` — No comment explaining it's a convenience wrapper
- `GetTemperatureWithSeed()` — No comment explaining it returns normalized [0,1] using Perlin noise
- `GetErosionWithSeed()` — No comment explaining it returns normalized [0,1] with spatial offset

**Magic numbers:**
- `* 0.5f + 0.5f` — Should explain this maps [-1,1] noise to [0,1] range
- `+ 100.f` offset in GetErosionWithSeed — Should explain this decorrelates erosion from temperature
- `200.f` Z-slice for crater noise — Should explain this is pseudo-2D to avoid aliasing
- `* 0.5f` crater frequency — Should explain this halves frequency for larger craters
- `0.01f` crater override threshold — Should explain this is a dead-zone

### VoxelBiomeGenerators.cpp

**Functions missing comments:**
- `FBM()` — No comment explaining Fractal Brownian Motion algorithm
- `GetForestHeight()` — No comment explaining the height formula
- `GetDesertHeight()` — No comment explaining the sharpness power curve
- `GetPeaksHeight()` — No comment explaining why domain warping is disabled
- `GetCliffsHeight()` — No comment explaining the billow noise formula
- `GetMesaHeight()` — No comment explaining the 7-step generation process
- `GetCraterHeight()` — No comment explaining the hierarchical crater system
- `GetSkylandColumnCache()` — No comment explaining the shard system
- `GetCrystalCavernDelta()` — No comment explaining the carve + fill formula

**Complex algorithms:**
- Crater rim generation — No comment explaining the rising curve + thinning factor
- Crater ejecta system — No comment explaining the 3 components (blanket, blocks, strata)
- Skyland altitude calculation — No comment explaining the shard-to-island transition

### VoxelBiome.h

**Missing documentation:**
- `EVoxelBiome` enum — No comment explaining biome ordering or how to add new biomes
- `FVoxelBiomeWeightMap` — No comment explaining the normalization requirement

---

## SYSTEM 5: WATER SIMULATION

### WaterVoxelSimulator.cpp

**Functions without comments:**
- `ToChunkCoord()` — No comment explaining floor division for chunk grid
- `ToLocal()` — No comment explaining modulo for local chunk coordinates
- `CellPtr()` — No comment explaining it returns nullptr if chunk not loaded
- `CellPtrConst()` — No comment explaining read-only access
- `IsSolidAt()` — No comment explaining the SolidCells bitfield
- `GetLevel()` — No comment explaining WATER_SOURCE → WATER_FULL conversion
- `IsWater()` — No comment explaining distinction between water presence vs level
- `IsSolid()` — No comment explaining convenience wrapper

**Complex algorithms:**
- `SimCell()` — No comment explaining gravity + lateral spread + diagonal flow
- `SimulateStep()` — No comment explaining the iteration loop and stability check

### VoxelWaterTypes.h

**Missing documentation:**
- `WATER_FULL = 8` — No comment explaining why 8 levels (0-8)
- `WATER_SOURCE = 255` — No comment explaining this is an infinite water source
- `FWaterCell` struct — No comment explaining the packed storage format

### VoxelWorldWater.cpp

**Functions without comments:**
- `InitChunkWater()` — No comment explaining it creates water simulator entry
- `RemoveChunkFromWaterSimulation()` — No comment explaining cleanup process

---

## SYSTEM 6: CONFIGURATION

### VoxelGenerationConfig.h

**Missing ToolTip:**
- `SurfaceGradientScale` — No comment explaining how it affects density falloff
- `MaxNoiseOctaves` — No comment explaining performance impact
- `bEnable3DSkylandNoise` — No comment explaining when to enable/disable

### SurfaceBiomesConfig.h

**Missing ToolTip:**
- `CentralCraterRadius` — No comment explaining interaction with RimWidth
- `CentralCraterDepth` — No comment explaining negative values = below ground
- `EjectaBlanketWidth` — No comment explaining it's a fraction of crater radius

### SkylandsLayerConfig.h

**Missing ToolTip:**
- `BaseProbability` — No comment explaining interaction with HeightProbabilityBonus
- `ShardTransitionStrength` — No comment explaining the smoothstep range
- `MaxTerrainReference` — No comment explaining what terrain height this represents

### CaveLayerConfig.h

**Missing ToolTip:**
- `DepthStart` — No comment explaining it's measured from surface height
- `ChamberThreshold` — No comment explaining lower = larger chambers
- `VeinPower` — No comment explaining higher = thinner veins

---

## RECOMMENDATIONS

### Priority 1: Critical Documentation (Add First)
1. Thread safety notes on all shared state variables
2. @param/@return on all public API functions
3. Magic number explanations for values < 0.01 or > 1000
4. Algorithm overviews for Surface Nets, Crater Generation, Water Simulation

### Priority 2: Important Documentation
1. Inline comments for complex loops and conditionals
2. UPROPERTY ToolTip for all editor-exposed parameters
3. Coordinate system explanations for all conversion functions
4. Pipeline order documentation for generation stages

### Priority 3: Nice-to-Have Documentation
1. Example usage in header comments
2. Performance notes (O(n) complexity)
3. Cross-references to related functions
4. Historical "why" comments for non-obvious fixes