// VoxelBiomeGenerators.cpp
// Shape-function implementations for all surface biomes and the skylands /
// crystal cavern layers.

#include "VoxelBiomeGenerators.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "VoxelBiomeManager.h"

// ============================================================
//  FBM
// ============================================================
float FVoxelBiomeGenerators::FBM(float X, float Y, float Z, int32 Octaves,
                                 float Lacunarity, float Gain,
                                 int32 MaxOctaves) {
  const int32 ActualOctaves =
      FMath::Clamp(FMath::Min(Octaves, MaxOctaves), 1, 16);
  float Value = 0.f, Amp = 0.5f, Freq = 1.f;
  for (int32 i = 0; i < ActualOctaves; ++i) {
    Value += FastNoise3D(X * Freq, Y * Freq, Z * Freq) * Amp;
    Freq *= Lacunarity;
    Amp *= Gain;
  }
  return Value;
}

// ============================================================
//  FOREST - rolling hills and plains
// ============================================================
float FVoxelBiomeGenerators::GetForestHeight(
    float X, float Y, const FVoxelGenerationConfig &Config) {
  const FForestBiomeConfig &FC = Config.Forest;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y;

  float Base = FBM(nX * FC.NoiseFrequency, nY * FC.NoiseFrequency, 0.f,
                   FC.Octaves, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);

  float Normalized = (Base + 1.f) * 0.5f;
  float Detail =
      FastNoise3D(nX * FC.DetailFrequency, nY * FC.DetailFrequency, 0.f) *
      FC.DetailAmplitude;

  return Config.SeaLevel + FMath::Lerp(FC.HeightMin, FC.HeightMax, Normalized) +
         Detail;
}

// ============================================================
//  DESERT - rolling sand dunes
// ============================================================
float FVoxelBiomeGenerators::GetDesertHeight(
    float X, float Y, const FVoxelGenerationConfig &Config) {
  const FDesertBiomeConfig &DC = Config.Desert;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y;

  float Base = FBM(nX * DC.NoiseFrequency, nY * DC.NoiseFrequency, 40.f,
                   DC.Octaves, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);

  float Normalized = (Base + 1.f) * 0.5f;
  float Shaped = FMath::Pow(FMath::Max(0.f, Normalized), DC.Sharpness);

  float Detail =
      FastNoise3D(nX * DC.RippleFrequency, nY * DC.RippleFrequency, 0.f) *
      DC.RippleAmplitude;

  return Config.SeaLevel + FMath::Lerp(DC.HeightMin, DC.HeightMax, Shaped) +
         Detail;
}

// ============================================================
//  PEAKS - dramatic alpine mountains
//
//  FIX: Reduced domain warp magnitude from 2000 to 1000 and clamped Shaped
//  to [0,1] before the height lerp.  The old 2000cm warp could fold the noise
//  field back on itself at sharp warp boundaries, creating local normals that
//  shot straight up — the vertical spike artifact.
// ============================================================
float FVoxelBiomeGenerators::GetPeaksHeight(
    float X, float Y, const FVoxelGenerationConfig &Config) {
  const FPeaksBiomeConfig &PC = Config.Peaks;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y;

  const float WF = 0.0002f;
  // FIX: Disabled Domain Warping entirely on Peaks/Cliffs because 
  // infinite-slope creases produce mesh folds that generate spikes on the ground.
  const float WarpX = 0.f; 
  const float WarpY = 0.f; 

  float Base = FBM((nX + WarpX) * PC.NoiseFrequency,
                   (nY + WarpY) * PC.NoiseFrequency,
                   10.f, PC.Octaves, 2.0f, 0.5f,
                   Config.Performance.MaxNoiseOctaves);

  float Normalized = (Base + 1.f) * 0.5f;
  float Shaped = FMath::Pow(FMath::Clamp(Normalized, 0.f, 1.f), PC.Sharpness);
  Shaped = FMath::Clamp(Shaped, 0.f, 1.f); // guarantee no overshoot into HeightMax detail

  const float MaxDetail = (PC.HeightMax - PC.HeightMin) * 0.02f; 
  const float Detail = FastNoise3D(nX * PC.NoiseFrequency * 4.f,
                                   nY * PC.NoiseFrequency * 4.f, 0.f)
                       * FMath::Min(PC.DetailAmplitude, MaxDetail);

  return Config.SeaLevel + FMath::Lerp(PC.HeightMin, PC.HeightMax, Shaped) + Detail;
}

// ============================================================
//  CLIFFS - ridged, terraced terrain
//
//  FIX: Replaced the spike-generating ridged noise formula.
//  Old formula:  Ridge = 1 - Abs(FBM)  then  Pow(Ridge, Sharpness)
//  Problem:      FBM zero-crossings produce infinitely thin ridges. At those
//                crossings Ridge=1, everywhere else Ridge<1. Pow() crushes
//                non-ridge values toward 0, leaving only razor-thin spikes
//                regardless of Sharpness value.
//  New formula:  Use a smooth "billow" noise: Abs(FBM) remapped to [0,1].
//                Billowed noise has broad hills with rounded tops, not spikes.
//                Terracing is applied to the remapped value for cliff steps.
// ============================================================
float FVoxelBiomeGenerators::GetCliffsHeight(
    float X, float Y, const FVoxelGenerationConfig &Config) {
  const FCliffsBiomeConfig &CC = Config.Cliffs;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y;

  const float WF = 0.00015f;
  // FIX: Disabled Domain Warping for Cliff ridges to avoid micro-fold spikes on boundary seams.
  const float WarpX = 0.f;
  const float WarpY = 0.f;

  float Base = FBM((nX + WarpX) * CC.NoiseFrequency,
                   (nY + WarpY) * CC.NoiseFrequency,
                   15.f, CC.Octaves, 2.1f, 0.55f,
                   Config.Performance.MaxNoiseOctaves);

  // Billow noise: FMath::Abs(Base) creates broad rounded tops instead of razor ridges
  float Shaped = FMath::Pow(FMath::Abs(Base), CC.Sharpness); 
  Shaped = FMath::Clamp(Shaped, 0.f, 1.f);

  // Terrace: floor-snap to create cliff ledge steps.
  if (CC.TerraceSteps > 0 && CC.TerraceFactor > 0.f)
  {
    const float StepScale = (float)CC.TerraceSteps;
    const float Terrace   = FMath::Floor(Shaped * StepScale) / StepScale;
    Shaped = FMath::Lerp(Shaped, Terrace, CC.TerraceFactor);
  }

  // Detail noise: small-scale surface roughness, scaled relative to height
  // range so it never dominates the overall silhouette.
  const float Detail = FastNoise3D(nX * CC.NoiseFrequency * 6.f,
                                   nY * CC.NoiseFrequency * 6.f, 0.f)
                       * CC.DetailAmplitude;

  return Config.SeaLevel + FMath::Lerp(CC.HeightMin, CC.HeightMax, Shaped) + Detail;
}

// ============================================================
//  MESA - flat-top plateaus
// ============================================================
float FVoxelBiomeGenerators::GetMesaHeight(
    float X, float Y, const FVoxelGenerationConfig &Config) {
  const FMesaBiomeConfig &MC = Config.Mesa;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y;

  const float Freq = 0.0001f;
  float Base = FBM(nX * Freq, nY * Freq, 20.f, 4, 2.0f, 0.5f,
                   Config.Performance.MaxNoiseOctaves);

  const float StepScale = (float)MC.PlateauSteps;
  const float Plateau = FMath::Floor(Base * StepScale) / StepScale;
  
  // 1. Sharpen edge cliff falloff profile for flat plateaus
  const float Sharpness = MC.EdgeSharpness + 4.0f;
  float EdgeBlend = FMath::SmoothStep(0.f, 1.f, (Base - Plateau) * Sharpness);
  const float Shape = Plateau + (FMath::Pow(EdgeBlend, 1.5f) / StepScale);

  const float Normalized = (Shape + 1.f) * 0.5f;
  float Height = Config.SeaLevel + MC.HeightBase +
                 Normalized * (MC.HeightMax - MC.HeightBase);

  // 2. Add Horizontal Strata Layering (layered sandstone shelves)
  // Operates strictly on height coordinates output for continuous flat ledge breaks.
  const float LayerFreq = 0.005f;  // Approx 1 shelf every 20m 
  const float LayerStrength = 180.f; // 1.8m ledge depth
  float Stratification = FMath::Sin(Height * LayerFreq);
  Stratification = 1.0f - FMath::Abs(Stratification); // ridged sharp peaks

  // 3. High frequency crack detail to break smooth FBM faces
  float Crackle = FastNoise3D(nX * 0.008f, nY * 0.008f, 0.f) * 160.f;

  return Height + (Stratification * LayerStrength) + Crackle;
}

// ============================================================
//  CRATERS - impact basins with raised rims
//
//  REWRITE: The old implementation had three hard if/else zone switches
//  (NormalizedDepth > 0.45 / > 0.30 / > 0.15) that created step-function
//  discontinuities in the height field.  Surface Nets generates a thin
//  vertical column at every discontinuity -> the pillar forest seen at spawn.
//
//  Additional bugs fixed:
//  - Depth * 8.0 multiplier: Depth=-4000 * 8 = -32000cm (320m deep craters).
//    Replaced with Depth * 1.0 and sensible config defaults.
//  - RimNoiseAmplitude=4000: 40m of rim noise guarantees pillar spikes.
//    Config now defaults to 500cm.
//  - ShapeDistortion=0.5 + BorderIrregularity=0.8: excessive chaos in Impact
//    created chaotic height jumps that blended badly with adjacent biomes.
//
//  New approach: all zone transitions use SmoothStep / cubic easing so the
//  height field is C1-continuous everywhere -> no pillar artifacts.
// ============================================================
float FVoxelBiomeGenerators::GetCraterHeight(
    float X, float Y, const FVoxelGenerationConfig &Config) {
  const FCraterBiomeConfig &CRC = Config.Craters;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y;

  // Crater placement field (low-frequency, matches biome weight field)
  const float ModifiedFrequency = CRC.CraterSizeMultiplier > 0.f
      ? CRC.Frequency / CRC.CraterSizeMultiplier : CRC.Frequency;

  float Impact = FastNoise3D(nX * (ModifiedFrequency * 0.5f),
                             nY * (ModifiedFrequency * 0.5f), 200.f);

  // Subtle organic distortion - smooth swelling multipliers
  const float Distort = FastNoise3D(nX * 0.0012f, nY * 0.0012f, 400.f) * CRC.ShapeDistortion;
  const float Border  = FastNoise3D(nX * 0.0010f, nY * 0.0010f, 500.f) * CRC.BorderIrregularity;
  Impact += Distort + Border;

  // Map Impact into [0, 1] where 0 = flat plains, 1 = crater centre
  const float Denominator = 1.f - CRC.ImpactThreshold;
  const float NormDepth = (Denominator > 0.001f)
      ? FMath::Clamp((Impact - CRC.ImpactThreshold) / Denominator, 0.f, 1.f)
      : 0.f;

  // If NormDepth == 0 we are outside the crater entirely -> plain terrain
  if (NormDepth <= 0.f) return Config.SeaLevel + 1000.f;

  const float BasePlains = Config.SeaLevel + 1000.f;

  // --- Rim peak (sits between plains and floor) --------------------------
  const float RimCenter = 0.25f;
  const float RimWidth  = 0.22f; 
  const float RimT      = FMath::Max(0.f, 1.f - FMath::Square((NormDepth - RimCenter) / RimWidth));
  // FIX: lowered frequency to 0.0012f to prevent sawtooth jagged artifacts (was 0.008f)
  const float RimNoise  = FastNoise3D(nX * 0.0012f, nY * 0.0012f, 0.f) * CRC.RimNoiseAmplitude;
  const float RimPeak   = BasePlains + CRC.RimHeight + RimNoise * RimT;

  // --- Floor (deep centre of the crater) --------------------------------
  // DepthCurve: 0 at rim, 1 at centre.  Use smoothstep so descent is gradual.
  const float FloorStart = 0.28f; 
  const float WallEnd    = 0.50f; // Wall drops fully by 50% radius index
  const float FloorT     = FMath::SmoothStep(FloorStart, WallEnd, FMath::Min(NormDepth, WallEnd));
  const float FloorNoise = FBM(nX * CRC.BuildingNoiseFrequency,
                               nY * CRC.BuildingNoiseFrequency, 0.f,
                               2, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves)
                           * CRC.BuildingNoiseAmplitude;
  const float FloorDepth = BasePlains + CRC.Depth * FloorT + FloorNoise * FloorT;

  float Height;
  if (NormDepth <= RimCenter)
  {
      const float t = FMath::SmoothStep(0.f, RimCenter, NormDepth);
      Height = FMath::Lerp(BasePlains, RimPeak, t);
  }
  else
  {
      const float t = FMath::SmoothStep(RimCenter, WallEnd, FMath::Min(NormDepth, WallEnd));
      Height = FMath::Lerp(RimPeak, FloorDepth, t);
  }

  return Height;
}

// ============================================================
//  SKYLANDS - coherent floating islands
//
//  DESIGN PRINCIPLES:
//  - Altitude is CONSTANT per island (no per-voxel AltBoost) to prevent shard
//  artifacts
//  - Shape test is primarily 2D (XY) so island interiors are always solid
//  - Minimal Z-variation in noise prevents vertical holes through islands
//  - Mountains -> high altitude, large, frequent islands
//  - Plains    -> low altitude, small, sparse islands (shard rocks)
//
//  Fix history:
//  - Removed AltBoost = SizeFactor * 12000: was causing altitude to vary 120m
//  per-pixel,
//    which made the Z-gate fire inconsistently within a chunk creating vertical
//    black slabs
//  - Reduced Z frequency from 0.4x to 0.05x: prevents Swiss cheese holes
//  - Primary shape test is 2D (early-exit): guarantees solid island interiors
//  - Capped shape octaves at 2 for stability over flat terrain
// ============================================================
float FVoxelBiomeGenerators::GetSkylandDensity(
    float X, float Y, float Z, float SurfaceHeight,
    const FVoxelBiomeWeightMap &Weights, const FVoxelGenerationConfig &Config,
    int32 StepSize) {
    FSkylandColumnCache Cache = GetSkylandColumnCache(X, Y, SurfaceHeight, Weights, Config);
    return GetSkylandDensityFromCache(Cache, X, Y, Z, Config, StepSize);
}


FSkylandColumnCache FVoxelBiomeGenerators::GetSkylandColumnCache(

    float X, float Y, float SurfaceHeight,

    const FVoxelBiomeWeightMap& Weights,

    const FVoxelGenerationConfig& Config)

{

    FSkylandColumnCache Cache;

    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;

    const FVector Off = Config.GetSeedOffset();

    // ============================================================
    //  SKYLAND GENERATION OVERVIEW & SPIKE FIX
    // ============================================================

    //

    // PURPOSE: Generate floating islands ("skylands") that appear high above terrain.
    // The system uses a cellular grid where each cell may spawn an island. The island's

    // properties (size, altitude, thickness, shape) are determined by the terrain
    // characteristics (height, roughness) beneath that cell.
    //
    // TWO-PHASE EVALUATION:
    //  1. Column Cache (this function): Expensive O(n²) operation run once per XY column.

    //     Samples 9 neighboring grid cells, selects the best one (nearest with valid spawn),
    //     and caches its properties (SkyAlt, HalfThick, Freq, Threshold, etc.).
    //  2. Voxel Density: Cheap O(1) lookup using the cache. Evaluates shape noise and
    //     vertical falloff to produce the final signed distance value.
    //
    // WHY SKYLANDS BECAME SPIKY (pre-fix):
    //  - Thickness scaled linearly with IslandSize: HalfThick = IslandSize * EffThickness
    //  - Horizontal feature size (noise wavelength) scaled with sqrt(IslandSize):

    //      Freq = ShapeFrequency / sqrt(SizeRatio)   where SizeRatio = IslandSize / BaseIslandSize
    //  - As islands grew larger (high terrain), thickness grew faster than horizontal extent.

    //    The aspect ratio (thickness/width) increased ~sqrt(SizeRatio), turning large islands
    //    into tall thin pillars instead of flat discs.
    //
    // THE FIX (implemented below):

    //  1. Linear frequency scaling: Freq = ShapeFrequency / SizeRatio

    //     This makes wavelength proportional to island size, maintaining consistent aspect

    //     ratio across all scales. Large islands are now properly wide and flat.
    //  2. Threshold reduction for large islands:

    //      if (CellShardT > 0.5f) Threshold -= log2(SizeRatio) * 0.05f
    //     Lowers the shape threshold so noise lobes merge into one coherent disc instead of
    //     many separate peaks. Without this, large islands would still be spiky even with
    //     correct frequency scaling.
    //  3. MaxThicknessRatio clamp: Safety net to prevent extreme aspect ratios from
    //     misconfigured parameters. HalfThick = min(HalfThick, IslandSize * MaxThicknessRatio).
    //  4. Clearance fix: After selecting the best cell, raise the island if its bottom would
    //     intersect the local terrain (using the column's SurfaceHeight, not the cell's
    //     AltitudeBase). Ensures visible gap above ground everywhere.
    //
    // ASPECT RATIO CONTROL:
    //  Desired: Aspect = HalfThick / IslandSize ≈ 0.1–0.3 (flat disc)
    //  With EffThickness = 0.1 (shards) to 0.2–0.3 (islands) and proper frequency scaling,

    //  the noise solid region radius ≈ 0.5–0.7 * IslandSize, giving Aspect ≈ 0.14–0.42.
    //  The MaxThicknessRatio (default 0.3) caps aspect at 0.6 even if config is extreme.
    //
    // PERFORMANCE NOTE:

    //  This function is called once per XY column during chunk generation. All expensive
    //  operations (biome sampling, noise evaluations) are confined here. The returned
    //  cache is reused for every Z voxel in that column, making skyland evaluation cheap.
    // ============================================================


    // ---------------------------------------------------------------
    //  SHARD SYSTEM: altitude-driven grid density
    //
    //  High terrain  -> large GridSize  -> islands are rare, large, spaced far apart
    //  Low terrain   -> small GridSize  -> shards are tiny, numerous, scattered
    //
    //  We compute a representative HeightNorm for THIS column first so we can
    //  set the grid size before we search cells. We use the passed-in SurfaceHeight
    //  rather than re-sampling, keeping the column-cache call cheap.
    // ---------------------------------------------------------------
    const float ColHeightNorm    = FMath::Clamp(SurfaceHeight / SC.MaxTerrainReference, 0.f, 1.f);
    const float ColRoughnessNorm = FMath::Clamp(Weights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);
    const float ColTerrainStr    = FMath::Clamp(ColHeightNorm * 1.5f + ColRoughnessNorm * 0.8f, 0.f, 1.f);

    // ShardT: 0 = pure low-altitude shard field, 1 = full-size high-altitude island
    // Uses a smoothstep so the transition from shard -> island is gradual.
    const float ShardT = FMath::SmoothStep(0.0f, SC.ShardTransitionStrength, ColTerrainStr);

    // Grid size: shards use a much tighter grid so there are more of them.
    // High islands:  BaseIslandSize * 4  (wide spacing, few large islands)
    // Low shards:    BaseIslandSize * 1  (tight spacing, many tiny shards)
    const float GridSize = SC.BaseIslandSize * FMath::Lerp(1.0f, 4.0f, ShardT);
    if (GridSize <= 0.f) return Cache;

    const int32 CellX = FMath::FloorToInt(X / GridSize);
    const int32 CellY = FMath::FloorToInt(Y / GridSize);


    float MaxW             = -1.f;
    float BestSkyAlt       = 0.f;
    float BestHalfThick    = 0.f;

    float BestThreshold    = 0.f;

    float BestHeightNorm   = 0.f;

    float BestShardFalloff = 0.f;

    float BestFreq         = 0.f;

    float BestIslandSize   = 0.f;


    float BestCellShardT   = 0.f;

    float BestDistRatio    = FLT_MAX;  // Lower is better - normalized distance to cell center



    for (int32 dx = -1; dx <= 1; ++dx)
    for (int32 dy = -1; dy <= 1; ++dy)
    {
        const int32 currentCellX = CellX + dx;
        const int32 currentCellY = CellY + dy;

        const float nX = (float)currentCellX * GridSize + Off.X;
        const float nY = (float)currentCellY * GridSize + Off.Y;

        const float HashX = (FastNoise3D(nX * 0.001f, nY * 0.001f, 0.f) + 1.f) * 0.5f;
        const float HashY = (FastNoise3D(nX * 0.001f, nY * 0.001f, 100.f) + 1.f) * 0.5f;

        const float CenterX = (currentCellX + 0.12f + HashX * 0.76f) * GridSize;
        const float CenterY = (currentCellY + 0.12f + HashY * 0.76f) * GridSize;

        const float DistSq = FMath::Square(X - CenterX) + FMath::Square(Y - CenterY);
        const float Dist   = FMath::Sqrt(DistSq);

        // --- Evaluate cell terrain ---
        const FVoxelBiomeWeightMap CenterWeights = FVoxelBiomeManager::GetBiomeWeightsStatic(CenterX, CenterY, Config);
        
        // Calculate crater-neutral height so islands do not drop into local depressions
        FVoxelBiomeWeightMap NeutralWeights = CenterWeights;
        NeutralWeights.SetWeight(EVoxelBiome::Craters, 0.f);
        NeutralWeights.Normalize();
        
        const float CenterHeight = FVoxelBiomeManager::GetSurfaceHeightStatic(CenterX, CenterY, NeutralWeights, Config);

        const float HeightNorm    = FMath::Clamp(CenterHeight / SC.MaxTerrainReference, 0.f, 1.f);
        const float RoughnessNorm = FMath::Clamp(CenterWeights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);

        const float CurvedHeight  = FMath::Pow(FMath::Max(0.f, HeightNorm), 2.5f);
        const float CurvedRough   = FMath::Pow(FMath::Max(0.f, RoughnessNorm), 2.0f);
        const float TerrainStr    = FMath::Clamp(HeightNorm * 1.5f + RoughnessNorm * 0.8f, 0.f, 1.f);
        const float ShardFalloff  = FMath::Pow(FMath::Max(0.f, TerrainStr), 2.2f);

        // CellShardT: same altitude ramp but per-cell so size/thickness are
        // evaluated against the cell's own terrain, not the query column.
        const float CellShardT = FMath::SmoothStep(0.0f, SC.ShardTransitionStrength, TerrainStr);

        // Minimum ShardFalloff gate: disabled to allow sparse absolute-Altitude shards over plains
        // if (ShardFalloff < 0.008f) continue;

        const float cnX = CenterX + Off.X;
        const float cnY = CenterY + Off.Y;
        const float HashProb = (FastNoise3D(cnX * 0.002f, cnY * 0.002f, 200.f) + 1.f) * 0.5f;

        // -------------------------------------------------------------------
        //  PROBABILITY
        //  Low terrain  -> BaseProbability only (sparse scattered shards)
        //  High terrain -> BaseProbability + HeightBonus + RoughnessBonus
        //  CurvedHeight applies a power curve so probability rises steeply
        //  only over genuinely tall terrain, not gradual plains.
        // -------------------------------------------------------------------
        float Prob = FMath::Lerp(SC.BaseProbability, SC.BaseProbability + SC.HeightProbabilityBonus, CellShardT);
        if (HashProb > Prob) continue;

        // -------------------------------------------------------------------
        //  SIZE: aggressive altitude falloff
        //  High island:   BaseIslandSize + HeightSizeBonus + RoughnessSizeBonus
        //  Low shard:     BaseIslandSize * ShardMinScale  (very small)
        //
        // FIX: Raise ShardMinScale to ensure shards have a core width that supports 3D noise detail
        const float ShardMinScale = FMath::Max(0.20f, SC.ShardMinScale); // Wide enough to build decent features (was SC.ShardMinScale)
        const float SizeNoise   = FBM(cnX * 0.00008f, cnY * 0.00008f, 50.f, 2, 2.0f, 0.5f, 2);
        const float SizeFactor  = (SizeNoise + 1.f) * 0.5f;

        // Base size at this altitude: lerp from tiny shard to full island.
        float IslandSize = FMath::Lerp(
            SC.BaseIslandSize * ShardMinScale,
            SC.BaseIslandSize + SC.HeightSizeBonus,
            CellShardT);

        // Apply per-cell size noise (±50% at low altitude, ±25% at high).
        const float NoiseRange = FMath::Lerp(0.5f, 0.25f, CellShardT);
        IslandSize *= (1.f - NoiseRange) + NoiseRange * SizeFactor * 2.f;
        IslandSize  = FMath::Max(IslandSize, 150.f);  // absolute minimum: 1.5m

        // Never exceed grid cell to avoid overlapping adjacent cells.
        IslandSize = FMath::Min(IslandSize, GridSize * 0.48f);

        if (Dist > IslandSize) continue;

        // -------------------------------------------------------------------
        //  ALTITUDE: low shards float just above terrain, high islands soar
        // -------------------------------------------------------------------
        const float AltitudeBase = FMath::Lerp(SC.MinAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, TerrainStr);
        // FIX: Removed 150m absolute anchor that forced shards sky-high.
        // Setting DecoupledHeight directly to CenterHeight allows shards to hover contextually just above ground.
        const float DecoupledHeight = CenterHeight; 

        // FIX: Match altitude formula in VoxelWorld_Streaming.cpp exactly.
        // Previously missing the CurvedHeight/Rough curve factors, causing
        // islands and streamed Volumes to drift apart by up to 20 meters.
        const float SkyAlt = DecoupledHeight + AltitudeBase
            + CellShardT * (CurvedHeight * SC.HeightAltitudeBonus + CurvedRough * SC.RoughnessAltitudeBonus);

        // SIZE BY ALTITUDE: shards that float higher above local terrain are bigger.
        // A shard barely clearing a hillside = small pebble.
        // A shard soaring 150m above flat plains = dramatic sky boulder.
        // Islands (CellShardT=1) are unaffected (factor lerps to 1.0).
        {
            const float AltGap        = FMath::Max(0.f, SkyAlt - CenterHeight);
            const float RefGap        = FMath::Max(1.f, SC.MinAltitudeAboveTerrain);
            const float AltSizeScale  = FMath::Clamp(AltGap / RefGap, 0.4f, 3.0f);
            IslandSize *= FMath::Lerp(AltSizeScale, 1.0f, CellShardT);
            IslandSize  = FMath::Max(IslandSize, 150.f);         // 1.5m minimum
            IslandSize  = FMath::Min(IslandSize, GridSize * 0.48f); // never overlap cells
        }


        // -------------------------------------------------------------------

        //  THICKNESS: shards are chunky rocks, islands are flat discs
        //
        //  ROCK SHAPE FIX:
        //  Old: EffThickness=0.10 for shards → razor-thin pancake → looks like
        //       a vertical slab/pillar from the side. Surface Nets generates a
        //       1-voxel-thin quad that reads as a pillar.
        //  New: EffThickness=0.65 for shards → near-spherical boulder aspect.
        //       With IslandSize=625cm → HalfThick≈406cm → proper 3D rock shape.
        //
        //  ThicknessRatio (islands, CellShardT=1): stays at SC.ThicknessRatio
        //  (default 0.2) so full skylands remain flat floating platforms.
        // -------------------------------------------------------------------
        // FIX: Add random aspect ratio for shards. Flat pancakes preferred!
        const float HashAspect = (FastNoise3D(cnX * 0.005f, cnY * 0.005f, 300.f) + 1.f) * 0.5f;
        const float ShardThickBase = FMath::Lerp(0.12f, 0.25f, HashAspect); // 12% to 25% wide ratio (was 35-75%)
        const float EffThickness = FMath::Lerp(ShardThickBase, SC.ThicknessRatio, CellShardT);

        float HalfThick = IslandSize * EffThickness;

        // --- 🪨 MAX THICKNESS RATIO: per-type limits ---
        // Shards (rocks):  allow near-spherical aspect (0.75 → height = 75% of radius)
        // Islands (discs): keep SC.MaxThicknessRatio (default 0.3 → flat floating platform)
        const float EffMaxThicknessRatio = FMath::Lerp(0.75f, SC.MaxThicknessRatio, CellShardT);
        const float MaxAllowedHalfThick  = IslandSize * EffMaxThicknessRatio;

        HalfThick = FMath::Min(HalfThick, MaxAllowedHalfThick);

        

        // --- CLEARANCE PROTECTION: commented out per user request ---
        // The 200cm buffer was clamping HalfThick down too aggressively,
        // causing shards to lose thickness and appear as thin pillar slabs.
        // Island altitude is already set well above terrain via AltitudeBase;
        // the post-selection column-height adjustment below handles real clipping.
        //
        // const float Clearance = 200.f;
        // const float MaxAllow  = AltitudeBase - Clearance;
        // if (MaxAllow <= 0.f)
        //     HalfThick = 0.01f;
        // else
        //     HalfThick = FMath::Min(HalfThick, MaxAllow);


        // shape threshold

        const float ShardThresholdBoost = FMath::Lerp(0.20f, 0.0f, CellShardT);

        float Threshold = FMath::Lerp(SC.ThresholdAtMinProbability, SC.ThresholdAtMaxProbability, CellShardT)
                                + ShardThresholdBoost;
        
        // Ensure larger islands get lower threshold to merge noise features into a single disc
        // Without this, large islands would be many small peaks instead of one coherent shape
        if (CellShardT > 0.5f) {
            const float SizeRatio = FMath::Max(1.f, IslandSize / SC.BaseIslandSize);
            Threshold -= FMath::Log2(SizeRatio) * 0.05f;  // Lower threshold for larger islands

        }

        
        // Ensure larger islands get lower threshold to merge noise features into a single disc
        // Without this, large islands would be many small peaks instead of one coherent shape
        if (CellShardT > 0.5f) {
            const float SizeRatio = FMath::Max(1.f, IslandSize / SC.BaseIslandSize);
            Threshold -= FMath::Log2(SizeRatio) * 0.05f;  // Lower threshold for larger islands
        }



        // Continuous blend weight (used for MaxW check)

        const float W = FMath::Square(1.f - (Dist / IslandSize));



        // Normalized distance for fair cell selection across different island sizes
        const float DistRatio = Dist / IslandSize;



        // Track the cell with smallest normalized distance (nearest cell)
        if (DistRatio < BestDistRatio && W > 0.001f)  // Only consider cells with meaningful influence

        {

            MaxW             = W;

            BestSkyAlt       = SkyAlt;
            BestHalfThick    = HalfThick;
            BestThreshold    = Threshold;
            BestHeightNorm   = HeightNorm;
            BestShardFalloff = ShardFalloff;
            BestIslandSize   = IslandSize;
            BestCellShardT   = CellShardT;

            const float SizeRatio = FMath::Max(1.f, IslandSize / SC.BaseIslandSize);
            const float IslandFreq = SC.ShapeFrequency / SizeRatio;
            const float ShardFreq  = SC.ShapeFrequency * 6.0f;
            BestFreq = FMath::Lerp(ShardFreq, IslandFreq, CellShardT);

            // FIX: Update the tracking ratio so it correctly selects the NEAREST cell 
            // instead of falling back to the last cell in the grid loop iterator.
            BestDistRatio = DistRatio; 
        }
    }


    if (MaxW <= 0.f) return Cache;




    Cache.SkyAlt       = BestSkyAlt;



    Cache.HalfThick    = BestHalfThick;



    Cache.Threshold    = BestThreshold;



    Cache.HeightNorm   = BestHeightNorm;



    Cache.ShardFalloff = BestShardFalloff;

    // ShardT drives falloff shape and noise in GetSkylandDensityFromCache.
    Cache.ShardT      = BestCellShardT;


    // Post-selection terrain clearance adjustment: commented out per user request.
    // The 200cm forced-rise was pushing islands upward even when they were already
    // correctly placed, which combined with MinAltitudeAboveTerrain was double-offsetting
    // the altitude and causing islands to clip into high terrain on the way up.
    // AltitudeBase already guarantees separation; let the density gate in
    // GetSkylandDensityFromCache (HeightCutoff fade) handle the isosurface boundary.
    //
    // const float Clearance = 200.f;
    // const float MinBottom = SurfaceHeight + Clearance;
    // const float CurrentBottom = Cache.SkyAlt - Cache.HalfThick;
    // if (CurrentBottom < MinBottom)
    // {
    //     const float NeededRise = MinBottom - CurrentBottom;
    //     Cache.SkyAlt += NeededRise;
    // }




    // Freq: shards need much higher frequency noise to look jagged.

    // Low shards: ShapeFrequency * 6  (high-freq = rough, spiky silhouette)

    // High islands: ShapeFrequency / sqrt(SizeRatio)  (smooth, organic)

    // Use the selected cell's island size (not blended) for consistent shape


    const float SizeRatio = FMath::Max(1.f, BestIslandSize / SC.BaseIslandSize);



    // FIX: Linear scaling (was sqrt) to maintain consistent aspect ratio across island sizes.
    // With sqrt scaling, large islands had disproportionately small horizontal extent,

    // causing spikes. Linear scaling makes wavelength ∝ IslandSize, so solid region scales
    // proportionally with thickness → flat discs at all sizes.
    const float IslandFreq = SC.ShapeFrequency / SizeRatio;  // Linear scaling for consistent aspect ratio



    const float ShardFreq = SC.ShapeFrequency * 6.0f;


    Cache.Freq = FMath::Lerp(ShardFreq, IslandFreq, BestCellShardT);

    Cache.Freq = FMath::Max(Cache.Freq, 0.00025f);




    Cache.Prob    = 0.5f;





    Cache.WX_base = X + Off.X;



    Cache.WY_base = Y + Off.Y;



    Cache.WX      = Cache.WX_base;



    Cache.WY      = Cache.WY_base;



    Cache.bHasSkyland = true;



    

    // Aspect Ratio logging removed for performance tuning (was UE_LOG spamming per column).

    

    return Cache;


}

float FVoxelBiomeGenerators::GetSkylandDensityFromCache(
    const FSkylandColumnCache& Cache, float X, float Y, float Z,
    const FVoxelGenerationConfig& Config, int32 StepSize)
{
    if (!Cache.bHasSkyland) return -2.f;

    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
    const FVector Off = Config.GetSeedOffset();

    const float HalfThick = Cache.HalfThick;

    const float Margin = HalfThick * 0.4f;
    if (Z < Cache.SkyAlt - HalfThick - Margin || Z > Cache.SkyAlt + HalfThick + Margin) return -2.f;

    const float FullRange = HalfThick + Margin;
    const float tCenter = FMath::Clamp((Z - Cache.SkyAlt) / (FullRange + 1.f), -1.f, 1.f);

    // FALLOFF SHAPE: blend between rock (spherical) and island (flat-top plateau).
    //
    // Island falloff (ShardT=1): flat top zone (35%) + smooth underside taper.
    //   Creates the "floating platform" look — flat on top, tapered underneath.
    //
    // Rock falloff  (ShardT=0): symmetric spherical — equal taper in all Z directions.
    //   No flat zone → looks like a boulder/rock, not a platform.
    //   Uses pow(|t|, 0.6) for a slightly boxy rock profile (flatter than a perfect
    //   sphere at center, sharper at the edges).

    // --- Island falloff (flat-top) ---
    float IslandFalloff;
    {
        if (tCenter >= 0.f) {
            const float FlatZone = 0.35f;
            if (tCenter < FlatZone) {
                IslandFalloff = 1.0f;
            } else {
                const float nt = (tCenter - FlatZone) / (1.f - FlatZone);
                IslandFalloff = FMath::SmoothStep(0.f, 1.f, 1.f - nt);
            }
        } else {
            const float t = FMath::Clamp(-tCenter, 0.f, 1.f);
            IslandFalloff = FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(t, 0.85f));
        }
    }

    // --- Rock falloff (spherical, no flat zone) ---
    const float tAbs      = FMath::Abs(tCenter);
    const float RockFalloff = FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(tAbs, 0.6f));

    // FIX: Blend at least 40% IslandFalloff onto shards to give them flat tops
    const float Falloff = FMath::Lerp(RockFalloff, IslandFalloff, FMath::Max(0.40f, Cache.ShardT));

    const float WX_base = X + Off.X;
    const float WY_base = Y + Off.Y;
    const float WZ = Z + Off.Z;

    if (Falloff < 0.001f)
    {
        const float MaxBreakUpEO    = FMath::Lerp(0.50f, 2.80f, Cache.HeightNorm);
        const float BreakUpStrengthEO = FMath::Lerp(0.10f, MaxBreakUpEO, Cache.ShardT);
        const float BreakUp = FMath::Max(0.f, FastNoise3D(WX_base * 0.002f, WY_base * 0.002f, WZ * 0.001f)) * BreakUpStrengthEO;
        return FMath::Clamp(-1.8f - BreakUp, -2.f, 2.f);
    }

    float WX = WX_base;
    float WY = WY_base;

    if (SC.bEnableDomainWarping) {
      const float WF = SC.DomainWarpFrequency;
      WX += FastNoise3D(WX * WF + 10.f, WY * WF + 20.f, 0.f) * SC.DomainWarpStrength;
      WY += FastNoise3D(WX * WF + 50.f, WY * WF + 10.f, 0.f) * SC.DomainWarpStrength;
    }

    // 3D SHAPE NOISE:
    // Islands (ShardT=1): very low Z frequency (0.05x) keeps island interior solid —
    //   prevents swiss-cheese vertical holes through large platforms.
    // Rocks  (ShardT=0): higher Z frequency (0.50x) gives irregular 3D boulder surface.
    //   Strength also raised (0.55) so the rock surface is visibly lumpy/craggy.
    //   3D noise is ALWAYS evaluated for shards regardless of bEnable3DSkylandNoise flag.
    float ShapeDetail = 0.f;
    {
        const float ZFreqScale     = FMath::Lerp(0.50f, 0.05f, Cache.ShardT);
        const float DetailStrength = FMath::Lerp(0.55f, 0.25f, Cache.ShardT);
        if (Config.Performance.bEnable3DSkylandNoise || Cache.ShardT < 0.5f)
        {
            ShapeDetail = FastNoise3D(
                WX * Cache.Freq * 0.6f,
                WY * Cache.Freq * 0.6f,
                WZ * Cache.Freq * ZFreqScale) * DetailStrength;
        }
    }

    // Point-wise ShapeXY prevents absolute grid-cell fractures on cell boundaries
    const int32 Oct2D = FMath::Clamp(FMath::Min((int32)SC.ShapeOctaves, 2), 1, Config.Performance.MaxNoiseOctaves);
    
    // FIX: Use 3D noise (absolute Z) for smaller shards to break the continuous 
    // vertical columnar extrusion projections, forming organic 3D boulders.
    const float ShapeZ  = (Cache.ShardT < 0.5f) ? WZ * Cache.Freq : 0.f;
    const float ShapeXY = FBM(WX * Cache.Freq, WY * Cache.Freq, ShapeZ, Oct2D, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);

    const float Shape = ShapeXY + ShapeDetail;

    float RootDensity = 0.f;
    if (SC.bEnableHangingRoots && tCenter < -0.25f) {
      const float RootZNorm = FMath::Clamp((-tCenter - 0.25f) / 0.75f, 0.f, 1.f);
      const float RootNoise = FMath::Max(0.f, FBM(WX * SC.RootFrequency, WY * SC.RootFrequency, WZ * SC.RootFrequency, 2, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves));
      RootDensity = RootNoise * (1.f - RootZNorm) * 0.4f * Falloff;
    }

    const float HorizStrength = FMath::SmoothStep(Cache.Threshold, Cache.Threshold + 0.4f, Shape);
    float D = HorizStrength * Falloff * 2.5f - (1.f - Falloff) * 1.8f + RootDensity;

    // BREAKUP STRENGTH FIX for shards:
    // Old: BreakUpStrength based only on HeightNorm → shards get 0.50, which strips
    //      material from all sides of a thin shape → leaves thin spike tips (pillar artifact).
    // New: Shards (ShardT=0) get minimal breakup (0.10) — they are rocks with irregular
    //      surface from 3D noise, not eroded islands. The ShardT lerp means only high-terrain
    //      islands get the full HeightNorm-scaled breakup for their organic eroded look.
    const float MaxBreakUp     = FMath::Lerp(0.50f, 2.80f, Cache.HeightNorm);
    const float BreakUpStrength = FMath::Lerp(0.10f, MaxBreakUp, Cache.ShardT);
    const float BreakUp = FMath::Max(0.f, FastNoise3D(WX_base * 0.002f, WY_base * 0.002f, WZ * 0.001f)) * BreakUpStrength;
    
    // FIX: Mask breakup on island tops to protect flat plates from forming vertical swiss-cheese holes.
    float PlateauMask = 1.0f;
    if (Cache.ShardT > 0.5f && tCenter > 0.0f) {
        // Safe taper range: fully protects core top center (tCenter -> 1.0) 
        PlateauMask = FMath::SmoothStep(0.15f, 0.45f, 1.0f - tCenter);
    }
    D -= BreakUp * PlateauMask;

    return FMath::Clamp(D, -2.f, 2.f);
}

// ============================================================
//  CRYSTAL CAVERNS - deep underground carved chambers
// ============================================================
float FVoxelBiomeGenerators::GetCrystalCavernDelta(
    float X, float Y, float Z, float SurfaceHeight,
    const FVoxelGenerationConfig &Config) {
  const FCrystalCavernsConfig &CVC = Config.CaveCrystals;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y, nZ = Z + Off.Z;

  const float CavernCeiling = SurfaceHeight - CVC.DepthStart;
  if (Z > CavernCeiling)
    return 0.f;

  // --- ⚡ Optimization: Floor gate avoids running 3D noise for endless depths
  // ---
  const float DepthEndLimit =
      CVC.FadeDepth + 8000.f; // Max thickness of cavern layer list
  if (Z < CavernCeiling - DepthEndLimit)
    return 0.f;

  const float Fade =
      FMath::Clamp((CavernCeiling - Z) / CVC.FadeDepth, 0.f, 1.f);

  // Enforce minimum frequency so chamber pattern varies within a chunk;
  // prevents one chamber from filling an entire chunk and creating a single
  // void.
  const float MinChamberFreq = 0.00005f;
  const float CF = FMath::Max(CVC.ChamberFrequency, MinChamberFreq);

  const float Ch1 = FMath::Abs(FBM(nX * CF, nY * CF, nZ * CF, 4, 2.0f, 0.5f,
                                   Config.Performance.MaxNoiseOctaves));
  const float Ch2 =
      FMath::Abs(FBM(nX * CF * 0.7f, nY * CF * 0.7f, nZ * CF + 5678.f, 3, 2.1f,
                     0.5f, Config.Performance.MaxNoiseOctaves));

  float CarveFactor =
      FMath::Max(0.f, CVC.ChamberThreshold - FMath::Min(Ch1, Ch2)) *
      CVC.ChamberStrength;

  float Veins = 0.f;
  if (CVC.bEnableConnectingVeins) {
    const float VeinNoise =
        FBM(nX * CF * 2.5f, nY * CF * 2.5f, nZ * CF * 2.5f, 2, 2.0f, 0.5f,
            Config.Performance.MaxNoiseOctaves);
    Veins = FMath::Pow(FMath::Max(0.f, 1.f - FMath::Abs(VeinNoise)),
                       CVC.VeinPower) *
            CVC.VeinStrength;
  }

  CarveFactor = FMath::Clamp(CarveFactor + Veins, 0.f, 1.5f);

  const float Detail = FastNoise3D(nX * CVC.CrystalDetailFrequency,
                                   nY * CVC.CrystalDetailFrequency,
                                   nZ * CVC.CrystalDetailFrequency);
  const float CrystalFill =
      FMath::Max(0.f, Detail - CVC.CrystalThreshold) * CVC.CrystalAmplitude;

  // --- 💎 CRYSTAL PLACEMENT FIX ---
  // Scale CrystalFill by CarveFactor so geometry only forms inside the chamber 
  // without exceeding the carved magnitude threshold to seal the wall plates.
  const float NetDelta = -(CarveFactor * 1.3f) + (CrystalFill * FMath::Clamp(CarveFactor, 0.f, 1.f));
  return NetDelta * Fade;
}
