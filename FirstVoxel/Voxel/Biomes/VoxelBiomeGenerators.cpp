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
  const float WarpX = FastNoise3D(nX * WF, nY * WF, 0.f) * 1000.f;  // was 2000
  const float WarpY = FastNoise3D(nX * WF, nY * WF, 100.f) * 1000.f; // was 2000

  float Base = FBM((nX + WarpX) * PC.NoiseFrequency,
                   (nY + WarpY) * PC.NoiseFrequency,
                   10.f, PC.Octaves, 2.0f, 0.5f,
                   Config.Performance.MaxNoiseOctaves);

  float Normalized = (Base + 1.f) * 0.5f;
  float Shaped = FMath::Pow(FMath::Clamp(Normalized, 0.f, 1.f), PC.Sharpness);
  Shaped = FMath::Clamp(Shaped, 0.f, 1.f); // guarantee no overshoot into HeightMax

  // Detail noise capped relative to height range so it can’t spike beyond HeightMax.
  const float MaxDetail = (PC.HeightMax - PC.HeightMin) * 0.02f; // 2% of range
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

  // Moderate domain warp for organic cliff curvature (reduced from 1500 to 800
  // so warp doesn't create micro-fold spikes at warp boundaries).
  const float WF = 0.00015f;
  const float WarpX = FastNoise3D(nX * WF, nY * WF, 10.f) * 800.f;
  const float WarpY = FastNoise3D(nX * WF, nY * WF, 110.f) * 800.f;

  float Base = FBM((nX + WarpX) * CC.NoiseFrequency,
                   (nY + WarpY) * CC.NoiseFrequency,
                   15.f, CC.Octaves, 2.1f, 0.55f,
                   Config.Performance.MaxNoiseOctaves);

  // Ridged noise: (1 - Abs(FBM)) creates sharp ridgelines.
  // Clamped to [0,1] and raised to Sharpness (1.8) for defined cliff faces
  // without the extreme spikes that the old 3.6 power caused.
  float Ridge = 1.f - FMath::Abs(Base);
  Ridge = FMath::Clamp(Ridge, 0.f, 1.f);
  float Shaped = FMath::Pow(Ridge, CC.Sharpness); // Sharpness=1.8 is safe

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
  const float EdgeBlend =
      FMath::SmoothStep(0.f, 1.f, (Base - Plateau) * MC.EdgeSharpness);
  const float Shape = Plateau + (EdgeBlend / StepScale);

  const float Normalized = (Shape + 1.f) * 0.5f;
  return Config.SeaLevel + MC.HeightBase +
         Normalized * (MC.HeightMax - MC.HeightBase);
}

// ============================================================
//  CRATERS - impact basins with raised rims
//  FIXED: Improved crater generation with better noise blending and detail
// ============================================================
float FVoxelBiomeGenerators::GetCraterHeight(
    float X, float Y, const FVoxelGenerationConfig &Config) {
  const FCraterBiomeConfig &CRC = Config.Craters;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y;
  
  // Domain warp for organic crater walls with shape distortion
  const float CenterWarp = FastNoise3D(nX * 0.004f, nY * 0.004f, 100.f) * 0.25f;
  
  // Shape distortion for irregular crater borders
  const float ShapeDistortion = FastNoise3D(nX * 0.006f, nY * 0.006f, 400.f) * CRC.ShapeDistortion;
  
  // Border irregularity for non-perfect circular craters
  const float BorderNoise = FastNoise3D(nX * 0.003f, nY * 0.003f, 500.f) * CRC.BorderIrregularity;

  // Crater size multiplier for larger craters
  const float ModifiedFrequency = CRC.Frequency * CRC.CraterSizeMultiplier;

  float Impact = FastNoise3D(nX * (ModifiedFrequency * 0.5f),
                             nY * (ModifiedFrequency * 0.5f), 200.f);
  Impact += CenterWarp;
  Impact += ShapeDistortion;
  Impact += BorderNoise;

  // Secondary noise layer for crater complexity
  const float SecondaryNoise = FastNoise3D(nX * 0.002f, nY * 0.002f, 300.f) * 0.3f;
  Impact += SecondaryNoise;

  const float BasePlains = Config.SeaLevel + 1000.f;

  if (Impact > CRC.ImpactThreshold)
  {
    // Outside any crater basin: return the flat plains height so the weighted
    // blend evaluates to plains * CratersW + otherBiome * (1-CratersW).
    // Returning 0 caused the other biomes' heights to be down-weighted,
    // creating a subtle depression at crater boundaries that looked like a blob.
    return BasePlains;
  }

  const float Denominator = 1.f - FMath::Abs(CRC.ImpactThreshold);
  float NormalizedDepth = 0.f;
  if (Denominator > 0.001f) {
    NormalizedDepth = (FMath::Abs(Impact) - FMath::Abs(CRC.ImpactThreshold)) / Denominator;
  }
  NormalizedDepth = FMath::Clamp(NormalizedDepth, 0.f, 1.f);
  
    // FIXED: Better depth calculation with exponential curve and increased multiplier for deeper craters
    const float DepthCurve = FMath::Pow(NormalizedDepth, 1.5f);
    const float BottomDepth = BasePlains + FMath::Min(0.f, CRC.Depth) * 4.5f * DepthCurve;
  
  // FIXED: Better rim calculation with noise detail
  const float RimHeight = BasePlains + CRC.RimHeight * 1.0f;
  const float RimNoise = FastNoise3D(nX * 0.008f, nY * 0.008f, 0.f) * CRC.RimNoiseAmplitude * 0.8f;

  float Height = BasePlains;
  
  // FIXED: Smoother transitions with better blending and building-friendly noise
  if (NormalizedDepth > 0.45f) {
    // Deep crater floor with organic building-friendly noise
    const float BuildingNoise = FastNoise3D(nX * CRC.BuildingNoiseFrequency, nY * CRC.BuildingNoiseFrequency, 0.f) * CRC.BuildingNoiseAmplitude;
    Height = BottomDepth + BuildingNoise;
  } else if (NormalizedDepth > 0.30f) {
    // Crater slope with gentler terracing for building
    float t = (NormalizedDepth - 0.30f) / 0.15f;
    const float Terrace = FMath::Floor(t * 4.0f) / 4.0f; // Reduced terrace steps for gentler slopes
    t = FMath::Lerp(t, Terrace, 0.4f); // Reduced terrace strength for smoother building surfaces
    Height = FMath::Lerp(RimHeight + RimNoise, BottomDepth, t);
  } else if (NormalizedDepth > 0.15f) {
    // Rim area with noise for organic look
    float t = (NormalizedDepth - 0.15f) / 0.15f;
    Height = FMath::Lerp(BasePlains, RimHeight + RimNoise, t);
  } else {
    // Transition zone to plains with gentle slope
    float t = NormalizedDepth / 0.15f;
    Height = FMath::Lerp(BasePlains, RimHeight + RimNoise * 0.3f, t);
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

    float SumAlt           = 0.f;
    float SumThick         = 0.f;
    float SumThresh        = 0.f;
    float SumHeightNorm    = 0.f;
    float SumShardFalloff  = 0.f;
    float SumIslandSize    = 0.f;
    float SumWeight        = 0.f;

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
        const float CenterHeight = FVoxelBiomeManager::GetSurfaceHeightStatic(CenterX, CenterY, CenterWeights, Config);

        const float HeightNorm    = FMath::Clamp(CenterHeight / SC.MaxTerrainReference, 0.f, 1.f);
        const float RoughnessNorm = FMath::Clamp(CenterWeights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);

        const float CurvedHeight  = FMath::Pow(HeightNorm, 2.5f);
        const float CurvedRough   = FMath::Pow(RoughnessNorm, 2.0f);
        const float TerrainStr    = FMath::Clamp(HeightNorm * 1.5f + RoughnessNorm * 0.8f, 0.f, 1.f);
        const float ShardFalloff  = FMath::Pow(TerrainStr, 2.2f);

        // CellShardT: same altitude ramp but per-cell so size/thickness are
        // evaluated against the cell's own terrain, not the query column.
        const float CellShardT = FMath::SmoothStep(0.0f, SC.ShardTransitionStrength, TerrainStr);

        // Minimum ShardFalloff gate: skip cells with truly zero terrain strength.
        if (ShardFalloff < 0.008f) continue;

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
        float Prob = FMath::Clamp(
            SC.BaseProbability
            + CurvedHeight * SC.HeightProbabilityBonus
            + CurvedRough  * SC.RoughnessProbabilityBonus,
            0.02f, 1.f);
        Prob *= FMath::Lerp(0.35f, 1.0f, ShardFalloff);
        if (HashProb > Prob) continue;

        // -------------------------------------------------------------------
        //  SIZE: aggressive altitude falloff
        //  High island:   BaseIslandSize + HeightSizeBonus + RoughnessSizeBonus
        //  Low shard:     BaseIslandSize * ShardMinScale  (very small)
        //
        //  ShardMinScale from config (default 0.08 = 8% of BaseIslandSize = ~200cm radius).
        // -------------------------------------------------------------------
        const float ShardMinScale = SC.ShardMinScale;
        const float SizeNoise   = FBM(cnX * 0.00008f, cnY * 0.00008f, 50.f, 2, 2.0f, 0.5f, 2);
        const float SizeFactor  = (SizeNoise + 1.f) * 0.5f;

        // Base size at this altitude: lerp from tiny shard to full island.
        float IslandSize = FMath::Lerp(
            SC.BaseIslandSize * ShardMinScale,
            SC.BaseIslandSize + CurvedHeight * SC.HeightSizeBonus + CurvedRough * SC.RoughnessSizeBonus,
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
        const float SkyAlt = CenterHeight + AltitudeBase
            + CurvedHeight * SC.HeightAltitudeBonus
            + CurvedRough  * SC.RoughnessAltitudeBonus
            + ShardFalloff * SC.LowTerrainAltitudeBoost;

        // -------------------------------------------------------------------
        //  THICKNESS: shards are flat discs, islands are chunky
        //  Low shard ThicknessRatio: 0.10  (very thin, disc-like)
        //  High island ThicknessRatio: SC.ThicknessRatio (0.48 default)
        // -------------------------------------------------------------------
        const float EffThickness = FMath::Lerp(0.10f, SC.ThicknessRatio, CellShardT);
        const float HalfThick    = IslandSize * EffThickness;

        // Shape noise threshold: shards use a higher threshold so only the
        // core of the noise field is solid — making them jagged and irregular.
        // Islands use a lower threshold for solid, smooth interiors.
        const float ShardThresholdBoost = FMath::Lerp(0.20f, 0.0f, CellShardT);
        const float Threshold = FMath::Lerp(SC.ThresholdAtMinProbability, SC.ThresholdAtMaxProbability, Prob)
                                + ShardThresholdBoost;

        // Continuous blend weight
        const float W = FMath::Square(1.f - (Dist / IslandSize));
        SumAlt          += SkyAlt    * W;
        SumThick        += HalfThick * W;
        SumThresh       += Threshold * W;
        SumHeightNorm   += HeightNorm * W;
        SumShardFalloff += ShardFalloff * W;
        SumIslandSize   += IslandSize * W;
        SumWeight       += W;
    }

    if (SumWeight <= 0.f) return Cache;

    Cache.SkyAlt       = SumAlt       / SumWeight;
    Cache.HalfThick    = SumThick     / SumWeight;
    Cache.Threshold    = SumThresh    / SumWeight;
    Cache.HeightNorm   = SumHeightNorm / SumWeight;
    Cache.ShardFalloff = SumShardFalloff / SumWeight;

    // Freq: shards need much higher frequency noise to look jagged.
    // Low shards: ShapeFrequency * 6  (high-freq = rough, spiky silhouette)
    // High islands: ShapeFrequency / sqrt(SizeRatio)  (smooth, organic)
    const float BlendedIslandSize = SumIslandSize / SumWeight;
    const float SizeRatio         = FMath::Max(1.f, BlendedIslandSize / SC.BaseIslandSize);
    const float IslandFreq        = SC.ShapeFrequency / FMath::Sqrt(SizeRatio);
    const float ShardFreq         = SC.ShapeFrequency * 6.0f;
    Cache.Freq = FMath::Lerp(ShardFreq, IslandFreq, ShardT);
    Cache.Freq = FMath::Max(Cache.Freq, 0.00025f);

    Cache.Prob    = 0.5f;
    Cache.WX_base = X + Off.X;
    Cache.WY_base = Y + Off.Y;
    Cache.WX      = Cache.WX_base;
    Cache.WY      = Cache.WY_base;
    Cache.bHasSkyland = true;
    return Cache;
}

float FVoxelBiomeGenerators::GetSkylandDensityFromCache(
    const FSkylandColumnCache& Cache, float X, float Y, float Z,
    const FVoxelGenerationConfig& Config, int32 StepSize)
{
    if (!Cache.bHasSkyland) return -2.f;

    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
    const FVector Off = Config.GetSeedOffset();

    const float MinHalfThick = FMath::Max(200.f, (float)(StepSize * 55.f));
    const float HalfThick = FMath::Max(MinHalfThick, Cache.HalfThick);

    const float Margin = HalfThick * 0.4f;
    if (Z < Cache.SkyAlt - HalfThick - Margin || Z > Cache.SkyAlt + HalfThick + Margin) return -2.f;

    const float tCenter = FMath::Clamp((Z - Cache.SkyAlt) / (HalfThick + 1.f), -1.f, 1.f);

    float Falloff;
    if (tCenter >= 0.f) {
      const float FlatZone = 0.35f;
      if (tCenter < FlatZone) {
        Falloff = 1.0f;
      } else {
        const float nt = (tCenter - FlatZone) / (1.f - FlatZone);
        Falloff = FMath::SmoothStep(0.f, 1.f, 1.f - nt);
      }
    } else {
      const float t = FMath::Clamp(-tCenter, 0.f, 1.f);
      Falloff = FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(t, 0.85f));
    }

    const float WX_base = X + Off.X;
    const float WY_base = Y + Off.Y;
    const float WZ = Z + Off.Z;

    float WX = WX_base;
    float WY = WY_base;

    if (SC.bEnableDomainWarping) {
      const float WF = SC.DomainWarpFrequency;
      WX += FastNoise3D(WX * WF + 10.f, WY * WF + 20.f, 0.f) * SC.DomainWarpStrength;
      WY += FastNoise3D(WX * WF + 50.f, WY * WF + 10.f, 0.f) * SC.DomainWarpStrength;
    }

    float ShapeDetail = 0.f;
    if (Config.Performance.bEnable3DSkylandNoise) {
      ShapeDetail = FastNoise3D(WX * Cache.Freq * 0.6f, WY * Cache.Freq * 0.6f, WZ * Cache.Freq * 0.05f) * 0.25f;
    }

    // Point-wise ShapeXY prevents absolute grid-cell fractures on cell boundaries
    const int32 Oct2D = FMath::Clamp(FMath::Min((int32)SC.ShapeOctaves, 2), 1, Config.Performance.MaxNoiseOctaves);
    const float ShapeXY = FBM(WX * Cache.Freq, WY * Cache.Freq, 0.f, Oct2D, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);

    const float Shape = ShapeXY + ShapeDetail;

    float RootDensity = 0.f;
    if (SC.bEnableHangingRoots && tCenter < -0.25f) {
      const float RootZNorm = FMath::Clamp((-tCenter - 0.25f) / 0.75f, 0.f, 1.f);
      const float RootNoise = FMath::Max(0.f, FBM(WX * SC.RootFrequency, WY * SC.RootFrequency, WZ * SC.RootFrequency, 2, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves));
      RootDensity = RootNoise * (1.f - RootZNorm) * 0.4f * Falloff;
    }

    const float HorizStrength = FMath::SmoothStep(Cache.Threshold, Cache.Threshold + 0.4f, Shape);
    float D = HorizStrength * Falloff * 2.5f - (1.f - Falloff) * 1.8f + RootDensity;

    const float BreakUpStrength = FMath::Lerp(0.50f, 2.80f, Cache.HeightNorm);
    const float BreakUp = FMath::Max(0.f, FastNoise3D(WX_base * 0.002f, WY_base * 0.002f, WZ * 0.001f)) * BreakUpStrength;
    D -= BreakUp;

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
  // Crystals should ONLY spawn inside already hollowed chambers to prevent
  // canceling out the carver and creating solid wall plates.
  const float NetDelta = -(CarveFactor * 1.3f); // Scrapped CrystalFill stalagmites
  return NetDelta * Fade;
}
