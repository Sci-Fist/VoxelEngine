// VoxelBiomeGenerators.cpp
// Shape-function implementations for all surface biomes and the skylands / crystal cavern layers.

#include "VoxelBiomeGenerators.h"
#include "Voxel/Config/VoxelGenerationConfig.h"

// ============================================================
//  FBM
// ============================================================
float FVoxelBiomeGenerators::FBM(float X, float Y, float Z, int32 Octaves, float Lacunarity, float Gain, int32 MaxOctaves)
{
    const int32 ActualOctaves = FMath::Clamp(FMath::Min(Octaves, MaxOctaves), 1, 16);
    float Value = 0.f, Amp = 0.5f, Freq = 1.f;
    for (int32 i = 0; i < ActualOctaves; ++i)
    {
        Value += FastNoise3D(X * Freq, Y * Freq, Z * Freq) * Amp;
        Freq  *= Lacunarity;
        Amp   *= Gain;
    }
    return Value;
}

// ============================================================
//  FOREST - rolling hills and plains
// ============================================================
float FVoxelBiomeGenerators::GetForestHeight(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FForestBiomeConfig& FC = Config.Forest;
    const FVector Off = Config.GetSeedOffset();
    const float nX = X + Off.X, nY = Y + Off.Y;

    float Base = FBM(nX * FC.NoiseFrequency, nY * FC.NoiseFrequency, 0.f,
                     FC.Octaves, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);

    float Normalized = (Base + 1.f) * 0.5f;
    float Detail = FastNoise3D(nX * FC.DetailFrequency, nY * FC.DetailFrequency, 0.f) * FC.DetailAmplitude;

    return Config.SeaLevel + FMath::Lerp(FC.HeightMin, FC.HeightMax, Normalized) + Detail;
}

// ============================================================
//  DESERT - rolling sand dunes
// ============================================================
float FVoxelBiomeGenerators::GetDesertHeight(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FDesertBiomeConfig& DC = Config.Desert;
    const FVector Off = Config.GetSeedOffset();
    const float nX = X + Off.X, nY = Y + Off.Y;

    float Base = FBM(nX * DC.NoiseFrequency, nY * DC.NoiseFrequency, 40.f,
                     DC.Octaves, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);

    float Normalized = (Base + 1.f) * 0.5f;
    float Shaped = FMath::Pow(FMath::Max(0.f, Normalized), DC.Sharpness);

    float Detail = FastNoise3D(nX * DC.RippleFrequency, nY * DC.RippleFrequency, 0.f) * DC.RippleAmplitude;

    return Config.SeaLevel + FMath::Lerp(DC.HeightMin, DC.HeightMax, Shaped) + Detail;
}

// ============================================================
//  PEAKS - dramatic alpine mountains
// ============================================================
float FVoxelBiomeGenerators::GetPeaksHeight(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FPeaksBiomeConfig& PC = Config.Peaks;
    const FVector Off = Config.GetSeedOffset();
    const float nX = X + Off.X, nY = Y + Off.Y;

    const float WF = 0.0002f;
    const float WarpX = FastNoise3D(nX * WF, nY * WF, 0.f) * 2000.f;
    const float WarpY = FastNoise3D(nX * WF, nY * WF, 100.f) * 2000.f;

    float Base = FBM((nX + WarpX) * PC.NoiseFrequency, (nY + WarpY) * PC.NoiseFrequency, 10.f,
                     PC.Octaves, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);

    float Normalized = (Base + 1.f) * 0.5f;
    float Shaped = FMath::Pow(FMath::Max(0.f, Normalized), PC.Sharpness);

    float Detail = FastNoise3D(nX * PC.NoiseFrequency * 4.f, nY * PC.NoiseFrequency * 4.f, 0.f) * PC.DetailAmplitude;

    const float BonusHeight = Shaped * 8000.f;
    return Config.SeaLevel + FMath::Lerp(PC.HeightMin, PC.HeightMax, Shaped) + BonusHeight + Detail;
}

// ============================================================
//  CLIFFS - ridged, terraced terrain
// ============================================================
float FVoxelBiomeGenerators::GetCliffsHeight(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FCliffsBiomeConfig& CC = Config.Cliffs;
    const FVector Off = Config.GetSeedOffset();
    const float nX = X + Off.X, nY = Y + Off.Y;

    const float WF = 0.00015f;
    const float WarpX = FastNoise3D(nX * WF, nY * WF, 10.f) * 1500.f;
    const float WarpY = FastNoise3D(nX * WF, nY * WF, 110.f) * 1500.f;

    float Base = FBM((nX + WarpX) * CC.NoiseFrequency, (nY + WarpY) * CC.NoiseFrequency, 15.f,
                     CC.Octaves, 2.1f, 0.55f, Config.Performance.MaxNoiseOctaves);

    float Ridge = 1.f - FMath::Abs(Base);
    Ridge = FMath::Pow(FMath::Max(0.f, Ridge), CC.Sharpness);

    const float StepScale = (float)CC.TerraceSteps;
    const float Terrace   = FMath::Floor(Ridge * StepScale) / StepScale;
    Ridge = FMath::Lerp(Ridge, Terrace, CC.TerraceFactor);

    float Detail = FastNoise3D(nX * CC.NoiseFrequency * 8.f, nY * CC.NoiseFrequency * 8.f, 0.f) * CC.DetailAmplitude;

    return Config.SeaLevel + FMath::Lerp(CC.HeightMin, CC.HeightMax, Ridge) + Detail;
}

// ============================================================
//  MESA - flat-top plateaus
// ============================================================
float FVoxelBiomeGenerators::GetMesaHeight(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FMesaBiomeConfig& MC = Config.Mesa;
    const FVector Off = Config.GetSeedOffset();
    const float nX = X + Off.X, nY = Y + Off.Y;

    const float Freq = 0.0001f;
    float Base = FBM(nX * Freq, nY * Freq, 20.f, 4, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);

    const float StepScale = (float)MC.PlateauSteps;
    const float Plateau   = FMath::Floor(Base * StepScale) / StepScale;
    const float EdgeBlend = FMath::SmoothStep(0.f, 1.f, (Base - Plateau) * MC.EdgeSharpness);
    const float Shape     = Plateau + (EdgeBlend / StepScale);

    const float Normalized = (Shape + 1.f) * 0.5f;
    return Config.SeaLevel + MC.HeightBase + Normalized * (MC.HeightMax - MC.HeightBase);
}

// ============================================================
//  CRATERS - impact basins with raised rims
// ============================================================
float FVoxelBiomeGenerators::GetCraterHeight(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FCraterBiomeConfig& CRC = Config.Craters;
    const FVector Off = Config.GetSeedOffset();
    const float nX = X + Off.X, nY = Y + Off.Y;

    const float BasePlains = Config.SeaLevel + 1000.f;
    const float Impact     = FastNoise3D(nX * CRC.Frequency, nY * CRC.Frequency, 200.f);

    if (Impact > CRC.ImpactThreshold)
    {
        return BasePlains + FastNoise3D(nX * 0.001f, nY * 0.001f, 0.f) * 200.f;
    }

    const float NormalizedDepth = (FMath::Abs(Impact) - FMath::Abs(CRC.ImpactThreshold))
                                  / (1.f - FMath::Abs(CRC.ImpactThreshold));
    const float BottomDepth = BasePlains + CRC.Depth * 2.5f;
    const float RimHeight   = BasePlains + CRC.RimHeight * 1.2f;
    const float RimNoise    = FastNoise3D(nX * 0.008f, nY * 0.008f, 0.f) * CRC.RimNoiseAmplitude;

    float Height;
    if (NormalizedDepth > 0.40f)
    {
        Height = BottomDepth + FastNoise3D(nX * 0.01f, nY * 0.01f, 0.f) * CRC.FloorNoiseAmplitude;
    }
    else if (NormalizedDepth > 0.25f)
    {
        float t = (NormalizedDepth - 0.25f) / 0.25f;
        const float Terrace = FMath::Floor(t * 5.0f) / 5.0f;
        t = FMath::Lerp(t, Terrace, 0.75f);
        Height = FMath::Lerp(RimHeight + RimNoise, BottomDepth, t);
    }
    else
    {
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
//  - Altitude is CONSTANT per island (no per-voxel AltBoost) to prevent shard artifacts
//  - Shape test is primarily 2D (XY) so island interiors are always solid
//  - Minimal Z-variation in noise prevents vertical holes through islands
//  - Mountains -> high altitude, large, frequent islands
//  - Plains    -> low altitude, small, sparse islands (shard rocks)
//
//  Fix history:
//  - Removed AltBoost = SizeFactor * 12000: was causing altitude to vary 120m per-pixel,
//    which made the Z-gate fire inconsistently within a chunk creating vertical black slabs
//  - Reduced Z frequency from 0.4x to 0.05x: prevents Swiss cheese holes
//  - Primary shape test is 2D (early-exit): guarantees solid island interiors
//  - Capped shape octaves at 2 for stability over flat terrain
// ============================================================
float FVoxelBiomeGenerators::GetSkylandDensity(
    float X, float Y, float Z,
    float SurfaceHeight,
    const FVoxelBiomeWeightMap& Weights,
    const FVoxelGenerationConfig& Config)
{
    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;

    // ----- Terrain influence factors -----
    const float HeightNorm    = FMath::Clamp(SurfaceHeight / SC.MaxTerrainReference, 0.f, 1.f);
    const float RoughnessNorm = FMath::Clamp(Weights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);

    // Exponential curves: high terrain boosts islands disproportionately
    const float CurvedHeight = FMath::Pow(HeightNorm,    2.5f);
    const float CurvedRough  = FMath::Pow(RoughnessNorm, 2.0f);

    // Combined terrain strength [0,1]: drives size and visibility over low terrain
    const float TerrainStrength = FMath::Clamp(HeightNorm * 1.5f + RoughnessNorm * 0.8f, 0.f, 1.f);
    // Hard falloff: low terrain should drop islands into tiny shards quickly.
    const float ShardFalloff = FMath::Pow(TerrainStrength, 2.2f);

    const FVector Off = Config.GetSeedOffset();
    const float WX_base = X + Off.X;
    const float WY_base = Y + Off.Y;

    // ----- Island size: use very-low-frequency noise for coherent island clusters -----
    // Low freq (0.00008) means size varies over 12km, keeping all voxels in a chunk at
    // the same approximate size rather than varying per-voxel.
    const float SizeNoise  = FBM(WX_base * 0.00008f, WY_base * 0.00008f, 50.f, 2, 2.0f, 0.5f, 2);
    const float SizeFactor = (SizeNoise + 1.f) * 0.5f; // [0, 1]

    float IslandSize = SC.BaseIslandSize
                     + CurvedHeight * SC.HeightSizeBonus
                     + CurvedRough  * SC.RoughnessSizeBonus;
    // Cluster noise: 50-100% size range (not 30% - avoids tiny invisible islands)
    IslandSize *= (0.5f + 0.5f * SizeFactor);
    // Scale down over flat/low terrain so plains only have tiny rock shards
    IslandSize *= FMath::Lerp(0.10f, 1.15f, ShardFalloff);
    // Minimum useful size: at least 400cm radius so islands are always visible
    IslandSize = FMath::Max(IslandSize, 400.f);

    const float HalfThick = FMath::Max(200.f, IslandSize * SC.ThicknessRatio);

    // ----- Altitude: stable, terrain-driven ONLY -----
    // CRITICAL: altitude must not vary per-voxel, or the Z-gate fires inconsistently
    // within a chunk, creating vertical black slabs of geometry.
    //
    // AltitudeBase lerps from MinAltitudeAboveTerrain (tiny shards skimming flat ground,
    // e.g. 300cm = 3m) up to BaseAltitudeAboveTerrain (large islands high in the sky)
    // proportional to TerrainStrength so low flat terrain gets low-flying shards and
    // mountains get properly elevated islands. Both values are config-exposed.
    const float AltitudeBase = FMath::Lerp(SC.MinAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, TerrainStrength);

    // SkyAlt is purely terrain-driven: flat plains get low-flying shards at MinAltitude,
    // mountains push islands far above their peaks via HeightAltitudeBonus.
    // CRITICAL: must not clamp or offset SkyAlt after this point — any artificial floor
    // causes islands to appear everywhere at a fixed height, defeating the height-based system.
    const float SkyAlt = SurfaceHeight
                       + AltitudeBase
                       + CurvedHeight * SC.HeightAltitudeBonus
                       + CurvedRough  * SC.RoughnessAltitudeBonus
                       + ShardFalloff * SC.LowTerrainAltitudeBoost;

    // Early-out: if terrain influence is extremely low, skip skylands entirely.
    // This prevents flat plains from being blanketed by dense skyland noise.
    if (ShardFalloff < 0.08f)
    {
        return -2.f;
    }

    // ----- Z-gate: skip if clearly outside the island band -----
    const float Margin = HalfThick * 0.4f;
    if (Z < SkyAlt - HalfThick - Margin || Z > SkyAlt + HalfThick + Margin) return -2.f;


    // ----- Probability -> threshold -----
    float Prob = FMath::Clamp(
        SC.BaseProbability
        + CurvedHeight * SC.HeightProbabilityBonus
        + CurvedRough  * SC.RoughnessProbabilityBonus,
        0.02f, 1.f);
    // Scale probability by terrain falloff so lowlands don't saturate with islands.
    Prob *= FMath::Lerp(0.15f, 1.0f, ShardFalloff);
    
    // Cap maximum probability to prevent chunk-filling islands
    Prob = FMath::Min(Prob, 0.45f);
    
    // Ensure threshold interpolation is monotonic: high probability should have low threshold
    // This fixes the inverted logic that was causing extreme generation
    const float Threshold = FMath::Lerp(SC.ThresholdAtMaxProbability, SC.ThresholdAtMinProbability, Prob);

    // Domain warping: XY only (no Z warp prevents vertical discontinuities between chunks)
    float WX = WX_base, WY = WY_base;
    if (SC.bEnableDomainWarping)
    {
        const float WF = SC.DomainWarpFrequency;
        WX += FastNoise3D(WX * WF + 10.f, WY * WF + 20.f, 0.f) * SC.DomainWarpStrength;
        WY += FastNoise3D(WX * WF + 50.f, WY * WF + 10.f, 0.f) * SC.DomainWarpStrength;
    }

    // Frequency scales inversely with size
    const float SizeRatio = FMath::Max(1.f, IslandSize / SC.BaseIslandSize);
    float Freq = SC.ShapeFrequency / FMath::Sqrt(SizeRatio);
    // Enforce minimum so island shape varies within a chunk (~32m): prevents entire chunks
    // from becoming one solid skyland block when shape noise is too low-frequency.
    const float MinShapeFreqForChunkVariation = 0.00025f; // ~0.8 period over 3200 cm
    Freq = FMath::Max(Freq, MinShapeFreqForChunkVariation);

    // ----- PRIMARY SHAPE TEST: 2D (XY) only -----
    // A purely 2D test guarantees that any column inside an island region is
    // fully solid - no Z-axis holes that fragment the island into shards.
    // Octaves capped at 2 to avoid high-frequency variation that causes fragmentation.
    const int32 Oct2D = FMath::Clamp(FMath::Min(SC.ShapeOctaves, 2), 1, Config.Performance.MaxNoiseOctaves);
    const float ShapeXY = FBM(WX * Freq, WY * Freq, 0.f, Oct2D, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);

    // Early exit: this XY position is not part of any island
    if (ShapeXY <= Threshold) return -2.f;

    // ----- Vertical profile -----
    // tCenter: 0 = island center, negative = below, positive = above (clamped to [-1, +1])
    const float tCenter = FMath::Clamp((Z - SkyAlt) / (HalfThick + 1.f), -1.f, 1.f);

    float Falloff;
    if (tCenter >= 0.f)
    {
        // Top half: flat table surface, then smooth taper to edge
        const float FlatZone = 0.35f;
        if (tCenter < FlatZone)
        {
            Falloff = 1.0f;
        }
        else
        {
            const float nt = (tCenter - FlatZone) / (1.f - FlatZone);
            Falloff = FMath::SmoothStep(0.f, 1.f, 1.f - nt);
        }

    }
    else
    {
        // Bottom half: stalactite taper (slightly convex for natural rocky bottom)
        const float t = FMath::Clamp(-tCenter, 0.f, 1.f);
        Falloff = FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(t, 0.85f));
    }

    // ----- Optional 3D surface detail (TINY Z contribution to avoid holes) -----
    // Only adds roughness to the island surface, does not create holes in the interior
    float ShapeDetail = 0.f;
    if (Config.Performance.bEnable3DSkylandNoise)
    {
        const float WZ = Z + Off.Z;
        // Z frequency is 0.05x of XY: over 1600cm chunk = 0.08 noise periods,
        // causing only gentle surface undulation, not vertical holes.
        ShapeDetail = FastNoise3D(WX * Freq * 0.6f, WY * Freq * 0.6f, WZ * Freq * 0.05f) * 0.25f;
    }

    const float Shape = ShapeXY + ShapeDetail;

    // ----- Hanging roots (below island center) -----
    float RootDensity = 0.f;
    if (SC.bEnableHangingRoots && tCenter < -0.25f)
    {
        const float WZ = Z + Off.Z;
        const float RootZNorm = FMath::Clamp((-tCenter - 0.25f) / 0.75f, 0.f, 1.f);
        const float RootNoise = FMath::Max(0.f, FBM(
            WX * SC.RootFrequency, WY * SC.RootFrequency, WZ * SC.RootFrequency,
            2, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves));
        // Roots taper from thick at island bottom to thin at tip
        RootDensity = RootNoise * (1.f - RootZNorm) * 0.4f * Falloff;
    }

    // ----- Final density -----
    const float HorizStrength = FMath::SmoothStep(Threshold, Threshold + 0.4f, Shape);

    float D = HorizStrength * Falloff * 2.5f
            - (1.f - Falloff) * 1.8f
            + RootDensity;

    // Break-up term: high-frequency 3D noise so chunks never fill as one solid block.
    // Over ~32m this oscillates several times, turning some voxels to air.
    const float WZ = Z + Off.Z;
    const float BreakUp = FastNoise3D(WX_base * 0.002f, WY_base * 0.002f, WZ * 0.001f) * 0.45f;
    D -= BreakUp;

    return FMath::Clamp(D, -2.f, 2.f);
}

// ============================================================
//  CRYSTAL CAVERNS - deep underground carved chambers
// ============================================================
float FVoxelBiomeGenerators::GetCrystalCavernDelta(
    float X, float Y, float Z,
    float SurfaceHeight,
    const FVoxelGenerationConfig& Config)
{
    const FCrystalCavernsConfig& CVC = Config.CaveCrystals;
    const FVector Off = Config.GetSeedOffset();
    const float nX = X + Off.X, nY = Y + Off.Y, nZ = Z + Off.Z;

    const float CavernCeiling = SurfaceHeight - CVC.DepthStart;
    if (Z > CavernCeiling) return 0.f;

    // --- ⚡ Optimization: Floor gate avoids running 3D noise for endless depths ---
    const float DepthEndLimit = CVC.FadeDepth + 8000.f; // Max thickness of cavern layer list
    if (Z < CavernCeiling - DepthEndLimit) return 0.f;


    const float Fade = FMath::Clamp((CavernCeiling - Z) / CVC.FadeDepth, 0.f, 1.f);

    // Enforce minimum frequency so chamber pattern varies within a chunk; prevents
    // one chamber from filling an entire chunk and creating a single void.
    const float MinChamberFreq = 0.00005f;
    const float CF = FMath::Max(CVC.ChamberFrequency, MinChamberFreq);

    const float Ch1 = FMath::Abs(FBM(nX * CF,         nY * CF,         nZ * CF,           4, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves));
    const float Ch2 = FMath::Abs(FBM(nX * CF * 0.7f,  nY * CF * 0.7f,  nZ * CF + 5678.f,  3, 2.1f, 0.5f, Config.Performance.MaxNoiseOctaves));

    float CarveFactor = FMath::Max(0.f, CVC.ChamberThreshold - FMath::Min(Ch1, Ch2)) * CVC.ChamberStrength;

    float Veins = 0.f;
    if (CVC.bEnableConnectingVeins)
    {
        const float VeinNoise = FBM(nX * CF * 2.5f, nY * CF * 2.5f, nZ * CF * 2.5f, 2, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);
        Veins = FMath::Pow(FMath::Max(0.f, 1.f - FMath::Abs(VeinNoise)), CVC.VeinPower) * CVC.VeinStrength;
    }

    CarveFactor = FMath::Clamp(CarveFactor + Veins, 0.f, 1.5f);

    const float Detail = FastNoise3D(nX * CVC.CrystalDetailFrequency, nY * CVC.CrystalDetailFrequency, nZ * CVC.CrystalDetailFrequency);
    const float CrystalFill = FMath::Max(0.f, Detail - CVC.CrystalThreshold) * CVC.CrystalAmplitude;

    return (-(CarveFactor * 1.2f) + CrystalFill * 1.0f) * Fade;
}
