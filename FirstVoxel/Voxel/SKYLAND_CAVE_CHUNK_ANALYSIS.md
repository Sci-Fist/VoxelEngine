# Why entire chunks become full solid or full empty (skylands vs caves)

## What you're seeing

- **“Filling entire chunks”** can mean either:
  1. **Chunk is one solid block** (skyland block or filled “cave” with no holes), or  
  2. **Chunk is one big void** (entire chunk carved out like a single cave chamber).

Both come from the same place: for every voxel in the chunk, density ends up on the same side of the **iso-surface (0)** — either all **> 0** (solid) or all **≤ 0** (air).

---

## Root causes

### 1. Chunk entirely solid (one big block)

**A) Chunk is in the skyland band and the whole chunk passes the island test**

- In `VoxelBiomeGenerators::GetSkylandDensity`, the **shape test is 2D (XY only)** so all Z in a column share the same result.
- **Size noise is very low frequency** (0.00008) so it barely changes across one chunk (~3.2 km → ~0.25 in noise space).
- If that 2D shape is above the threshold for every (X,Y) in the chunk **and** the chunk’s Z range lies inside the island band (`SkyAlt ± HalfThick ± Margin`), then **every voxel** gets solid skyland density → **entire chunk is one skyland block**.
- Even with the “chunk-filling islands” cap (`Prob = FMath::Min(Prob, 0.45f)`), a single low-frequency “island” can still cover multiple chunks.

**B) Chunk is entirely below bedrock**

- In `VoxelDensityGenerator::GetDensityFull`, if `Z < Config.CaveTunnels.BedrockDepth` then `SurfD = 2.f`.
- For chunks with **Coord.Z** such that the whole chunk in world Z is below `BedrockDepth` (e.g. −5000 cm), **every voxel** is forced solid → **full bedrock chunk** (looks like “cave filled” with rock).

**C) Surface height far above the chunk**

- `GetBaseSurfaceDensity(Z, SurfaceHeight, Config)` is `(SurfaceHeight - Z) / SurfaceGradientScale`.
- If **SurfaceHeight** is very large for every column (e.g. config/noise putting “surface” at 50k+ cm), then for all Z in the chunk `SurfaceHeight - Z` is big and positive → **whole chunk solid** (underground rock).

---

### 2. Chunk entirely empty (one big void)

**A) Chunk is in the skyland band but no island**

- Chunk Z is above `SkyLowerBound`, so `GetSkylandDensity` runs.
- If for every (X,Y) in the chunk `ShapeXY <= Threshold`, the function returns **−2** (air).
- Surface density for that Z range is already negative (chunk above terrain), so **max(SkyD, SurfD)** stays negative → **entire chunk air** (floating void / “sky”).

**B) Crystal cavern carves the whole chunk**

- `GetCrystalCavernDelta` runs when `SurfD > 0.05f` (below surface, solid).
- Cavern ceiling is `SurfaceHeight - DepthStart` (e.g. surface − 3000 cm). So only voxels **below** that get carved.
- Chamber noise is FBM; if **ChamberThreshold - Min(Ch1, Ch2)** is large and positive across the chunk, **CarveFactor** is large (capped at 1.5), then `CavernDelta` is strongly negative (clamped to −1 in the density code).
- So for a chunk that sits entirely in the cavern layer, **every voxel** can get `SurfD += -1` → density drops below 0 → **entire chunk becomes one big cavern void**.

**C) Worm tunnels + cavern together**

- Tunnel carve is limited to 1.2 per voxel; cavern carve to 1.0. So you can subtract at most ~2.2 from `SurfD`.
- If base surface density is only slightly positive (e.g. 0.5–1.0) and both tunnel and cavern carve near their caps everywhere, the chunk can still go **all negative** → **full chunk void**.

---

## How to tell which case you have

- **Full solid:** Chunk renders as one continuous block (no holes, no terrain shape).  
  - High in the world → likely **skyland** (1A) or **surface too high** (1C).  
  - Low in the world → **bedrock** (1B) or **surface too high** (1C).
- **Full empty:** Chunk has no mesh (hole in the world).  
  - High in the world → **skyland band but no island** (2A).  
  - Below terrain → **crystal cavern (and/or tunnels)** carving the whole chunk (2B/2C).

---

## Recommended fixes / mitigations

1. **Skyland full-chunk blocks**
   - Increase **shape frequency** or **lower BaseIslandSize** so one “island” doesn’t span a full chunk.
   - Or add a **per-chunk guard**: e.g. if more than ~90% of voxels in the chunk would be skyland solid, blend down skyland strength or treat the chunk as “no island” so surface/air can show through.

2. **Cavern full-chunk voids**
   - **Raise ChamberThreshold** so fewer voxels get strong carve (smaller chambers).
   - **Lower ChamberStrength** so each voxel can’t subtract the full 1.0.
   - Or **increase ChamberFrequency** so the chamber pattern varies more within a chunk and you don’t get one chamber the size of a chunk.

3. **Bedrock full chunks**
   - Only chunks whose **entire Z range** is below `BedrockDepth` become full solid. If you don’t want huge solid layers, either **raise BedrockDepth** (e.g. −10000) or **reduce RenderDistanceZ** so you don’t load so many deep chunks.

4. **Surface height extremes**
   - Ensure **SurfaceGradientScale** is sane (e.g. 500) and that biome **HeightMin/HeightMax** and **SeaLevel** can’t combine into a “surface” at tens of thousands of cm. Add a clamp or warning if blended surface height goes outside a reasonable range (e.g. ±20000 cm).

5. **Debug**
   - Log or visualize **chunk Coord**, **WorldOrigin.Z**, and **SurfaceHeight** at chunk center; and for one voxel per chunk, log final density and which layer (surface / skyland / cave / bedrock) dominated. That will confirm whether you’re in case 1A/1B/1C or 2A/2B/2C.

Implementing (1) and (2) will most directly address “skylands or caves filling entire chunks” by avoiding single chunks that are entirely one skyland block or one cavern void.
