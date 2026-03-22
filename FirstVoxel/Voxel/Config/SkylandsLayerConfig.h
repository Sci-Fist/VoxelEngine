// SkylandsLayerConfig.h
//
// TERRAIN-HEIGHT-RESPONSIVE SKYLANDS
//
// Desired behaviour (driven by CST = terrain-score-to-shard/island transition):
//
//   LOW terrain   (CST ≈ 0)  →  tiny ROUND sky-shards, hovering just above the
//                               ground (ShardAltitudeAboveTerrain).  Very little
//                               altitude jitter so they cluster in a low band.
//
//   HIGH terrain  (CST ≈ 1)  →  large FLAT-TOP sky-islands soaring far above
//                               the peaks (BaseAltitudeAboveTerrain + HeightAltitudeBonus).
//                               Large jitter for scenic variation.
//
// Size:     Lerp(BaseIslandSize * ShardMinScale,  BaseIslandSize + HeightSizeBonus,  CST)
//           ShardMinScale = 0.08 → shards are 8% of base (≈320 cm radius min)
//
// Altitude: Lerp(ShardAltitudeAboveTerrain, BaseAltitudeAboveTerrain, CST)
//           + CST*(HeightAltitudeBonus*CuH + RoughnessAltitudeBonus*CuR)
//
// Jitter:   Lerp(ShardAltitudeJitter, IslandAltitudeJitter, CST)
//           Shards get low jitter (tight band), islands get high jitter.
//
// Shape:    Thickness ratio Lerp(ShardThicknessRatio, ThicknessRatio, CST)
//           ShardThicknessRatio ≈ 0.80 → near-sphere; ThicknessRatio ≈ 0.15 → flat pancake

#pragma once
#include "CoreMinimal.h"
#include "SkylandsLayerConfig.generated.h"

USTRUCT(BlueprintType)
struct FSkylandsLayerConfig
{
    GENERATED_BODY()

    // ── Altitude ──────────────────────────────────────────────────────────────
    // Base altitude above terrain for SHARDS (CST≈0, low/flat terrain).
    // Shards hover very close to the ground — they are small boulders in the sky.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude",
        meta=(ToolTip="Altitude above terrain for sky-shards (CST=0, low terrain). Small shards hover close to the ground."))
    float ShardAltitudeAboveTerrain = 150.f;

    // Base altitude above terrain for ISLANDS (CST≈1, high/rough terrain).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude",
        meta=(ToolTip="Average altitude above terrain for full sky-islands (CST=1, high terrain)."))
    float BaseAltitudeAboveTerrain = 700.f;

    // Legacy floor clamp — island bottom never goes below this gap.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude",
        meta=(ToolTip="Hard floor: island bottom is never closer than this to neutral terrain."))
    float MinAltitudeAboveTerrain = 300.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude")
    float HeightAltitudeBonus = 1200.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude")
    float RoughnessAltitudeBonus = 500.f;

    // Altitude random jitter for SHARDS — kept small so shards cluster in a
    // tight low band rather than being scattered all over the sky.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude",
        meta=(ToolTip="Max random altitude offset for shards (CST=0). Low value keeps shards in a tight low band."))
    float ShardAltitudeJitter = 400.f;

    // Altitude random jitter for ISLANDS — large for scenic artistic variation.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude",
        meta=(ToolTip="Max random altitude offset for islands (CST=1). Large value gives scenic height variation."))
    float IslandAltitudeJitter = 6000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude")
    float LowTerrainAltitudeBoost = 400.f;

    // ── Probability ───────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability",
        meta=(ToolTip="Base probability over flat ground (0-1). Raised for denser coverage over plains."))
    float BaseProbability = 0.55f;

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

    // TUNED: was 0.15 → kept; large islands stay flat-top (pancake shape).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size",
        meta=(ToolTip="Island thickness ratio for full sky-islands (CST=1). 0.15 = flat pancake."))
    float ThicknessRatio = 0.15f;

    // Thickness ratio for SHARDS (CST=0). Near-sphere (0.80) makes them look
    // like rounded boulders rather than flat discs — the smaller, the rounder.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size",
        meta=(ClampMin="0.3", ClampMax="1.0",
              ToolTip="Shard thickness ratio (CST=0). 0.80 = near-sphere; blends toward ThicknessRatio as terrain rises."))
    float ShardThicknessRatio = 0.80f;

    // TUNED: was 0.3 → 0.20 to prevent tall tower artifacts
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size",
        meta=(ClampMin="0.1", ClampMax="0.5",
              ToolTip="Max thickness ratio (safety cap). Keep at or above ThicknessRatio."))
    float MaxThicknessRatio = 0.80f;

    // ── Shape ─────────────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shape") float ShapeFrequency = 0.0003f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shape") int32 ShapeOctaves   = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Absolute Noise Bounds")
    float ThresholdAtMaxProbability = -0.18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Absolute Noise Bounds",
        meta=(ToolTip="Noise density threshold cutoff. Lowered to -0.10 to prevent transparent island voxel cores."))
    float ThresholdAtMinProbability = -0.10f;

    // ── Domain warping ────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Domain Warp") bool  bEnableDomainWarping = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Domain Warp") float DomainWarpStrength  = 1200.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Domain Warp") float DomainWarpFrequency = 0.0007f;

    // ── Hanging roots ─────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hanging Roots") bool  bEnableHangingRoots = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hanging Roots") float RootTaperLength     = 0.7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hanging Roots") float RootFrequency        = 0.0018f;

    // ── Reference calibration ─────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="References",
        meta=(ToolTip="Max height reference for scaling maps. Lowered to match flat world defaults."))
    float MaxTerrainReference = 1000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="References") float RoughnessReference  = 0.8f;

    // ShardMinScale = 0.08 → shards over low terrain are 8% of BaseIslandSize.
    // At BaseIslandSize=4000 cm this is 320 cm radius — a small floating rock.
    // High-terrain islands reach BaseIslandSize + HeightSizeBonus = 20 000 cm radius.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size",
        meta=(ClampMin="0.01", ClampMax="0.9",
              ToolTip="Shard size as fraction of BaseIslandSize (CST=0, low terrain). 0.08 = tiny boulder."))
    float ShardMinScale = 0.08f;

    // ShardTransitionStrength controls how quickly the shard→island transition happens
    // as terrain score (TS) rises. Lower = slower transition (more shards over hills).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size",
        meta=(ClampMin="0.1", ClampMax="0.9"))
    float ShardTransitionStrength = 0.45f;
};
