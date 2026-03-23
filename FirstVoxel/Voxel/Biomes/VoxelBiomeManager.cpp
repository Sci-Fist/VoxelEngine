// VoxelBiomeManager.cpp
//
// ROOT FIX: GetNeutralSurfaceHeightStatic — returns base terrain WITHOUT crater overlay.
//
// Problem: GetSurfaceHeightStatic() ignores its weight parameter. It recomputes
// internal weights from temp/erosion noise, then ALWAYS calls GetCraterHeight()
// at the end. So passing zeroed-crater weights had zero effect. Every caller that
// tried to get "pre-crater terrain" was silently receiving the full crater-modified
// value. This caused skylands to compute altitude based on the crater FLOOR height
// (e.g. 1840 cm) instead of the surrounding terrain (e.g. 9840 cm), placing
// island bodies inside solid crater wall material.
//
// Fix: GetNeutralSurfaceHeightStatic() is identical to GetSurfaceHeightStatic()
// except the GetCraterHeight() call at the end is REMOVED.
// This gives the true pre-crater terrain reference.

#include "VoxelBiomeManager.h"
#include "FirstVoxel.h"
#include "../VoxelLogger.h"
#include "VoxelBiomeGenerators.h"

DEFINE_LOG_CATEGORY(LogVoxelBiome);

// ============================================================
//  GetBiomeWeightsStatic
// ============================================================
FVoxelBiomeWeightMap FVoxelBiomeManager::GetBiomeWeightsStatic(
    float X, float Y, const FVoxelGenerationConfig& Config,
    float* OutTemp, float* OutErosion)
{
    const FVector Off = Config.GetSeedOffset();
    const float Temp    = GetTemperatureWithSeed(X, Y, Config, Off);
    const float Erosion = GetErosionWithSeed    (X, Y, Config, Off);

    if (OutTemp)   *OutTemp   = Temp;
    if (OutErosion) *OutErosion = Erosion;

    const FBiomeBlendConfig& B = Config.BiomeBlend;

    FVoxelBiomeWeightMap Map;

    float ForestW = FMath::SmoothStep(0.0f,0.6f,1.0f-Erosion)*FMath::SmoothStep(0.0f,1.0f,1.5f-Temp);
    float DesertW = FMath::SmoothStep(0.0f,0.6f,1.0f-Erosion)*FMath::SmoothStep(0.0f,0.25f,Temp-0.70f);
    float PeaksW  = FMath::SmoothStep(0.0f,0.2f,Erosion-0.68f)*B.PeaksStrength  *FMath::SmoothStep(0.0f,0.8f,1.2f-Temp);
    float CliffsW = FMath::SmoothStep(0.0f,0.2f,Erosion-0.50f)*B.CliffsStrength *FMath::SmoothStep(0.0f,0.6f,Temp*1.3f-0.15f);
    float MesaW   = FMath::SmoothStep(0.0f,0.2f,Temp-0.62f)   *B.MesaStrength   *FMath::SmoothStep(0.0f,0.2f,1.f-FMath::Abs(Erosion-0.4f));

    float CratersW = 0.f;
    if (Config.Craters.CentralCraterRadius > 0.f)
    {
        const float dx = X - Config.Craters.ForcedCraterCenter.X;
        const float dy = Y - Config.Craters.ForcedCraterCenter.Y;
        const float Dist = FMath::Sqrt(dx*dx + dy*dy);
        const float ExactRadius = Config.Craters.CentralCraterRadius;
        CratersW = FMath::SmoothStep(ExactRadius * 1.25f, ExactRadius * 1.10f, Dist);
    }
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

// ============================================================
//  GetWeightsAndSurfaceHeightStatic
// ============================================================
FVoxelBiomeManager::FWeightsAndHeight FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(
    float X, float Y, const FVoxelGenerationConfig& Config)
{
    FWeightsAndHeight Out;
    float Temp = -999.f, Erosion = -999.f;
    Out.Weights = GetBiomeWeightsStatic(X, Y, Config, &Temp, &Erosion);
    Out.NeutralHeight = GetNeutralSurfaceHeightStatic(X, Y, Config, Temp, Erosion);
    Out.SurfaceHeight = FVoxelBiomeGenerators::GetCraterHeight(X, Y, Config, Out.NeutralHeight);
    return Out;
}

// ============================================================
//  GetSurfaceHeightStatic — includes crater overlay
//  NOTE: The 'W' parameter only affects the performance flags check below.
//  The crater overlay is always applied unconditionally.
//  Use GetNeutralSurfaceHeightStatic() to get pre-crater terrain.
// ============================================================
float FVoxelBiomeManager::GetSurfaceHeightStatic(
    float X, float Y, const FVoxelBiomeWeightMap& W, const FVoxelGenerationConfig& Config,
    float InTemp, float InErosion)
{
    const FVector Off = Config.GetSeedOffset();
    const float Temp    = (InTemp > -900.f) ? InTemp : GetTemperatureWithSeed(X, Y, Config, Off);
    const float Erosion = (InErosion > -900.f) ? InErosion : GetErosionWithSeed(X, Y, Config, Off);
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

    // Crater overlay — always applied regardless of the 'W' parameter
    Height = FVoxelBiomeGenerators::GetCraterHeight(X, Y, Config, Height);
    return Height;
}

// ============================================================
//  GetNeutralSurfaceHeightStatic — ROOT FIX
//  Returns base terrain height WITHOUT the crater overlay.
//
//  Use this everywhere "pre-crater terrain reference" is needed:
//    • Skyland altitude calculation (so islands don't anchor to floor depth)
//    • Skyland SkyLB early-out threshold (so rim doesn't kill nearby islands)
//    • Cave pass neutral height (correct cavern depth reference)
//
//  Implementation: identical to GetSurfaceHeightStatic EXCEPT the
//  GetCraterHeight() call at the end is omitted.
// ============================================================
float FVoxelBiomeManager::GetNeutralSurfaceHeightStatic(
    float X, float Y, const FVoxelGenerationConfig& Config,
    float InTemp, float InErosion)
{
    const FVector Off = Config.GetSeedOffset();
    const float Temp    = (InTemp > -900.f) ? InTemp : GetTemperatureWithSeed(X, Y, Config, Off);
    const float Erosion = (InErosion > -900.f) ? InErosion : GetErosionWithSeed(X, Y, Config, Off);
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

    // NO GetCraterHeight() call — this is the whole point of this function.
    return Height;
}

// ============================================================
//  GetBaseSurfaceDensity
// ============================================================
float FVoxelBiomeManager::GetBaseSurfaceDensity(float Z, float SurfaceHeight,
                                                  const FVoxelGenerationConfig& Config)
{
    if (SurfaceHeight < Z - 4000.f) return -1.f;
    return (SurfaceHeight - Z) / Config.SurfaceGradientScale;
}

// ============================================================
//  ComputeSkylandSpawnProbability
// ============================================================
float FVoxelBiomeManager::ComputeSkylandSpawnProbability(
    float HeightNorm, float RoughnessNorm, const FSkylandsLayerConfig& SC)
{
    float Prob = SC.BaseProbability
               + HeightNorm    * SC.HeightProbabilityBonus
               + RoughnessNorm * SC.RoughnessProbabilityBonus;

    // J-curve Gaussian mid-altitude dip
    const float DipDelta   = HeightNorm - SC.ProbMidDipCenter;
    const float GaussianDip = SC.ProbMidDipDepth
        * FMath::Exp(-(DipDelta*DipDelta) / (2.f * SC.ProbMidDipWidth * SC.ProbMidDipWidth));
    Prob -= GaussianDip * Prob;

    // J-curve high-altitude surge
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
