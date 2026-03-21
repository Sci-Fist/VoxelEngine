// =============================================================================
// SurfaceBiomesConfig.h
// =============================================================================
//
// Configuration for all 2D height-field SURFACE BIOMES.
//
// REFACTORED FCraterBiomeConfig (v2):
//   - ECraterStyle enum: Weathered / Fresh / Meteor
//   - Removed all "Legacy" category fields that were dead code
//   - Meteor style adds: central uplift peak, impact melt sheet, ejecta rays
//   - ForcedCraterCenter preserved as the coordinate handshake with spawn system
//   - All parameters documented with tuning ranges
// =============================================================================

#pragma once

#include "CoreMinimal.h"
#include "SurfaceBiomesConfig.generated.h"

// ============================================================
//  BIOME BLEND CONTROL
// ============================================================
USTRUCT(BlueprintType)
struct FBiomeBlendConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend",
        meta=(ToolTip="Perlin frequency for the temperature axis (X). Lower = broader climate zones."))
    float TemperatureFrequency = 0.00015f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend",
        meta=(ToolTip="Perlin frequency for the erosion axis (Y). Lower = broader roughness zones."))
    float ErosionFrequency = 0.00010f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend",
        meta=(ClampMin="0.0", ClampMax="4.0"))
    float PeaksStrength = 1.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend",
        meta=(ClampMin="0.0", ClampMax="4.0"))
    float CliffsStrength = 1.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend",
        meta=(ClampMin="0.0", ClampMax="4.0"))
    float MesaStrength = 1.0f;
};

// ============================================================
//  SURFACE — FOREST BIOME
// ============================================================
USTRUCT(BlueprintType)
struct FForestBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest")
    float NoiseFrequency = 0.00007f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest")
    float HeightMin = 500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest")
    float HeightMax = 6000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest", meta=(ClampMin="1", ClampMax="8"))
    int32 Octaves = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest")
    float DetailFrequency = 0.0003f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest")
    float DetailAmplitude = 200.f;
};

// ============================================================
//  SURFACE — DESERT BIOME
// ============================================================
USTRUCT(BlueprintType)
struct FDesertBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert")
    float NoiseFrequency = 0.00012f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert")
    float HeightMin = 800.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert")
    float HeightMax = 8000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert", meta=(ClampMin="1", ClampMax="8"))
    int32 Octaves = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert", meta=(ClampMin="1.0", ClampMax="3.0"))
    float Sharpness = 1.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert")
    float RippleFrequency = 0.002f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert")
    float RippleAmplitude = 40.f;
};

// ============================================================
//  SURFACE — PEAKS BIOME
// ============================================================
USTRUCT(BlueprintType)
struct FPeaksBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks")
    float NoiseFrequency = 0.00015f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks")
    float HeightMin = 3000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks",
        meta=(ToolTip="Max peak height (cm). Was 80000 — caused spike artifacts. Keep <= 30000."))
    float HeightMax = 25000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks", meta=(ClampMin="1", ClampMax="8"))
    int32 Octaves = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks", meta=(ClampMin="1.0", ClampMax="3.0"))
    float Sharpness = 1.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks")
    float DetailAmplitude = 300.f;
};

// ============================================================
//  SURFACE — CLIFFS BIOME
// ============================================================
USTRUCT(BlueprintType)
struct FCliffsBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs")
    float NoiseFrequency = 0.00035f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs")
    float HeightMin = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs",
        meta=(ToolTip="Max cliff height (cm). Was 40000 — caused spike artifacts. Keep <= 20000."))
    float HeightMax = 15000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs", meta=(ClampMin="1", ClampMax="8"))
    int32 Octaves = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs", meta=(ClampMin="1.0", ClampMax="3.0"))
    float Sharpness = 1.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs", meta=(ClampMin="0.0", ClampMax="1.0"))
    float TerraceFactor = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs", meta=(ClampMin="1", ClampMax="20"))
    int32 TerraceSteps = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs")
    float DetailAmplitude = 400.f;
};

// ============================================================
//  SURFACE — MESA BIOME
// ============================================================
USTRUCT(BlueprintType)
struct FMesaBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa")
    float HeightBase = 12000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa")
    float HeightMax = 30000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa", meta=(ClampMin="1", ClampMax="16"))
    int32 PlateauSteps = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa")
    float EdgeSharpness = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation")
    float MesaFrequency = 0.00008f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation")
    float MesaSizeMin = 15000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation")
    float MesaSizeMax = 45000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation")
    float ButteFrequency = 0.00015f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation")
    float ButteSizeMin = 5000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation")
    float ButteSizeMax = 12000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")
    float PillarFrequency = 0.00025f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")
    float PillarHeightMin = 8000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")
    float PillarHeightMax = 25000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")
    float PillarWidthMin = 1000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")
    float PillarWidthMax = 5000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float PillarConicalFactor = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Stratification")
    float LayerFrequency = 0.005f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Stratification")
    float LayerThickness = 2000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Stratification",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float LayerVariation = 0.3f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Stratification",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float LayerHardness = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Cliffs")
    float CliffFrequency = 0.00012f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Cliffs")
    float CliffHeightMin = 5000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Cliffs")
    float CliffHeightMax = 20000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Cliffs",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float CliffTerracing = 0.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Erosion")
    float ChannelFrequency = 0.00020f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Erosion")
    float ChannelDepth = 1500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Erosion")
    float ChannelWidth = 800.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Erosion",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float ChannelBranching = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Talus")
    float TalusFrequency = 0.00010f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Talus")
    float TalusAngle = 35.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Talus",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float TalusSpread = 0.8f;
};

// ============================================================
//  CRATER STYLE ENUM
//  Controls the visual profile of the main (player spawn) crater.
//
//  Weathered  — old crater, eroded rim, debris-filled floor, gentle slopes.
//               Use for background craters that are millions of years old.
//
//  Fresh      — recent impact, sharp rim, open floor, moderate ejecta.
//               Good default for the player spawn — dramatic but survivable.
//
//  Meteor     — spectacular fresh strike. Tallest rim, deepest basin,
//               central uplift peak, smooth impact-melt floor, directional
//               ejecta rays. Used for the player spawn crater when you want
//               a cinematic first impression.
// ============================================================
UENUM(BlueprintType)
enum class ECraterStyle : uint8
{
    Weathered  UMETA(DisplayName="Weathered (Ancient)"),
    Fresh      UMETA(DisplayName="Fresh Impact"),
    Meteor     UMETA(DisplayName="Meteor Strike (Spectacular)"),
};

// ============================================================
//  SURFACE — CRATER BIOME (v2 — Refactored)
//
//  ── COORDINATE HANDSHAKE ───────────────────────────────────────────────────
//  ForcedCraterCenter is the shared coordinate between three systems:
//    System 1  VoxelBiomeManager — Perlin noise produces CratersW, peaks at a
//               natural XY per seed.
//    System 2  FindCraterSpawnLocation() — scans System 1 weights, stores the
//               peak position in SpawnTargetPos, ConfigureChunk() copies that
//               into ForcedCraterCenter for every chunk.
//    System 3  GetCraterHeight() — uses ForcedCraterCenter as the bowl center
//               so the shaped geometry aligns with where the biome says the
//               crater is and where the player will spawn.
//  Do NOT replace ForcedCraterCenter with hardcoded (0,0) — it will put the
//  bowl at world origin while the player spawns at the noise peak.
//
//  ── STYLE GUIDE ────────────────────────────────────────────────────────────
//  CraterStyle = Meteor:
//    Central uplift:  dome/cone at exact center, height ~35% of |Depth|.
//                     Real meteor physics — rebound of impact pressure wave.
//    Impact melt:     Glassy smooth floor surrounding the uplift. Very low noise.
//    Ejecta rays:     8-12 narrow raised rays radiating outward, like Tycho (Moon).
//    Rim:             Tallest and sharpest. Minimal erosion. No rim widths.
//
//  CraterStyle = Fresh:
//    No central uplift (smaller impacts don't form one).
//    Moderate ejecta blanket without ray pattern.
//    Rim is sharp but not as extreme as Meteor.
//
//  CraterStyle = Weathered:
//    Filled floor (reduced depth). Rim heavily eroded. Subtle secondary craters.
// ============================================================
USTRUCT(BlueprintType)
struct FCraterBiomeConfig
{
    GENERATED_BODY()

    // ── Style ───────────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Style",
        meta=(ToolTip="Visual profile for the central (player spawn) crater."))
    ECraterStyle CraterStyle = ECraterStyle::Meteor;

    // ── Central crater dimensions ───────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Dimensions",
        meta=(ClampMin="5000.0",
              ToolTip="Half-radius of the central crater (cm). 12000 = 240m diameter."))
    float CentralCraterRadius = 12000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Dimensions",
        meta=(ToolTip="Depth of the crater floor below the surrounding plains (NEGATIVE cm). -2500 = 25m deep."))
    float CentralCraterDepth = -2500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Dimensions",
        meta=(ClampMin="0.0",
              ToolTip="Height of the crater rim above surrounding plains (cm). 3000 = 30m rim walls."))
    float CentralCraterRimHeight = 3000.f;

    // ── Rim shape ───────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Rim",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="Normalized length of the outer rim peak beyond RimEnd. 0.15 = 15% of CentralCraterRadius."))
    float RimPeakLength = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Rim",
        meta=(ClampMin="0.0",
              ToolTip="Vertical noise amplitude on the rim crest (cm). Higher = jaggier rim peaks."))
    float RimNoiseAmplitude = 150.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Rim",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="How much weathering reduces the rim height. 0=fresh, 1=fully eroded. Only meaningful with ECraterStyle::Weathered."))
    float RimErosion = 0.05f;

    // ── Meteor style — central uplift peak ─────────────────────────────────
    // These parameters are ignored for Weathered and Fresh styles.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|Uplift",
        meta=(ToolTip="[Meteor only] Enable the central rebound uplift peak. Real meteor craters form a central dome from impact pressure rebound."))
    bool bEnableCentralUplift = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|Uplift",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="[Meteor only] Uplift height as fraction of |CentralCraterDepth|. 0.35 = uplift rises 35% of the crater depth above the floor."))
    float UpliftHeightFraction = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|Uplift",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="[Meteor only] Uplift dome radius as fraction of CentralCraterRadius. 0.20 = uplift occupies central 20% of the crater."))
    float UpliftRadiusFraction = 0.20f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|Uplift",
        meta=(ClampMin="0.5", ClampMax="4.0",
              ToolTip="[Meteor only] Shape exponent for the uplift dome profile. 1.0 = cone, 2.0 = parabolic dome, 0.5 = pointed spire."))
    float UpliftShapeExponent = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|Uplift",
        meta=(ClampMin="0.0",
              ToolTip="[Meteor only] Noise amplitude on the uplift surface (cm). Adds rocky texture to the rebound dome."))
    float UpliftNoiseAmplitude = 200.f;

    // ── Meteor style — impact melt sheet ───────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|ImpactMelt",
        meta=(ToolTip="[Meteor only] Smooth glassy floor around the central uplift. Suppresses FBM noise on the crater floor for a realistic melt sheet."))
    bool bEnableImpactMelt = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|ImpactMelt",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="[Meteor only] Melt sheet radius as fraction of CentralCraterRadius. 0.60 = melt covers inner 60% of the crater floor."))
    float MeltSheetRadiusFraction = 0.60f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|ImpactMelt",
        meta=(ClampMin="0.0", ClampMax="200.0",
              ToolTip="[Meteor only] Maximum floor noise amplitude inside the melt sheet (cm). Effectively zero for a glassy surface; 50-100 for partial melt."))
    float MeltFloorNoiseAmplitude = 30.f;

    // ── Meteor style — ejecta rays ──────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|EjectaRays",
        meta=(ToolTip="[Meteor only] Enable directional ejecta rays (like Tycho crater). Rays are narrow raised ridges extending from the rim."))
    bool bEnableEjectaRays = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|EjectaRays",
        meta=(ClampMin="4", ClampMax="24",
              ToolTip="[Meteor only] Number of ejecta rays around the crater."))
    int32 EjectaRayCount = 10;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|EjectaRays",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="[Meteor only] Angular half-width of each ray in radians. 0.15 = roughly 9 degree half-width per ray."))
    float EjectaRayAngularWidth = 0.12f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|EjectaRays",
        meta=(ClampMin="0.0",
              ToolTip="[Meteor only] Peak height of each ejecta ray at the rim (cm). Rays fade outward with the EjectaFadeExponent curve."))
    float EjectaRayHeight = 800.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Meteor|EjectaRays",
        meta=(ClampMin="0.1", ClampMax="4.0",
              ToolTip="[Meteor only] Outer extent of rays as fraction of CentralCraterRadius. 2.5 = rays extend 2.5× the crater radius outward."))
    float EjectaRayExtent = 2.5f;

    // ── Ejecta blanket (all styles) ─────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta",
        meta=(ClampMin="0.0", ClampMax="2.0",
              ToolTip="Width of uniform ejecta blanket beyond the rim (fraction of CentralCraterRadius)."))
    float EjectaBlanketWidth = 0.30f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="Ejecta thickness as fraction of |CentralCraterDepth|."))
    float EjectaThickness = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta")
    float EjectaBlockFrequency = 0.0015f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta")
    float EjectaBlockAmplitude = 1200.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float EjectaBlockSize = 0.08f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta")
    float OverturnedStrataFrequency = 0.0025f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta")
    float OverturnedStrataAmplitude = 800.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Ejecta",
        meta=(ClampMin="1.0", ClampMax="5.0",
              ToolTip="How quickly ejecta fades outward. 2.0 = exponential, 4.0 = sharp cutoff."))
    float EjectaFadeExponent = 2.0f;

    // ── Secondary craters ───────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Secondary",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float SecondaryCraterDensity = 0.9f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Secondary",
        meta=(ClampMin="1000.0",
              ToolTip="Maximum radius of secondary craters (cm). 36000 = 360m diameter max."))
    float SecondaryCraterMaxRadius = 36000.f;

    // ── Tertiary craters ────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Tertiary",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float TertiaryCraterDensity = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Tertiary")
    float TertiaryCraterMaxRadius = 4000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Tertiary")
    float TertiaryCraterMinRadius = 1000.f;

    // ── Noise distribution ──────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Distribution",
        meta=(ToolTip="Perlin noise frequency used by the biome weight system to place craters. Lower = rarer craters. 0.00008 ≈ one large crater zone per km."))
    float Frequency = 0.00008f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Distribution",
        meta=(ClampMin="-1.0", ClampMax="0.0",
              ToolTip="Noise threshold below which a point is considered a crater. -0.2 = fairly common."))
    float ImpactThreshold = -0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Distribution")
    float ImpactFrequency = 0.00015f;

    // ── Floor detail ────────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Floor")
    float BuildingNoiseFrequency = 0.0015f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crater|Floor",
        meta=(ToolTip="Max floor noise amplitude (cm) in the basin. Meteor style overrides this with MeltFloorNoiseAmplitude inside the melt sheet zone."))
    float BuildingNoiseAmplitude = 350.f;

    // ── Coordinate handshake (do not remove or reinterpret) ─────────────────
    // ForcedCraterCenter is written by ConfigureChunk() from SpawnTargetPos,
    // which itself comes from FindCraterSpawnLocation().  GetCraterHeight()
    // measures dx/dy from this point so the terrain bowl aligns with where
    // the biome weight says the crater is and where the player will spawn.
    // Setting this to (0,0) will mis-align the bowl and the spawn position.
    UPROPERTY(VisibleAnywhere, Category="Crater|SpawnSystem",
        meta=(ToolTip="Set automatically by the spawn system — do not edit manually. This is the world XY of the natural noise crater peak."))
    FVector2D ForcedCraterCenter = FVector2D(0.f, 0.f);
};

// ============================================================
//  SURFACE — OVERHANG CONFIG
// ============================================================
USTRUCT(BlueprintType)
struct FOverhangConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overhangs")
    float MaxDistFromSurface = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overhangs")
    float NoiseFrequency = 0.0007f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overhangs",
        meta=(ClampMin="0.0", ClampMax="0.5",
              ToolTip="Overhang density. Was 0.35 — too high, amplified spike artifacts on steep terrain."))
    float Amplitude = 0.12f;
};
