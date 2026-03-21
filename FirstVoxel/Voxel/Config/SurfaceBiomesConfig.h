// =============================================================================
// SurfaceBiomesConfig.h — Crater parameters tuned for proper bowl geometry
// =============================================================================
#pragma once
#include "CoreMinimal.h"
#include "SurfaceBiomesConfig.generated.h"

USTRUCT(BlueprintType)
struct FBiomeBlendConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend") float TemperatureFrequency = 0.00015f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend") float ErosionFrequency     = 0.00010f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend", meta=(ClampMin="0.0", ClampMax="4.0")) float PeaksStrength  = 1.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend", meta=(ClampMin="0.0", ClampMax="4.0")) float CliffsStrength = 1.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend", meta=(ClampMin="0.0", ClampMax="4.0")) float MesaStrength   = 1.0f;
};

USTRUCT(BlueprintType)
struct FForestBiomeConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest") float NoiseFrequency  = 0.00007f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest") float HeightMin       = -500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest") float HeightMax       = 5000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest", meta=(ClampMin="1", ClampMax="8")) int32 Octaves = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest") float DetailFrequency = 0.0003f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest") float DetailAmplitude = 200.f;
};

USTRUCT(BlueprintType)
struct FDesertBiomeConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert") float NoiseFrequency = 0.00012f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert") float HeightMin      = 200.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert") float HeightMax      = 6000.f;
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks") float HeightMin      = 3000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks") float HeightMax      = 25000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks", meta=(ClampMin="1", ClampMax="8")) int32 Octaves = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks", meta=(ClampMin="1.0", ClampMax="3.0")) float Sharpness = 1.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks") float DetailAmplitude = 300.f;
};

USTRUCT(BlueprintType)
struct FCliffsBiomeConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs") float NoiseFrequency = 0.00035f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs") float HeightMin      = 1500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs") float HeightMax      = 15000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs", meta=(ClampMin="1", ClampMax="8")) int32 Octaves = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs", meta=(ClampMin="1.0", ClampMax="3.0")) float Sharpness = 1.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs", meta=(ClampMin="0.0", ClampMax="1.0")) float TerraceFactor = 0.35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs", meta=(ClampMin="1", ClampMax="20"))   int32 TerraceSteps  = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs") float DetailAmplitude = 400.f;
};

USTRUCT(BlueprintType)
struct FMesaBiomeConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa") float HeightBase = 8000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa") float HeightMax  = 20000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa", meta=(ClampMin="1", ClampMax="16")) int32 PlateauSteps = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa") float EdgeSharpness = 10.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation") float MesaFrequency  = 0.00008f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation") float MesaSizeMin    = 15000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation") float MesaSizeMax    = 45000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation") float ButteFrequency = 0.00015f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation") float ButteSizeMin   = 5000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation") float ButteSizeMax   = 12000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")   float PillarFrequency = 0.00025f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")   float PillarHeightMin = 4000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")   float PillarHeightMax = 14000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")   float PillarWidthMin  = 1000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")   float PillarWidthMax  = 5000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars", meta=(ClampMin="0.0", ClampMax="1.0")) float PillarConicalFactor = 0.3f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Stratification") float LayerFrequency = 0.005f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Stratification") float LayerThickness = 2000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Stratification", meta=(ClampMin="0.0", ClampMax="1.0")) float LayerVariation = 0.3f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Stratification", meta=(ClampMin="0.0", ClampMax="1.0")) float LayerHardness  = 0.7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Cliffs") float CliffFrequency = 0.00012f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Cliffs") float CliffHeightMin = 5000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Cliffs") float CliffHeightMax = 20000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Cliffs", meta=(ClampMin="0.0", ClampMax="1.0")) float CliffTerracing = 0.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Erosion") float ChannelFrequency = 0.00020f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Erosion") float ChannelDepth     = 1500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Erosion") float ChannelWidth     = 800.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Erosion", meta=(ClampMin="0.0", ClampMax="1.0")) float ChannelBranching = 0.6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Talus") float TalusFrequency = 0.00010f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Talus") float TalusAngle     = 35.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Talus", meta=(ClampMin="0.0", ClampMax="1.0")) float TalusSpread = 0.8f;
};

// ── Crater Style ──────────────────────────────────────────────────────────────
UENUM(BlueprintType)
enum class ECraterStyle : uint8
{
    Weathered UMETA(DisplayName="Weathered (Ancient)"),
    Fresh     UMETA(DisplayName="Fresh Impact"),
    Meteor    UMETA(DisplayName="Meteor Strike (Spectacular)"),
};

// ── Crater Config — five-zone bowl geometry (see VoxelBiomeGenerators_Craters.cpp)
// Zone layout for S.CraterRadius = CentralCraterRadius*0.5 = 20000 cm:
//   [0, 0.74]  Flat bowl floor (~296m diameter)
//   [0.74,0.88] Steep inner wall (SmoothStep, no discontinuity)
//   [0.88,0.93] Narrow rim crest
//   [0.93,1.00] Outer rim dropoff back to terrain
//   [1.00+]     Ejecta blanket + secondary/tertiary craters
USTRUCT(BlueprintType)
struct FCraterBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Style")
    ECraterStyle CraterStyle = ECraterStyle::Meteor;

    // ── Dimensions ────────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Dimensions",
        meta=(ClampMin="5000.0", ToolTip="Config radius. S.CraterRadius = this * 0.5. 40000 gives 200m actual radius."))
    float CentralCraterRadius = 40000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Dimensions",
        meta=(ToolTip="Bowl floor depth below terrain surface (negative cm). -8000 = 80m deep, floor below sea level so a lake fills in."))
    float CentralCraterDepth = -8000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Dimensions",
        meta=(ClampMin="0.0", ToolTip="Rim height above terrain (cm). Used directly with no multipliers. 2500 = 25m rim, realistic for a 400m crater."))
    float CentralCraterRimHeight = 2500.f;

    // ── Rim ──────────────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Rim", meta=(ClampMin="0.0", ClampMax="1.0"))
    float RimPeakLength = 0.08f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Rim",
        meta=(ClampMin="0.0", ToolTip="Noise amplitude on the rim crest (cm). Keep below RimHeight*0.2 to avoid spikes."))
    float RimNoiseAmplitude = 300.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Rim", meta=(ClampMin="0.0", ClampMax="1.0"))
    float RimErosion = 0.05f;

    // ── Central uplift ────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|Uplift")
    bool bEnableCentralUplift = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|Uplift",
        meta=(ClampMin="0.0", ClampMax="1.0", ToolTip="Uplift height fraction of |Depth|. Capped at 40% of RimHeight so it stays inside the bowl."))
    float UpliftHeightFraction = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|Uplift", meta=(ClampMin="0.0", ClampMax="1.0"))
    float UpliftRadiusFraction = 0.20f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|Uplift", meta=(ClampMin="0.5", ClampMax="4.0"))
    float UpliftShapeExponent = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|Uplift",
        meta=(ClampMin="0.0", ToolTip="Noise on uplift surface (cm). Keep below RimHeight."))
    float UpliftNoiseAmplitude = 200.f;

    // ── Impact melt sheet ─────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|ImpactMelt")
    bool bEnableImpactMelt = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|ImpactMelt", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MeltSheetRadiusFraction = 0.55f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|ImpactMelt",
        meta=(ClampMin="0.0", ClampMax="200.0", ToolTip="Floor noise in melt zone (cm). 20 = nearly glassy."))
    float MeltFloorNoiseAmplitude = 20.f;

    // ── Ejecta rays ───────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|EjectaRays")
    bool bEnableEjectaRays = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|EjectaRays", meta=(ClampMin="4", ClampMax="24"))
    int32 EjectaRayCount = 10;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|EjectaRays", meta=(ClampMin="0.0", ClampMax="1.0"))
    float EjectaRayAngularWidth = 0.12f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|EjectaRays",
        meta=(ClampMin="0.0", ToolTip="Peak height of ejecta rays (cm). Scaled down from 8000 to match the 2500cm rim."))
    float EjectaRayHeight = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|EjectaRays", meta=(ClampMin="0.1", ClampMax="4.0"))
    float EjectaRayExtent = 2.2f;

    // ── Ejecta blanket ────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta", meta=(ClampMin="0.0", ClampMax="2.0"))
    float EjectaBlanketWidth = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta",
        meta=(ClampMin="0.0", ClampMax="1.0", ToolTip="Ejecta lift as fraction of RimHeight. 0.3 = 750cm blanket lift at rim edge."))
    float EjectaThickness = 0.30f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta") float EjectaBlockFrequency     = 0.0015f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta",
        meta=(ToolTip="Max height of ejecta boulders (cm). Scaled to match smaller rim."))
    float EjectaBlockAmplitude = 600.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta", meta=(ClampMin="0.0", ClampMax="1.0"))
    float EjectaBlockSize = 0.08f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta") float OverturnedStrataFrequency = 0.0025f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta",
        meta=(ToolTip="Height of overturned strata ripples near rim (cm)."))
    float OverturnedStrataAmplitude = 400.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta", meta=(ClampMin="1.0", ClampMax="5.0"))
    float EjectaFadeExponent = 2.0f;

    // ── Secondary craters ─────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Secondary", meta=(ClampMin="0.0", ClampMax="1.0"))
    float SecondaryCraterDensity = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Secondary", meta=(ClampMin="1000.0"))
    float SecondaryCraterMaxRadius = 8000.f;

    // ── Tertiary craters ──────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Tertiary", meta=(ClampMin="0.0", ClampMax="1.0"))
    float TertiaryCraterDensity = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Tertiary") float TertiaryCraterMaxRadius = 2500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Tertiary") float TertiaryCraterMinRadius = 600.f;

    // ── Distribution ──────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Distribution") float Frequency      = 0.00008f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Distribution", meta=(ClampMin="-1.0", ClampMax="0.0")) float ImpactThreshold = -0.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Distribution") float ImpactFrequency = 0.00015f;

    // ── Floor detail ──────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Floor") float BuildingNoiseFrequency = 0.0006f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Floor",
        meta=(ToolTip="Floor noise amplitude (cm). Small = smooth glassy lake bed. Large = rough rocky floor."))
    float BuildingNoiseAmplitude = 150.f;

    // ── Coordinate handshake ──────────────────────────────────────────────────
    UPROPERTY(VisibleAnywhere, Category="Crater|SpawnSystem",
        meta=(ToolTip="Set automatically by spawn system. Do not edit manually."))
    FVector2D ForcedCraterCenter = FVector2D(0.f, 0.f);
};

// ── Overhang ──────────────────────────────────────────────────────────────────
USTRUCT(BlueprintType)
struct FOverhangConfig
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overhangs") float MaxDistFromSurface = 1500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overhangs") float NoiseFrequency     = 0.0007f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overhangs", meta=(ClampMin="0.0", ClampMax="0.5")) float Amplitude = 0.12f;
};
