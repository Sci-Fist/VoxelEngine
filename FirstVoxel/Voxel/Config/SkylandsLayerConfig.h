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

    // HeightProbabilityBonus: extra chance added proportional to terrain height.
    // Over high mountains this pushes probability close to 1.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability")
    float HeightProbabilityBonus = 0.70f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability")
    float RoughnessProbabilityBonus = 0.40f;

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
    // Lowered so mid-height terrain (5000-15000cm) meaningfully contributes.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="References")
    float MaxTerrainReference = 12000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="References")
    float RoughnessReference = 0.8f;
};
