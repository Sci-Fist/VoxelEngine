// SkylandsLayerConfig.h
//
// TUNED DEFAULTS — Skylands altitude fix.
//
// Problem in screenshot: islands appeared as ground-level pillars because
// MinAltitudeAboveTerrain = 200 cm (2 meters above terrain). Islands formed
// right on the terrain surface, indistinguishable from Mesa buttes.
//
// Fix: MinAltitudeAboveTerrain = 8000 (80m), BaseAltitudeAboveTerrain = 15000 (150m).
// Islands now float clearly above terrain with visible sky gap beneath them.
//
// Also increased BaseIslandSize for better island-to-thickness ratio.
// Reduced MaxThicknessRatio so islands are wide and flat, not tower-shaped.

#pragma once
#include "CoreMinimal.h"
#include "SkylandsLayerConfig.generated.h"

USTRUCT(BlueprintType)
struct FSkylandsLayerConfig
{
    GENERATED_BODY()

    // ── Altitude ──────────────────────────────────────────────────────────────
    // TUNED: was 200 → 8000 so islands don't spawn at terrain level
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude",
        meta=(ToolTip="Minimum sky-gap between terrain surface and island bottom (cm). 8000 = 80m minimum."))
    float MinAltitudeAboveTerrain = 8000.f;

    // TUNED: was 5000 → 15000 for clearly floating islands
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude",
        meta=(ToolTip="Average altitude over mid-height terrain (cm). 15000 = 150m base height."))
    float BaseAltitudeAboveTerrain = 15000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude")
    float HeightAltitudeBonus = 20000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude")
    float RoughnessAltitudeBonus = 8000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude")
    float LowTerrainAltitudeBoost = 3000.f;

    // ── Probability ───────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability")
    float BaseProbability = 0.08f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability")
    float HeightProbabilityBonus = 0.65f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability")
    float RoughnessProbabilityBonus = 0.40f;

    // J-curve probability shaping
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float ProbMidDipCenter = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability",
        meta=(ClampMin="0.01", ClampMax="0.5"))
    float ProbMidDipWidth = 0.18f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float ProbMidDipDepth = 0.55f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float ProbHighAltitudeThreshold = 0.50f;

    // ── Size ──────────────────────────────────────────────────────────────────
    // TUNED: was 2500 → 4000 for more visible islands
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size",
        meta=(ToolTip="Minimum island radius (cm). 4000 = 80m diameter minimum."))
    float BaseIslandSize = 4000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size")
    float HeightSizeBonus = 16000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size")
    float RoughnessSizeBonus = 8000.f;

    // TUNED: was 0.2 → 0.15 for flatter (less tower-like) aspect ratio
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size",
        meta=(ToolTip="Island thickness = IslandSize * ThicknessRatio. 0.15 = flat pancake shape."))
    float ThicknessRatio = 0.15f;

    // TUNED: was 0.3 → 0.20 to prevent tall tower artifacts
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size",
        meta=(ClampMin="0.1", ClampMax="0.5",
              ToolTip="Max thickness ratio. 0.20 prevents pillar artifacts. Keep below ThicknessRatio*2."))
    float MaxThicknessRatio = 0.20f;

    // ── Shape ─────────────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shape") float ShapeFrequency = 0.0003f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shape") int32 ShapeOctaves   = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Absolute Noise Bounds")
    float ThresholdAtMaxProbability = -0.18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Absolute Noise Bounds")
    float ThresholdAtMinProbability = 0.25f;

    // ── Domain warping ────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Domain Warp") bool  bEnableDomainWarping = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Domain Warp") float DomainWarpStrength  = 1200.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Domain Warp") float DomainWarpFrequency = 0.0007f;

    // ── Hanging roots ─────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hanging Roots") bool  bEnableHangingRoots = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hanging Roots") float RootTaperLength     = 0.7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hanging Roots") float RootFrequency        = 0.0018f;

    // ── Reference calibration ─────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="References") float MaxTerrainReference = 25000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="References") float RoughnessReference  = 0.8f;

    // TUNED: was 0.25 → 0.40 for more substantial shards
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size",
        meta=(ClampMin="0.01", ClampMax="0.5",
              ToolTip="Minimum shard size as fraction of BaseIslandSize. 0.40 = 40% = ~1600cm radius over flat plains."))
    float ShardMinScale = 0.40f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size",
        meta=(ClampMin="0.1", ClampMax="0.9"))
    float ShardTransitionStrength = 0.45f;
};
