// =============================================================================
// SurfaceBiomesConfig.h
// =============================================================================
//
// TUNED DEFAULTS — Visual quality pass based on in-engine observation:
//
//  CRATER  — Was barely visible because:
//    1. CentralCraterRadius=12000 (240m) is too small at world scale
//    2. CentralCraterDepth=-2500 (25m) + CentralCraterRimHeight=3000 (30m)
//       produce geometry well below surrounding terrain (terrain ~10000cm)
//    FIX CRATER-ANCHOR (in VoxelBiomeGenerators_Craters.cpp) resolves the
//    anchor bug. The new defaults produce a visually dramatic crater:
//    - 800m diameter (radius 40000cm) — dominates the spawn area
//    - 180m deep floor — well below sea level → crater lake fills automatically
//    - 200m rim height — towers clearly above surrounding terrain
//
//  SKYLANDS — Were appearing at ground level because MinAltitudeAboveTerrain=200
//    (2m above terrain). Islands spawned right on the surface, indistinguishable
//    from mesa pillars. New defaults float them clearly above terrain.
//
// =============================================================================

#pragma once

#include "CoreMinimal.h"
#include "SurfaceBiomesConfig.generated.h"

USTRUCT(BlueprintType)
struct FBiomeBlendConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend")
    float TemperatureFrequency = 0.00015f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend")
    float ErosionFrequency = 0.00010f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend", meta=(ClampMin="0.0", ClampMax="4.0"))
    float PeaksStrength = 1.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend", meta=(ClampMin="0.0", ClampMax="4.0"))
    float CliffsStrength = 1.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend", meta=(ClampMin="0.0", ClampMax="4.0"))
    float MesaStrength = 1.0f;
};

USTRUCT(BlueprintType)
struct FForestBiomeConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest") float NoiseFrequency = 0.00007f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest") float HeightMin = -1000.f; // below sea level → shorelines
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest") float HeightMax = 5000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest", meta=(ClampMin="1", ClampMax="8")) int32 Octaves = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest") float DetailFrequency = 0.0003f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest") float DetailAmplitude = 200.f;
};

USTRUCT(BlueprintType)
struct FDesertBiomeConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert") float NoiseFrequency = 0.00012f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert") float HeightMin = 200.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert") float HeightMax = 7000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert", meta=(ClampMin="1", ClampMax="8")) int32 Octaves = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert", meta=(ClampMin="1.0", ClampMax="3.0")) float Sharpness = 1.6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert") float RippleFrequency = 0.002f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert") float RippleAmplitude = 40.f;
};

USTRUCT(BlueprintType)
struct FPeaksBiomeConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks") float NoiseFrequency = 0.00015f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks") float HeightMin = 3000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks") float HeightMax = 25000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks", meta=(ClampMin="1", ClampMax="8")) int32 Octaves = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks", meta=(ClampMin="1.0", ClampMax="3.0")) float Sharpness = 1.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks") float DetailAmplitude = 300.f;
};

USTRUCT(BlueprintType)
struct FCliffsBiomeConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs") float NoiseFrequency = 0.00035f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs") float HeightMin = 1500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs") float HeightMax = 15000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs", meta=(ClampMin="1", ClampMax="8")) int32 Octaves = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs", meta=(ClampMin="1.0", ClampMax="3.0")) float Sharpness = 1.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs", meta=(ClampMin="0.0", ClampMax="1.0")) float TerraceFactor = 0.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs", meta=(ClampMin="1", ClampMax="20")) int32 TerraceSteps = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs") float DetailAmplitude = 400.f;
};

USTRUCT(BlueprintType)
struct FMesaBiomeConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa") float HeightBase = 8000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa") float HeightMax = 22000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa", meta=(ClampMin="1", ClampMax="16")) int32 PlateauSteps = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa") float EdgeSharpness = 10.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation") float MesaFrequency   = 0.00008f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation") float MesaSizeMin     = 15000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation") float MesaSizeMax     = 45000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation") float ButteFrequency  = 0.00015f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation") float ButteSizeMin    = 5000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation") float ButteSizeMax    = 12000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")   float PillarFrequency     = 0.00025f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")   float PillarHeightMin     = 5000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")   float PillarHeightMax     = 16000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")   float PillarWidthMin      = 1000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")   float PillarWidthMax      = 5000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars",   meta=(ClampMin="0.0", ClampMax="1.0"))
    float PillarConicalFactor = 0.3f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Stratification") float LayerFrequency = 0.005f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Stratification") float LayerThickness = 2000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Stratification", meta=(ClampMin="0.0",ClampMax="1.0")) float LayerVariation = 0.3f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Stratification", meta=(ClampMin="0.0",ClampMax="1.0")) float LayerHardness  = 0.7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Cliffs")    float CliffFrequency  = 0.00012f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Cliffs")    float CliffHeightMin  = 5000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Cliffs")    float CliffHeightMax  = 20000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Cliffs",    meta=(ClampMin="0.0",ClampMax="1.0")) float CliffTerracing = 0.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Erosion")   float ChannelFrequency = 0.00020f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Erosion")   float ChannelDepth     = 1500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Erosion")   float ChannelWidth     = 800.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Erosion",   meta=(ClampMin="0.0",ClampMax="1.0")) float ChannelBranching = 0.6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Talus")     float TalusFrequency = 0.00010f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Talus")     float TalusAngle     = 35.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Talus",     meta=(ClampMin="0.0",ClampMax="1.0")) float TalusSpread = 0.8f;
};

// ── Crater style ──────────────────────────────────────────────────────────────
UENUM(BlueprintType)
enum class ECraterStyle : uint8
{
    Weathered  UMETA(DisplayName="Weathered (Ancient)"),
    Fresh      UMETA(DisplayName="Fresh Impact"),
    Meteor     UMETA(DisplayName="Meteor Strike (Spectacular)"),
};

USTRUCT(BlueprintType)
struct FCraterBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Style")
    ECraterStyle CraterStyle = ECraterStyle::Meteor;

    // ── TUNED: Much larger crater that dominates the spawn area ──────────────
    // Old: Radius=12000 (240m) Depth=-2500 (25m) Rim=3000 (30m) → invisible
    // New: Radius=40000 (800m) Depth=-18000 (180m) Rim=20000 (200m) → dramatic
    //
    // With the FIX CRATER-ANCHOR, these are now offsets from terrain surface.
    // Depth=-18000 means floor = terrain - 180m → below sea level → crater lake!
    // Rim=20000 means rim = terrain + 200m → visible from far away.

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Dimensions",
        meta=(ClampMin="5000.0", ToolTip="Half-radius (cm). 40000 = 800m diameter."))
    float CentralCraterRadius = 40000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Dimensions",
        meta=(ToolTip="Depth below terrain surface (negative cm). -18000 = 180m deep."))
    float CentralCraterDepth = -18000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Dimensions",
        meta=(ClampMin="0.0", ToolTip="Rim height above terrain surface (cm). 20000 = 200m rim walls."))
    float CentralCraterRimHeight = 20000.f;

    // ── Rim ──────────────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Rim", meta=(ClampMin="0.0", ClampMax="1.0"))
    float RimPeakLength = 0.15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Rim", meta=(ClampMin="0.0"))
    float RimNoiseAmplitude = 1500.f;  // increased for more irregular natural rim
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Rim", meta=(ClampMin="0.0", ClampMax="1.0"))
    float RimErosion = 0.05f;

    // ── Meteor: central uplift ────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|Uplift")
    bool bEnableCentralUplift = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|Uplift", meta=(ClampMin="0.0", ClampMax="1.0"))
    float UpliftHeightFraction = 0.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|Uplift", meta=(ClampMin="0.0", ClampMax="1.0"))
    float UpliftRadiusFraction = 0.20f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|Uplift", meta=(ClampMin="0.5", ClampMax="4.0"))
    float UpliftShapeExponent = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|Uplift", meta=(ClampMin="0.0"))
    float UpliftNoiseAmplitude = 500.f;

    // ── Meteor: impact melt sheet ─────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|ImpactMelt")
    bool bEnableImpactMelt = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|ImpactMelt", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MeltSheetRadiusFraction = 0.60f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|ImpactMelt", meta=(ClampMin="0.0", ClampMax="200.0"))
    float MeltFloorNoiseAmplitude = 30.f;

    // ── Meteor: ejecta rays ───────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|EjectaRays")
    bool bEnableEjectaRays = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|EjectaRays", meta=(ClampMin="4", ClampMax="24"))
    int32 EjectaRayCount = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|EjectaRays", meta=(ClampMin="0.0", ClampMax="1.0"))
    float EjectaRayAngularWidth = 0.12f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|EjectaRays", meta=(ClampMin="0.0"))
    float EjectaRayHeight = 8000.f;   // was 800 — much more dramatic
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|EjectaRays", meta=(ClampMin="0.1", ClampMax="4.0"))
    float EjectaRayExtent = 2.5f;

    // ── Ejecta blanket ────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta", meta=(ClampMin="0.0", ClampMax="2.0"))
    float EjectaBlanketWidth = 0.30f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta", meta=(ClampMin="0.0", ClampMax="1.0"))
    float EjectaThickness = 0.20f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta") float EjectaBlockFrequency       = 0.0015f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta") float EjectaBlockAmplitude        = 3000.f;  // was 1200
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta", meta=(ClampMin="0.0", ClampMax="1.0")) float EjectaBlockSize = 0.08f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta") float OverturnedStrataFrequency  = 0.0025f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta") float OverturnedStrataAmplitude  = 2000.f;  // was 800
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta", meta=(ClampMin="1.0", ClampMax="5.0"))
    float EjectaFadeExponent = 2.0f;

    // ── Secondary craters ─────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Secondary", meta=(ClampMin="0.0", ClampMax="1.0"))
    float SecondaryCraterDensity = 0.9f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Secondary", meta=(ClampMin="1000.0"))
    float SecondaryCraterMaxRadius = 12000.f;  // secondary craters ~120m max

    // ── Tertiary craters ──────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Tertiary", meta=(ClampMin="0.0", ClampMax="1.0"))
    float TertiaryCraterDensity = 0.7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Tertiary") float TertiaryCraterMaxRadius = 3000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Tertiary") float TertiaryCraterMinRadius = 800.f;

    // ── Distribution noise ────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Distribution")
    float Frequency = 0.00008f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Distribution", meta=(ClampMin="-1.0", ClampMax="0.0"))
    float ImpactThreshold = -0.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Distribution")
    float ImpactFrequency = 0.00015f;

    // ── Floor detail ──────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Floor") float BuildingNoiseFrequency = 0.0008f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Floor") float BuildingNoiseAmplitude = 500.f;

    // ── Coordinate handshake — do not edit manually ───────────────────────────
    UPROPERTY(VisibleAnywhere, Category="Crater|SpawnSystem")
    FVector2D ForcedCraterCenter = FVector2D(0.f, 0.f);
};

// ── Overhang ──────────────────────────────────────────────────────────────────
USTRUCT(BlueprintType)
struct FOverhangConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overhangs") float MaxDistFromSurface = 1500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overhangs") float NoiseFrequency     = 0.0007f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overhangs", meta=(ClampMin="0.0", ClampMax="0.5"))
    float Amplitude = 0.12f;
};
