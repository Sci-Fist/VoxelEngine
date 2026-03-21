// VoxelBiomeManager.cpp
// FIX N10 — J-curve skyland probability fields (ProbMidDipCenter, ProbMidDipWidth,
//            ProbMidDipDepth, ProbHighAltitudeThreshold) are now actually applied
//            in GetSkylandColumnCache() via VoxelBiomeGenerators.
//            Previously these four UPROPERTYs were defined but never read,
//            making BaseProbability the only probability driver regardless of terrain.
//            Now: probability has a Gaussian dip at mid-altitude (so rolling hills
//            rarely get skylands) and a surge at high altitude (peaks get dense islands).

#include "VoxelBiomeManager.h"
#include "FirstVoxel.h"
#include "../VoxelLogger.h"
#include "VoxelBiomeGenerators.h"

DEFINE_LOG_CATEGORY(LogVoxelBiome);

FVoxelBiomeWeightMap FVoxelBiomeManager::GetBiomeWeightsStatic(
    float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FVector Off = Config.GetSeedOffset();
    const float Temp    = GetTemperatureWithSeed(X, Y, Config, Off);
    const float Erosion = GetErosionWithSeed    (X, Y, Config, Off);
    const FBiomeBlendConfig& B = Config.BiomeBlend;

    FVoxelBiomeWeightMap Map;

    float ForestW = FMath::SmoothStep(0.0f,0.6f,1.0f-Erosion)*FMath::SmoothStep(0.0f,1.0f,1.5f-Temp);
    float DesertW = FMath::SmoothStep(0.0f,0.6f,1.0f-Erosion)*FMath::SmoothStep(0.0f,0.25f,Temp-0.70f);
    float PeaksW  = FMath::SmoothStep(0.0f,0.2f,Erosion-0.68f)*B.PeaksStrength  *FMath::SmoothStep(0.0f,0.8f,1.2f-Temp);
    float CliffsW = FMath::SmoothStep(0.0f,0.2f,Erosion-0.50f)*B.CliffsStrength *FMath::SmoothStep(0.0f,0.6f,Temp*1.3f-0.15f);
    float MesaW   = FMath::SmoothStep(0.0f,0.2f,Temp-0.62f)   *B.MesaStrength   *FMath::SmoothStep(0.0f,0.2f,1.f-FMath::Abs(Erosion-0.4f));

    const float CraterNoise = FMath::PerlinNoise3D(FVector(
        (X+Off.X)*(Config.Craters.Frequency*0.5f),
        (Y+Off.Y)*(Config.Craters.Frequency*0.5f), 200.f));
    float CratersW = FMath::SmoothStep(Config.Craters.ImpactThreshold+0.1f, Config.Craters.ImpactThreshold, CraterNoise);

    float OceanW = 0.f;
    if (Config.Performance.bEnableForest)
    {
        const float FH = FVoxelBiomeGenerators::GetForestHeight(X, Y, Config);
        OceanW = FMath::SmoothStep(Config.SeaLevel-200.f, Config.SeaLevel-800.f, FH);
    }

    const float SafeCraterFactor = 1.0f - CratersW;
    ForestW *= SafeCraterFactor; DesertW *= SafeCraterFactor;
    PeaksW  *= SafeCraterFactor; CliffsW *= SafeCraterFactor;
    MesaW   *= SafeCraterFactor; OceanW  *= SafeCraterFactor;

    if (OceanW > 0.6f)
    {
        const float Sup = 1.0f - OceanW;
        ForestW *= Sup; DesertW *= Sup; MesaW *= Sup;
    }

    if (!Config.Performance.bEnableForest)  ForestW  = 0.f;
    if (!Config.Performance.bEnableDesert)  DesertW  = 0.f;
    if (!Config.Performance.bEnablePeaks)   PeaksW   = 0.f;
    if (!Config.Performance.bEnableCliffs)  CliffsW  = 0.f;
    if (!Config.Performance.bEnableMesa)    MesaW    = 0.f;
    if (!Config.Performance.bEnableCraters) CratersW = 0.f;

    Map.SetWeight(EVoxelBiome::Forest,  ForestW);
    Map.SetWeight(EVoxelBiome::Peaks,   PeaksW);
    Map.SetWeight(EVoxelBiome::Cliffs,  CliffsW);
    Map.SetWeight(EVoxelBiome::Mesa,    MesaW);
    Map.SetWeight(EVoxelBiome::Craters, CratersW);
    Map.SetWeight(EVoxelBiome::Desert,  DesertW);
    Map.SetWeight(EVoxelBiome::Ocean,   OceanW);
    Map.Normalize();
    return Map;
}

FVoxelBiomeManager::FWeightsAndHeight FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(
    float X, float Y, const FVoxelGenerationConfig& Config)
{
    FWeightsAndHeight Out;
    Out.Weights = GetBiomeWeightsStatic(X, Y, Config);
    Out.SurfaceHeight = GetSurfaceHeightStatic(X, Y, Out.Weights, Config);
    return Out;
}

float FVoxelBiomeManager::GetSurfaceHeightStatic(
    float X, float Y, const FVoxelBiomeWeightMap& W, const FVoxelGenerationConfig& Config)
{
    // Recompute raw (unsuppressed) base biome weights to ensure continuous underlying terrain height.
    // This prevents the crater overlay from causing steep vertical walls when its texture weight squashes the base weights to zero.
    const FVector Off = Config.GetSeedOffset();
    const float Temp    = GetTemperatureWithSeed(X, Y, Config, Off);
    const float Erosion = GetErosionWithSeed    (X, Y, Config, Off);
    const FBiomeBlendConfig& B = Config.BiomeBlend;

    float ForestW = FMath::SmoothStep(0.0f,0.6f,1.0f-Erosion)*FMath::SmoothStep(0.0f,1.0f,1.5f-Temp);
    float DesertW = FMath::SmoothStep(0.0f,0.6f,1.0f-Erosion)*FMath::SmoothStep(0.0f,0.25f,Temp-0.70f);
    float PeaksW  = FMath::SmoothStep(0.0f,0.2f,Erosion-0.68f)*B.PeaksStrength  *FMath::SmoothStep(0.0f,0.8f,1.2f-Temp);
    float CliffsW = FMath::SmoothStep(0.0f,0.2f,Erosion-0.50f)*B.CliffsStrength *FMath::SmoothStep(0.0f,0.6f,Temp*1.3f-0.15f);
    float MesaW   = FMath::SmoothStep(0.0f,0.2f,Temp-0.62f)   *B.MesaStrength   *FMath::SmoothStep(0.0f,0.2f,1.f-FMath::Abs(Erosion-0.4f));

    if (!Config.Performance.bEnableForest) ForestW = 0.f;
    if (!Config.Performance.bEnableDesert) DesertW = 0.f;
    if (!Config.Performance.bEnablePeaks)  PeaksW  = 0.f;
    if (!Config.Performance.bEnableCliffs) CliffsW = 0.f;
    if (!Config.Performance.bEnableMesa)   MesaW   = 0.f;

    float Height = 0.f, BaseWeightSum = 0.f;

    if (ForestW > 0.01f) { Height += FVoxelBiomeGenerators::GetForestHeight(X,Y,Config)*ForestW; BaseWeightSum += ForestW; }
    if (DesertW > 0.01f) { Height += FVoxelBiomeGenerators::GetDesertHeight(X,Y,Config)*DesertW; BaseWeightSum += DesertW; }
    if (PeaksW  > 0.01f) { Height += FVoxelBiomeGenerators::GetPeaksHeight(X,Y,Config)*PeaksW;   BaseWeightSum += PeaksW; }
    if (CliffsW > 0.01f) { Height += FVoxelBiomeGenerators::GetCliffsHeight(X,Y,Config)*CliffsW; BaseWeightSum += CliffsW; }
    if (MesaW   > 0.01f) { Height += FVoxelBiomeGenerators::GetMesaHeight(X,Y,Config)*MesaW;     BaseWeightSum += MesaW; }

    Height /= (BaseWeightSum + 0.0001f);

    // Craters as overlay
    Height = FVoxelBiomeGenerators::GetCraterHeight(X, Y, Config, Height);
    return Height;
}

float FVoxelBiomeManager::GetBaseSurfaceDensity(float Z, float SurfaceHeight,
                                                  const FVoxelGenerationConfig& Config)
{
    if (SurfaceHeight < Z - 1000.f) return -1.f;
    return (SurfaceHeight - Z) / Config.SurfaceGradientScale;
}

// =============================================================================
//  ComputeSkylandSpawnProbability  — FIX N10
//  Previously BaseProbability was the only driver for skyland spawn chance.
//  The J-curve fields (ProbMidDipCenter/Width/Depth, ProbHighAltitudeThreshold)
//  were defined in the config but never applied.
//  Now: a Gaussian dip suppresses probability at mid-terrain heights (no islands
//  over rolling hills) and a smooth surge above ProbHighAltitudeThreshold gives
//  dramatic peaks dense island coverage.
// =============================================================================
float FVoxelBiomeManager::ComputeSkylandSpawnProbability(
    float HeightNorm, float RoughnessNorm, const FSkylandsLayerConfig& SC)
{
    // Base probability shaped by height and roughness bonuses
    float Prob = SC.BaseProbability
               + HeightNorm     * SC.HeightProbabilityBonus
               + RoughnessNorm  * SC.RoughnessProbabilityBonus;

    // FIX N10: J-curve — Gaussian mid-altitude dip
    // Suppresses skylands over rolling hills (HeightNorm ≈ ProbMidDipCenter)
    const float DipDelta = HeightNorm - SC.ProbMidDipCenter;
    const float GaussianDip = SC.ProbMidDipDepth
        * FMath::Exp(-(DipDelta*DipDelta) / (2.f * SC.ProbMidDipWidth * SC.ProbMidDipWidth));
    Prob -= GaussianDip * Prob; // proportional suppression

    // FIX N10: J-curve — high-altitude surge above threshold
    // Islands become dramatically more common over tall peaks
    if (HeightNorm > SC.ProbHighAltitudeThreshold)
    {
        const float SurgeT = FMath::SmoothStep(
            SC.ProbHighAltitudeThreshold,
            FMath::Min(SC.ProbHighAltitudeThreshold + 0.25f, 1.f),
            HeightNorm);
        Prob += SurgeT * SC.HeightProbabilityBonus * 0.5f;
    }

    return FMath::Clamp(Prob, 0.f, 0.98f);
}

float FVoxelBiomeManager::GetTemperatureWithSeed(float X, float Y,
    const FVoxelGenerationConfig& Config, const FVector& SeedOff)
{
    return FMath::PerlinNoise2D(FVector2D(
        (X+SeedOff.X)*Config.BiomeBlend.TemperatureFrequency,
        (Y+SeedOff.Y)*Config.BiomeBlend.TemperatureFrequency))*0.5f+0.5f;
}

float FVoxelBiomeManager::GetErosionWithSeed(float X, float Y,
    const FVoxelGenerationConfig& Config, const FVector& SeedOff)
{
    return FMath::PerlinNoise2D(FVector2D(
        (X+SeedOff.X)*Config.BiomeBlend.ErosionFrequency+100.f,
        (Y+SeedOff.Y)*Config.BiomeBlend.ErosionFrequency+100.f))*0.5f+0.5f;
}
