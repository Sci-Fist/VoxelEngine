// SkylandsLayerConfig.h
// Configuration for floating islands layer.

#pragma once

#include "CoreMinimal.h"
#include "SkylandsLayerConfig.generated.h"

// ============================================================
//  SKYLANDS LAYER
// ============================================================
USTRUCT(BlueprintType)
struct FSkylandsLayerConfig
{
    GENERATED_BODY()

    // --- Altitude ---
    // MinAltitude: how far above the terrain even tiny sky-shards float.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude")
    float MinAltitudeAboveTerrain = 800.f;

    // BaseAltitude: average altitude over mid-height terrain.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude")
    float BaseAltitudeAboveTerrain = 5000.f;

    // HeightAltitudeBonus: extra altitude added when terrain below is high.
    // Over peaks the islands soar much higher.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude")
    float HeightAltitudeBonus = 18000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude")
    float RoughnessAltitudeBonus = 7000.f;

    // LowTerrainAltitudeBoost: extra lift for tiny shards above flat terrain.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude")
    float LowTerrainAltitudeBoost = 2500.f;

    // --- Probability ---
    // BaseProbability: base spawn chance per grid cell even over flat terrain.
    // 0.06 = roughly one small shard every ~4 grid cells over plains.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability")
    float BaseProbability = 0.06f;

    // HeightProbabilityBonus: max extra spawn chance added at peak altitude.
    // The J-curve gates this behind the HighAltitudeThreshold so it only
    // kicks in above mid-terrain, not linearly from the ground up.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability")
    float HeightProbabilityBonus = 0.70f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability")
    float RoughnessProbabilityBonus = 0.40f;

    // --- J-Curve probability shape ---
    // Mid-altitude suppression: a Gaussian dip centred at MidDipCenter
    // carves probability down to near-zero at mid terrain heights so skylands
    // don't appear over rolling hills — only over flat plains (shards) or
    // dramatic peaks (islands).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="HeightNorm value where mid-altitude suppression is strongest (0=sea level, 1=MaxTerrainReference)."))
    float ProbMidDipCenter = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability",
        meta=(ClampMin="0.01", ClampMax="0.5",
              ToolTip="Width of the mid-altitude Gaussian dip. Larger = wider suppression band."))
    float ProbMidDipWidth = 0.18f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="Depth of the mid-altitude probability dip. 0.55 = suppresses ~55% of probability at the centre."))
    float ProbMidDipDepth = 0.55f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="HeightNorm above which the high-altitude probability surge begins."))
    float ProbHighAltitudeThreshold = 0.50f;

    // --- Size ---
    // BaseIslandSize: minimum island radius in cm — tiny shards above flat terrain.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size")
    float BaseIslandSize = 2500.f;

    // HeightSizeBonus: over high terrain islands grow much larger.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size")
    float HeightSizeBonus = 14000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size")
    float RoughnessSizeBonus = 7000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size")
    float ThicknessRatio = 0.48f;

    // --- Shape ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shape")
    float ShapeFrequency = 0.0004f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shape")
    int32 ShapeOctaves = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Absolute Noise Bounds")
    float ThresholdAtMaxProbability = -0.18f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Absolute Noise Bounds")
    float ThresholdAtMinProbability = 0.25f;

    // --- Domain Warping ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Domain Warp")
    bool bEnableDomainWarping = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Domain Warp")
    float DomainWarpStrength = 1200.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Domain Warp")
    float DomainWarpFrequency = 0.0007f;

    // --- Hanging Roots ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hanging Roots")
    bool bEnableHangingRoots = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hanging Roots")
    float RootTaperLength = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hanging Roots")
    float RootFrequency = 0.0018f;

    // --- Reference calibration ---
    // MaxTerrainReference: terrain height that maps to HeightNorm=1.
    // Raised to match Peaks.HeightMax=25000.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="References")
    float MaxTerrainReference = 25000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="References")
    float RoughnessReference = 0.8f;

    // --- Shard / Island size scale ---
    // ShardMinScale: size of the tiniest low-altitude shards as a fraction of
    // BaseIslandSize.  0.08 = 8% = ~200cm radius over flat plains.
    // Increase toward 0.2 for slightly bigger low-altitude rocks.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size",
        meta=(ClampMin="0.01", ClampMax="0.5",
              ToolTip="Minimum shard size as fraction of BaseIslandSize (0.08 = 8%)."))
    float ShardMinScale = 0.08f;

    // ShardTransitionStrength: upper bound of TerrainStrength that counts as
    // 'low altitude shard'.  Raise toward 0.6 to make the transition smoother.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size",
        meta=(ClampMin="0.1", ClampMax="0.9",
              ToolTip="TerrainStrength value where shards fully transition to islands."))
    float ShardTransitionStrength = 0.45f;
};
