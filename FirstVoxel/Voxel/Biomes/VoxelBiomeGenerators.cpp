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
// ============================================================
float FVoxelBiomeGenerators::GetPeaksHeight(
    float X, float Y, const FVoxelGenerationConfig &Config) {
  const FPeaksBiomeConfig &PC = Config.Peaks;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y;

  const float WF = 0.0002f;
  const float WarpX = FastNoise3D(nX * WF, nY * WF, 0.f) * 2000.f;
  const float WarpY = FastNoise3D(nX * WF, nY * WF, 100.f) * 2000.f;

  float Base =
      FBM((nX + WarpX) * PC.NoiseFrequency, (nY + WarpY) * PC.NoiseFrequency,
          10.f, PC.Octaves, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);

  float Normalized = (Base + 1.f) * 0.5f;
  float Shaped = FMath::Pow(FMath::Max(0.f, Normalized), PC.Sharpness);

  float Detail = FastNoise3D(nX * PC.NoiseFrequency * 4.f,
                             nY * PC.NoiseFrequency * 4.f, 0.f) *
                 PC.DetailAmplitude;

  const float BonusHeight = Shaped * 8000.f;
  return Config.SeaLevel + FMath::Lerp(PC.HeightMin, PC.HeightMax, Shaped) +
         BonusHeight + Detail;
}

// ============================================================
//  CLIFFS - ridged, terraced terrain
// ============================================================
float FVoxelBiomeGenerators::GetCliffsHeight(
    float X, float Y, const FVoxelGenerationConfig &Config) {
  const FCliffsBiomeConfig &CC = Config.Cliffs;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y;

  const float WF = 0.00015f;
  const float WarpX = FastNoise3D(nX * WF, nY * WF, 10.f) * 1500.f;
  const float WarpY = FastNoise3D(nX * WF, nY * WF, 110.f) * 1500.f;

  float Base =
      FBM((nX + WarpX) * CC.NoiseFrequency, (nY + WarpY) * CC.NoiseFrequency,
          15.f, CC.Octaves, 2.1f, 0.55f, Config.Performance.MaxNoiseOctaves);

  float Ridge = 1.f - FMath::Abs(Base);
  Ridge = FMath::Pow(FMath::Max(0.f, Ridge), CC.Sharpness);

  const float StepScale = (float)CC.TerraceSteps;
  const float Terrace = FMath::Floor(Ridge * StepScale) / StepScale;
  Ridge = FMath::Lerp(Ridge, Terrace, CC.TerraceFactor);

  float Detail = FastNoise3D(nX * CC.NoiseFrequency * 8.f,
                             nY * CC.NoiseFrequency * 8.f, 0.f) *
                 CC.DetailAmplitude;

  return Config.SeaLevel + FMath::Lerp(CC.HeightMin, CC.HeightMax, Ridge) +
         Detail;
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
// ============================================================
float FVoxelBiomeGenerators::GetCraterHeight(
    float X, float Y, const FVoxelGenerationConfig &Config) {
  const FCraterBiomeConfig &CRC = Config.Craters;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y;
  // ----- Center Canyon Force Overlay -----
  const float DistFrom0 = FMath::Sqrt(X * X + Y * Y);
  const float CenterCanyonRadius = 15000.f; // 150m starting basin
  const float CenterCanyonStr =
      FMath::Clamp(1.f - DistFrom0 / CenterCanyonRadius, 0.f, 1.f);

  float Impact = FastNoise3D(nX * (CRC.Frequency * 0.5f),
                             nY * (CRC.Frequency * 0.5f), 200.f);

  // Force Impact towards -1.0 at (0,0) center to carve a deep basin
  Impact = FMath::Lerp(Impact, -1.0f, CenterCanyonStr);

  const float BasePlains = Config.SeaLevel + 1000.f;
  // const float Impact     = FastNoise3D(nX * (CRC.Frequency * 0.5f), nY *
  // (CRC.Frequency * 0.5f), 200.f);

  if (Impact > CRC.ImpactThreshold) {
    return BasePlains + FastNoise3D(nX * 0.001f, nY * 0.001f, 0.f) * 200.f;
  }

  float NormalizedDepth =
      (FMath::Abs(Impact) - FMath::Abs(CRC.ImpactThreshold)) /
      (1.f - FMath::Abs(CRC.ImpactThreshold));
  NormalizedDepth = FMath::Max(
      0.f, NormalizedDepth); // Clamp to prevent negative Lerp divergences
  const float BottomDepth = BasePlains + FMath::Min(0.f, CRC.Depth) * 2.5f;
  const float RimHeight = BasePlains + CRC.RimHeight * 1.2f;
  const float RimNoise =
      FastNoise3D(nX * 0.008f, nY * 0.008f, 0.f) * CRC.RimNoiseAmplitude;

  float Height;
  if (NormalizedDepth > 0.40f) {
    Height = BottomDepth +
             FastNoise3D(nX * 0.01f, nY * 0.01f, 0.f) * CRC.FloorNoiseAmplitude;
  } else if (NormalizedDepth > 0.25f) {
    float t = (NormalizedDepth - 0.25f) / 0.25f;
    const float Terrace = FMath::Floor(t * 5.0f) / 5.0f;
    t = FMath::Lerp(t, Terrace, 0.75f);
    Height = FMath::Lerp(RimHeight + RimNoise, BottomDepth, t);
  } else {
    float t = NormalizedDepth / 0.25f;
    const float Terrace = FMath::Floor(t * 4.0f) / 4.0f;
    t = FMath::Lerp(t, Terrace, 0.75f);
    Height = FMath::Lerp(BasePlains, RimHeight + RimNoise, t);
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

    const float GridSize = SC.BaseIslandSize * 4.0f; 
    if (GridSize <= 0.f) return Cache;

    const int32 CellX = FMath::FloorToInt(X / GridSize);
    const int32 CellY = FMath::FloorToInt(Y / GridSize);

    float BestDistSq = 99999999.f;
    FVector2D BestCenter(0.f, 0.f);

    for (int32 dx = -1; dx <= 1; ++dx) {
      for (int32 dy = -1; dy <= 1; ++dy) {
        const int32 currentCellX = CellX + dx;
        const int32 currentCellY = CellY + dy;

        const float nX = (float)currentCellX * GridSize + Off.X;
        const float nY = (float)currentCellY * GridSize + Off.Y;

        const float HashX = (FastNoise3D(nX * 0.001f, nY * 0.001f, 0.f) + 1.f) * 0.5f; 
        const float HashY = (FastNoise3D(nX * 0.001f, nY * 0.001f, 100.f) + 1.f) * 0.5f;

        const float CenterX = (currentCellX + 0.12f + HashX * 0.76f) * GridSize;
        const float CenterY = (currentCellY + 0.12f + HashY * 0.76f) * GridSize;

        float DistSq = FMath::Square(X - CenterX) + FMath::Square(Y - CenterY);
        if (DistSq < BestDistSq) {
          BestDistSq = DistSq;
          BestCenter = FVector2D(CenterX, CenterY);
        }
      }
    }

    const float cnX = BestCenter.X + Off.X;
    const float cnY = BestCenter.Y + Off.Y;

    const FVoxelBiomeWeightMap CenterWeights = FVoxelBiomeManager::GetBiomeWeightsStatic(BestCenter.X, BestCenter.Y, Config);
    const float CenterHeight = FVoxelBiomeManager::GetSurfaceHeightStatic(BestCenter.X, BestCenter.Y, CenterWeights, Config);

    Cache.HeightNorm = FMath::Clamp(CenterHeight / SC.MaxTerrainReference, 0.f, 1.f);
    const float RoughnessNorm = FMath::Clamp(CenterWeights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);

    const float CurvedHeight = FMath::Pow(Cache.HeightNorm, 2.5f);
    const float CurvedRough  = FMath::Pow(RoughnessNorm, 2.0f);
    const float TerrainStrength = FMath::Clamp(Cache.HeightNorm * 1.5f + RoughnessNorm * 0.8f, 0.f, 1.f);
    Cache.ShardFalloff = FMath::Pow(TerrainStrength, 2.2f);

    if (Cache.ShardFalloff < 0.08f) return Cache; 

    const float HashProb = (FastNoise3D(cnX * 0.002f, cnY * 0.002f, 200.f) + 1.f) * 0.5f; 

    Cache.Prob = FMath::Clamp(SC.BaseProbability + CurvedHeight * SC.HeightProbabilityBonus + CurvedRough * SC.RoughnessProbabilityBonus, 0.02f, 1.f);
    Cache.Prob *= FMath::Lerp(0.15f, 1.0f, Cache.ShardFalloff);
    if (HashProb > Cache.Prob) return Cache; 

    const float SizeNoise  = FBM(cnX * 0.00008f, cnY * 0.00008f, 50.f, 2, 2.0f, 0.5f, 2);
    const float SizeFactor = (SizeNoise + 1.f) * 0.5f; 

    float IslandSize = SC.BaseIslandSize + CurvedHeight * SC.HeightSizeBonus + CurvedRough * SC.RoughnessSizeBonus;
    IslandSize *= (0.5f + 0.5f * SizeFactor);
    IslandSize *= FMath::Lerp(0.10f, 1.15f, Cache.ShardFalloff);
    IslandSize = FMath::Max(IslandSize, 400.f);

    const float MaxIslandSizeForAltitude = FMath::Lerp(20000.f, 8000.f, Cache.HeightNorm);
    IslandSize = FMath::Min(IslandSize, MaxIslandSizeForAltitude);

    const float AbsoluteMaxRadius = GridSize * 0.48f; 
    IslandSize = FMath::Min(IslandSize, AbsoluteMaxRadius);

    const float Dist = FMath::Sqrt(BestDistSq);
    if (Dist > IslandSize) return Cache; 

    const float AltitudeBase = FMath::Lerp(SC.MinAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, TerrainStrength);
    Cache.SkyAlt = CenterHeight + AltitudeBase + CurvedHeight * SC.HeightAltitudeBonus + CurvedRough * SC.RoughnessAltitudeBonus + Cache.ShardFalloff * SC.LowTerrainAltitudeBoost;
    Cache.HalfThick = IslandSize * SC.ThicknessRatio;

    Cache.Threshold = FMath::Lerp(SC.ThresholdAtMinProbability, SC.ThresholdAtMaxProbability, Cache.Prob);
    Cache.WX_base = X + Off.X;
    Cache.WY_base = Y + Off.Y;

    Cache.WX = Cache.WX_base;
    Cache.WY = Cache.WY_base;
    if (SC.bEnableDomainWarping) {
      const float WF = SC.DomainWarpFrequency;
      Cache.WX += FastNoise3D(Cache.WX * WF + 10.f, Cache.WY * WF + 20.f, 0.f) * SC.DomainWarpStrength;
      Cache.WY += FastNoise3D(Cache.WX * WF + 50.f, Cache.WY * WF + 10.f, 0.f) * SC.DomainWarpStrength;
    }

    const float SizeRatio = FMath::Max(1.f, (float)(IslandSize / SC.BaseIslandSize));
    Cache.Freq = SC.ShapeFrequency / FMath::Sqrt(SizeRatio);
    Cache.Freq = FMath::Max(Cache.Freq, 0.00025f);

    const int32 Oct2D = FMath::Clamp(FMath::Min((int32)SC.ShapeOctaves, 2), 1, Config.Performance.MaxNoiseOctaves);
    Cache.ShapeXY = FBM(Cache.WX * Cache.Freq, Cache.WY * Cache.Freq, 0.f, Oct2D, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);

    if (Cache.ShapeXY <= Cache.Threshold)
      return Cache;

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
    const float BreakUp = FastNoise3D(WX_base * 0.002f, WY_base * 0.002f, WZ * 0.001f) * BreakUpStrength;
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
  const float NetDelta = -(CarveFactor * 1.3f) + (CarveFactor > 0.15f ? CrystalFill * 0.8f : 0.f);

  return NetDelta * Fade;
}
