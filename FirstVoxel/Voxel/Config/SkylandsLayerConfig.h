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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude")
    float MinAltitudeAboveTerrain = 1400.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude")
    float BaseAltitudeAboveTerrain = 7500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude")
    float HeightAltitudeBonus = 22000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude")
    float RoughnessAltitudeBonus = 9000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude")
    float LowTerrainAltitudeBoost = 1200.f;

    // --- Probability ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability")
    float BaseProbability = 0.004f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability")
    float HeightProbabilityBonus = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability")
    float RoughnessProbabilityBonus = 0.25f;

    // --- Size ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size")
    float BaseIslandSize = 1600.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size")
    float HeightSizeBonus = 12000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size")
    float RoughnessSizeBonus = 6000.f;

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
    float ThresholdAtMinProbability = 0.68f;

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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="References")
    float MaxTerrainReference = 80000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="References")
    float RoughnessReference = 0.8f;
};
