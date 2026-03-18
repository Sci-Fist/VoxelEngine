
// =============================================================================

// SurfaceBiomesConfig.h

// =============================================================================
//

// Configuration for all 2D height-field SURFACE BIOMES.

//

// ── TYPES DEFINED HERE ───────────────────────────────────────────────────────

//   FBiomeBlendConfig    — temperature / erosion noise frequencies + strengths

//   FForestBiomeConfig   — rolling plains and forested hills

//   FDesertBiomeConfig   — sand dunes and arid flats

//   FPeaksBiomeConfig    — dramatic alpine mountains

//   FCliffsBiomeConfig   — ridged, terraced canyon walls

//   FMesaBiomeConfig     — flat-top sandstone plateaus

//   FCraterBiomeConfig   — impact basins with raised rims

//   FOverhangConfig      — cliff-face ledge parameters

//

// ── BIOME DISTRIBUTION OVERVIEW ──────────────────────────────────────────

//  Two orthogonal 2D Perlin fields drive biome placement:

//

//    Temperature (X axis)── cold ──────────────────────────────── hot →

//    Erosion     (Y axis)── flat ───────────────────────────── rugged →

//

//    ┌──────────────────────────────────────────────────────────┐

//    │            Temperature axis →                       │

//    │  cool           |           warm    hot              │

//    │  ___________________________________________          │

//    │ |                         |            |   │         │

//    │ |  Forest (flat/temp)     | Desert     |   │ flat    │

//  E │ |_________________________|____________|___│         │

//  r │ |                         |                │         │

//  o │ |  Peaks (rough/cool)     | Cliffs         │ rugged  │

//  s │ |_________________________|________________│         │

//  i │ |                         | Mesa           │         │

//  o │ |                         |________________│         │

//  n │ |  (Craters override any zone via separate  │         │

//    │ |   low-freq noise field)                  │         │

//    │ |__________________________________________│         │

//    └──────────────────────────────────────────────────────────┘

//

//  Weights are smoothstepped so boundaries blend smoothly. Craters use a

//  separate low-frequency noise field and override any blended biome locally.

//  All weights are normalized to sum to 1.0 (FVoxelBiomeWeightMap::Normalize).

//

// ── ADDING A NEW BIOME ─────────────────────────────────────────────────────

//  1. Add a config struct here (e.g. FSwampBiomeConfig).

//  2. Add entry to EVoxelBiome enum (VoxelBiome.h) and update MaxBiomes.

//  3. Update GBiomeOrder[] in VoxelGeneratorTask.cpp (compile-time assert enforces this).

//  4. Add GetSwampHeight() to FVoxelBiomeGenerators.

//  5. Wire weight calculation into FVoxelBiomeManager::GetBiomeWeightsStatic.

//  6. Add render + water config fields to AVoxelWorld and FVoxelGenerationConfig.

//
// ── PARAMETER TUNING GUIDE ───────────────────────────────────────────────────
//  Noise Frequency:    Lower = broader, smoother terrain features (0.00005-0.0002)

//                      Higher = finer detail, more variation (0.0003-0.001)
//

//  HeightMin/HeightMax: Absolute world Z values (cm) relative to SeaLevel.
//                       Typical range: 500-25000 (5m to 250m above sea level)
//                       Peaks/Mesa can go higher (30000-80000) for dramatic mountains.
//
//  Octaves:            Number of noise layers. More = more detail but slower.
//                      Recommended: 3-5 for performance, 6-8 for maximum detail.
//

//  Sharpness:          Power applied to normalized noise. Higher = sharper peaks/ridges.
//                      Range: 1.0 (smooth) to 3.0 (sharp). >3.0 can create spikes.
//
//  DetailAmplitude:    High-frequency noise amplitude (cm). Adds small surface variation.

//                      Keep proportional to height range: ~5-10% of (HeightMax-HeightMin).
//
//  ── CRATER-SPECIFIC TUNING ────────────────────────────────────────────────
//  Frequency:          How common craters are. Lower = rarer, larger craters.

//                      0.00002 = ~1 crater per 50km², 0.0001 = frequent small craters.
//
//  Depth:              Crater floor depth BELOW base plains (negative value, cm).
//                      -1000 to -3000 = shallow basins, -4500 to -8000 = deep impacts.
//                      Too deep (> -10000) can cause extreme terrain discontinuities.
//
//  RimHeight:          Raised rim elevation ABOVE base plains (positive cm).
//                      2000-4000 = modest rim, 5000-8000 = dramatic crater walls.
//                      Should be similar magnitude to |Depth| for balanced relief.
//
//  RimWidth:           Width of the transition zone from plains to rim peak (0-1 normalized).

//                      0.05-0.15 = very steep walls (sharp crater), 0.20-0.40 = gentle slopes.
//                      THIS CONTROLS WALL VISIBILITY — smaller values create more dramatic walls.
//

//  RimNoiseAmplitude:  Vertical variation added to rim height (cm).
//                      50-150 = subtle irregularity, 200-500 = jagged rim edge.
//                      High values (>300) can create pillar spikes at zone boundaries.

//
//  ShapeDistortion:    Low-frequency noise that warps crater circularity (0-1).
//                      0.0 = perfect circles, 0.2-0.3 = natural organic shapes.

//                      >0.5 creates chaotic blobs that blend poorly with adjacent biomes.
//

//  BorderIrregularity: Additional edge noise (0-1). Works with ShapeDistortion.
//                      Keep total (ShapeDistortion + BorderIrregularity) < 0.5 for clean blends.
//
//  CraterSizeMultiplier: Scales crater diameter. 1.0 = default size, 2.0 = 2x larger.
//                        Larger values make craters cover more area but require lower Frequency.
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend")
    float TemperatureFrequency = 0.00015f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend")
    float ErosionFrequency = 0.00010f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend")
    float PeaksStrength = 1.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend")
    float CliffsStrength = 1.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend")
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest")
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert")
    int32 Octaves = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert")
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks")
    float HeightMax = 25000.f;  // FIX: was 80000 (800m) — caused extreme stalagmite spikes

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks")
    int32 Octaves = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks")
    float Sharpness = 1.8f;  // FIX: was 2.5 — high sharpness on FBM creates razor peaks

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks")
    float DetailAmplitude = 300.f;  // FIX: was 500 — reduced detail noise to soften peaks
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs")
    float HeightMax = 15000.f;  // FIX: was 40000 — too tall combined with ridge sharpness

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs")
    int32 Octaves = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs")
    float Sharpness = 1.8f;  // FIX: was 3.6 — ridge noise ^ 3.6 = pure stalagmites

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs")
    float TerraceFactor = 0.35f;  // FIX: was 0.6 — less aggressive terracing

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs")
    int32 TerraceSteps = 5;  // FIX: was 12 — fewer steps = smoother cliff faces

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs")
    float DetailAmplitude = 400.f;  // FIX: was 800 — reduced detail noise amplitude
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa")
    int32 PlateauSteps = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa")
    float EdgeSharpness = 10.f;
};


// ============================================================

//  SURFACE — CRATER BIOME

// ============================================================

//
// Impact crater generation creates depressions with raised rims using a
// two-zone height profile: outer rim (elevated) and inner floor (depressed).
// The algorithm uses a low-frequency placement noise to determine crater
// centers, then applies smooth transitions to avoid Surface Nets artifacts.

//
// ── CRATER SHAPE PARAMETERS ───────────────────────────────────────────────
//

//  Frequency:          Controls crater density (inverse of typical spacing).
//                      0.00002 = ~1 crater per 50 km² (rare, large)
//                      0.00005 = ~1 crater per 20 km² (moderate)
//                      0.00010 = frequent small craters (every few km)
//                      ↓ Lower = rarer but larger craters

//                      ↑ Higher = more craters but smaller footprint
//
//  Depth:              Crater floor depth relative to base plains (NEGATIVE cm).

//                      -1000 to -3000 = shallow basins (10-30m deep)
//                      -4500 to -8000 = deep impacts (45-80m deep)
//                      -10000+ = extremely deep (can cause terrain discontinuities)

//                      NOTE: Combined with RimHeight, this controls total relief.

//
//  RimHeight:          Raised rim elevation above base plains (POSITIVE cm).
//                      2000-4000 = modest rim (20-40m)

//                      5000-8000 = dramatic crater walls (50-80m)
//                      Should be similar magnitude to |Depth| for balanced relief.
//
//  RimWidth:           Transition width from plains to rim peak (normalized 0-1).
//                      THIS IS THE KEY PARAMETER FOR WALL VISIBILITY.
//                      0.05-0.15 = very steep walls (sharp, dramatic craters)

//                      0.20-0.40 = gentle slopes (subtle basins)
//                      Smaller values create narrow transition zones → steeper visible walls.
//                      Larger values create broad slopes → walls appear gradual/hidden.
//
//  RimNoiseAmplitude:  Vertical irregularity added to rim edge (cm).
//                      50-150 = smooth rim (clean edges)

//                      200-400 = jagged rim (natural erosion look)
//                      500+ = pillar spikes at zone boundaries (artifact)

//                      Keep ≤ 200 for clean crater walls without stalagmite artifacts.
//
// ── CRATER DETAIL & DISTORTION ─────────────────────────────────────────────
//
//  ShapeDistortion:    Low-frequency warping of crater circularity (0-1).

//                      0.0 = perfect circles (artificial)
//                      0.1-0.3 = natural organic shapes (recommended)
//                      >0.5 = chaotic blobs that blend poorly with adjacent biomes.

//
//  BorderIrregularity: Additional edge noise (0-1). Works with ShapeDistortion.
//                      Keep total (ShapeDistortion + BorderIrregularity) < 0.5

//                      to maintain clean biome transitions.
//
//  BuildingNoiseFrequency / Amplitude:  Floor surface detail (small-scale height variation).
//                      Frequency: 0.001-0.003 (typical)
//                      Amplitude: 100-300 cm (subtle floor roughness)
//
//  CraterSizeMultiplier: Scales crater diameter.
//                      1.0 = default size (as determined by placement noise)
//                      2.0 = 2x larger craters (cover more area)

//                      0.5 = half-size craters (more numerous)
//                      NOTE: Larger values require lower Frequency to avoid overlap.
//
// ── PLACEMENT & OVERRIDE ───────────────────────────────────────────────────
//
//  ImpactThreshold:    Placement field threshold (normalized -1 to 1).

//                      Controls how much of the placement noise field qualifies
//                      as crater territory. Lower = more area considered for craters.
//                      Typical: -0.8 to -0.5
//                      (Advanced: adjust only if you understand the placement curve)
//
//  bForceCraterAtOrigin: If true, boosts crater weight at world anchor (0,0).

//                        Useful for ensuring player spawns in a crater basin.
//                        Coordinate offset controlled by ForcedCraterCenter.
//

//  ForcedCraterCenter:  World XY offset for forced crater placement (cm).

//                       Default (0,0) centers on world origin.
//                       Set to player spawn coordinates for guaranteed crater spawn.
//
// ── TUNING WORKFLOW ────────────────────────────────────────────────────────
//  1. Start with: Depth=-2500, RimHeight=3000, RimWidth=0.20, RimNoiseAmplitude=100
//  2. Adjust RimWidth to control wall steepness (lower = steeper)
//  3. Tune Depth/RimHeight ratio for desired relief (keep similar magnitudes)
//  4. Reduce RimNoiseAmplitude if you see pillar spikes at rim edges
//  5. Adjust Frequency/CraterSizeMultiplier to control crater size distribution
//  6. Use ShapeDistortion/BorderIrregularity for natural crater shapes (<0.3 total)
//
// ── COMMON ISSUES ──────────────────────────────────────────────────────────
//  • No visible crater walls → RimWidth too high (>0.4) → reduce to 0.15-0.25
//  • Pillar spikes at rim → RimNoiseAmplitude too high (>300) → reduce to 100-150
//  • Craters too large → CraterSizeMultiplier too high or Frequency too low
//  • Chaotic crater shapes → ShapeDistortion + BorderIrregularity > 0.5 → reduce

//  • Terrain discontinuities → Depth too extreme (< -10000) → moderate to -4500
//
// ── ALGORITHM NOTES ───────────────────────────────────────────────────────

//  The crater height function uses a normalized depth index (0=plains, 1=center).
//  Rim profile: parabolic falloff centered at RimCenter (0.12) with RimWidth.
//  Floor profile: SmoothStep from RimCenter to WallEnd (0.25) with FloorSlope.
//  All transitions are C1-continuous to prevent Surface Nets vertex pillars.
// =============================================================================
USTRUCT(BlueprintType)
struct FCraterBiomeConfig
{
    GENERATED_BODY()


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
    float Frequency = 0.00002f;

// REALISTIC CRATER CONFIGURATION: Natural impact craters
// Depth: 25-45m deep for realistic impact basins
// RimHeight: 30-50m high walls for natural crater rims
// RimWidth: 20-30% transition zone for gentle slopes
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
float Depth = -2500.f;

// Natural rim walls for realistic crater appearance
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
float RimHeight = 3500.f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
float ImpactThreshold = -0.7f;

// Reduced noise amplitude to prevent pillar artifacts while maintaining rim detail
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
float RimNoiseAmplitude = 100.f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
float FloorNoiseAmplitude = 200.f;

// Wider transition zone for gentle crater slopes
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
float RimWidth = 0.25f;


UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
float FloorSlope = 0.60f;

// Moderate distortion for natural crater shapes
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
float ShapeDistortion = 0.15f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
float BorderIrregularity = 0.20f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
float BuildingNoiseFrequency = 0.0015f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
float BuildingNoiseAmplitude = 150.f;

// Default size multiplier for appropriately sized craters
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
float CraterSizeMultiplier = 1.0f;

/** If true, forces a crater biome boost at the world origin so players always spawn in a crater basin. */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
bool bForceCraterAtOrigin = true;

/** Coordinate offset anchor location to center the forced crater boost over (e.g., spawn coordinates). */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overhangs")
    float Amplitude = 0.12f;  // FIX: was 0.35 — was amplifying spikes on steep terrain
};
