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

//  RimNoiseAmplitude:  Vertical variation added to rim edge (cm).
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
//
//  ── EJECTA FEATURES (Auswurfmaterial) ─────────────────────────────────────
//  EjectaBlanketWidth:   Width of ejecta material beyond rim (0-1 normalized).
//                        0.20-0.30 = moderate ejecta field, 0.40+ = extensive ejecta.
//                        Creates the "splash" pattern of material thrown out during impact.
//
//  EjectaThickness:      Thickness of ejecta material relative to crater depth (0-1).
//                        0.10-0.20 = thin ejecta layer, 0.30+ = thick ejecta deposits.
//                        Thicker ejecta creates more pronounced terrain around crater.
//
//  EjectaBlockFrequency: Frequency of large ejecta blocks/boulders (0.001-0.01).
//                        Lower values = fewer, more scattered blocks.
//                        Higher values = dense boulder fields in ejecta zone.
//
//  EjectaBlockAmplitude: Height of individual ejecta blocks (cm).
//                        500-2000 = small boulders, 3000-8000 = large impact blocks.
//                        Creates dramatic rock formations in ejecta field.
//
//  EjectaBlockSize:      Size variation of ejecta blocks (0-1).
//                        Controls the scale and distribution of block sizes.
//
//  OverturnedStrataFrequency: Frequency of overturned geological layers (0.001-0.01).
//                             Lower values = fewer strata features.
//                             Higher values = more frequent bent rock layers.
//
//  OverturnedStrataAmplitude: Height of overturned strata features (cm).
//                            300-1000 = subtle layer bending, 1500-5000 = dramatic strata.
//                            Creates the characteristic "folded" rock appearance.
//
//  EjectaFadeExponent:   How quickly ejecta material fades with distance (1.0-4.0).
//                        1.0 = linear fade, 2.0-3.0 = exponential fade, 4.0+ = sharp cutoff.
//                        Controls the transition from thick ejecta near rim to normal terrain.
//
//  ── EJECTA SYSTEM OVERVIEW ────────────────────────────────────────────────
//  The ejecta system simulates real impact crater ejecta blankets with three main components:
//
//  1. Ejecta Blanket (Ejecta-Decke): Layer of material thrown out during impact,
//     thickest near rim, thinning outward with exponential fade.
//
//  2. Ejecta Blocks (Blockfeld): Large angular rock blocks scattered in ejecta zone,
//     creating boulder fields and dramatic terrain features.
//
//  3. Overturned Strata (Überkippte Schichten): Bent and folded rock layers at
//     crater edge, showing geological disruption from impact.
//
//  All ejecta features are concentrated within EjectaBlanketWidth beyond the rim
//  and fade naturally into the surrounding terrain.
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

    // ── MESA & BUTTE FORMATION ─────────────────────────────────────────────
    // Controls the generation of flat-topped structures with steep sides
    // Mesas: wide, flat tops with steep vertical sides
    // Buttes: smaller, isolated stone "islands"
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation")
    float MesaFrequency = 0.00008f;  // Lower = larger, more spaced mesas

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation")
    float MesaSizeMin = 15000.f;     // Minimum mesa diameter (150m)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation")
    float MesaSizeMax = 45000.f;     // Maximum mesa diameter (450m)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation")
    float ButteFrequency = 0.00015f; // Higher = more buttes

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation")
    float ButteSizeMin = 5000.f;     // Minimum butte diameter (50m)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Formation")
    float ButteSizeMax = 12000.f;    // Maximum butte diameter (120m)

    // ── PILLAR & HOODOO FORMATION ──────────────────────────────────────────
    // Tall, narrow columns that can be conical or irregularly curved
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")
    float PillarFrequency = 0.00025f; // Frequency of pillar formations

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")
    float PillarHeightMin = 8000.f;   // Minimum pillar height (80m)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")
    float PillarHeightMax = 25000.f;  // Maximum pillar height (250m)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")
    float PillarWidthMin = 1000.f;    // Minimum pillar width (10m)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")
    float PillarWidthMax = 5000.f;    // Maximum pillar width (50m)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Pillars")
    float PillarConicalFactor = 0.3f; // How much pillars taper (0.0 = cylindrical, 1.0 = very conical)

    // ── LAYERED STRATIFICATION ─────────────────────────────────────────────
    // Horizontal bands of rock layers with consistent thickness
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Stratification")
    float LayerFrequency = 0.005f;    // Frequency of layer boundaries

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Stratification")
    float LayerThickness = 2000.f;    // Thickness of each rock layer (20m)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Stratification")
    float LayerVariation = 0.3f;      // Random variation in layer thickness

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Stratification")
    float LayerHardness = 0.7f;       // How resistant layers are to erosion (0.0 = soft, 1.0 = hard)

    // ── CLIFFS & ESCARPMENTS ───────────────────────────────────────────────
    // Sharp elevation changes with planar or terraced cliff faces
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Cliffs")
    float CliffFrequency = 0.00012f;  // Frequency of cliff formations

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Cliffs")
    float CliffHeightMin = 5000.f;    // Minimum cliff height (50m)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Cliffs")
    float CliffHeightMax = 20000.f;   // Maximum cliff height (200m)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Cliffs")
    float CliffTerracing = 0.4f;      // How much cliffs are terraced vs planar (0.0 = flat, 1.0 = fully terraced)

    // ── EROSION CHANNELS ───────────────────────────────────────────────────
    // Branching networks carved by water runoff
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Erosion")
    float ChannelFrequency = 0.00020f; // Frequency of erosion channels

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Erosion")
    float ChannelDepth = 1500.f;       // Depth of erosion channels (15m)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Erosion")
    float ChannelWidth = 800.f;        // Width of erosion channels (8m)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Erosion")
    float ChannelBranching = 0.6f;     // How much channels branch (0.0 = straight, 1.0 = highly branched)

    // ── TALUS SLOPES ───────────────────────────────────────────────────────
    // Accumulated rock debris at the base of cliffs
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Talus")
    float TalusFrequency = 0.00010f;   // Frequency of talus slope formations

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Talus")
    float TalusAngle = 35.0f;          // Angle of repose for talus slopes (degrees)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa|Talus")
    float TalusSpread = 0.8f;          // How far talus spreads from cliff base
};


// ============================================================
//  SURFACE — CRATER BIOME
// ============================================================
//
// Impact crater generation creates depressions with raised rims using a
// hierarchical system: one large central crater with smaller surrounding impacts.
// The algorithm uses distance-based falloff and multiple noise layers to create
// a natural impact crater field with dramatic central features.
//
// ── HIERARCHICAL CRATER SYSTEM ─────────────────────────────────────────────
//
//  Central Crater:   Large primary impact with deep basin and high rim
//  Secondary Craters: Smaller impacts around the primary crater
//  Tertiary Craters:  Very small impacts in the surrounding area
//
// ── CENTRAL CRATER PARAMETERS ──────────────────────────────────────────────
//
//  CentralCraterRadius:    Radius of the main central crater (cm)
//                          10000-20000 = 100-200m diameter (recommended)
//                          Controls the size of the dominant central feature
//
//  CentralCraterDepth:     Depth of central crater floor (NEGATIVE cm)
//                          -5000 to -8000 = 50-80m deep (recommended)
//                          Creates the main basin for the central impact
//
//  CentralCraterRimHeight: Height of central crater rim (POSITIVE cm)
//                          6000-8000 = 60-80m high walls (recommended)
//                          Creates dramatic central crater walls
//
// ── SECONDARY CRATER PARAMETERS ────────────────────────────────────────────
//
//  SecondaryCraterDensity: Density of smaller craters around central (0-1)
//                          0.2-0.5 = sparse to moderate distribution
//                          Controls how many smaller impacts appear
//
//  SecondaryCraterMaxRadius: Maximum radius for secondary craters (cm)
//                            3000-6000 = 30-60m diameter (recommended)
//                            Limits the size of surrounding impacts
//
// ── RIM PARAMETERS ─────────────────────────────────────────────────────────
//
//  RimWidth:             Transition width from plains to rim peak (normalized 0-1)
//                        0.08-0.15 = steep walls (recommended for dramatic craters)
//                        0.15-0.25 = moderate slopes
//                        0.25+ = gentle slopes (subtle basins)
//
//  RimNoiseAmplitude:    Vertical irregularity added to rim edge (cm)
//                        100-250 = natural rim detail (recommended)
//                        300+ = jagged rim (may cause artifacts)
//
//  RimErosion:           Natural weathering effect on crater rims (0-1)
//                        0.0 = sharp, artificial rims
//                        0.2-0.5 = natural weathered appearance
//
// ── DISTRIBUTION CONTROL ───────────────────────────────────────────────────
//
//  CraterDistribution:   Radial pattern strength (0-1)
//                        0.0 = random distribution
//                        0.7-0.9 = strong radial pattern from center
//                        1.0 = perfect radial symmetry
//
//  ImpactFrequency:      Controls overall crater density
//                        0.00001-0.00005 = rare, large craters
//                        0.0001-0.0003 = moderate density
//
// ── TUNING WORKFLOW ────────────────────────────────────────────────────────
//  1. Set CentralCraterRadius for desired main crater size (15000 = 150m)
//  2. Adjust CentralCraterDepth/RimHeight for dramatic relief (6000/-7000)
//  3. Set RimWidth to 0.10-0.15 for steep, visible walls
//  4. Configure SecondaryCraterDensity for surrounding impacts (0.3)
//  5. Use CraterDistribution for radial pattern (0.8)
//  6. Fine-tune RimErosion for natural appearance (0.3)
//
// ── COMMON ISSUES ──────────────────────────────────────────────────────────
//  • No central crater → CentralCraterRadius too small or depth too shallow
//  • Walls too gentle → Reduce RimWidth to 0.10-0.15
//  • Too many small craters → Reduce SecondaryCraterDensity
//  • Unnatural distribution → Increase CraterDistribution for radial pattern
//  • Rim artifacts → Reduce RimNoiseAmplitude to 100-200
//
// ── ALGORITHM NOTES ───────────────────────────────────────────────────────
//  Uses distance-based falloff from world center to create hierarchical crater
//  system. Central crater dominates near origin, secondary craters appear in
//  surrounding area with natural distribution patterns.
// =============================================================================
USTRUCT(BlueprintType)
struct FCraterBiomeConfig
{
    GENERATED_BODY()

    // Central Crater Configuration
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Central Crater")
    float CentralCraterRadius = 12000.f;  // Reduced from 18000 for smaller, more manageable crater (120m diameter)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Central Crater")
    float CentralCraterDepth = -2500.f;  // Adjusted depth to maintain dramatic relief ratio

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Central Crater")
    float CentralCraterRimHeight = 3000.f;  // Adjusted rim height to maintain impressive walls

    // Secondary Crater Configuration
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Secondary Craters")
    float SecondaryCraterDensity = 0.9f;  // Increased from 0.8 for even more surrounding craters

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Secondary Craters")
    float SecondaryCraterMaxRadius = 36000.f;  // Tripled from 12000 for much larger secondary craters

    // Tertiary Crater Configuration - Small crater variations around central crater
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tertiary Craters")
    float TertiaryCraterDensity = 0.7f;  // High density for small crater variations

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tertiary Craters")
    float TertiaryCraterMaxRadius = 4000.f;  // Small craters (40m diameter) for detailed variations

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tertiary Craters")
    float TertiaryCraterMinRadius = 1000.f;  // Very small craters (10m diameter) for fine detail

    // Rim Configuration
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rim")
    float RimWidth = 0.04f;  // Much thinner rim walls (4% of crater radius)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rim")
    float RimNoiseAmplitude = 150.f;  // Increased for more dramatic rim peaks

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rim")
    float RimErosion = 0.05f;  // Minimal erosion for sharp, dramatic rims

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rim")
    float RimPeakLength = 0.15f;  // Extended curved peaks beyond rim edge

    // Ejecta Configuration - Impact ejecta material around crater rim
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ejecta")
    float EjectaBlanketWidth = 0.30f;  // Width of ejecta blanket beyond rim (30% of crater radius, scaled for smaller crater)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ejecta")
    float EjectaThickness = 0.15f;  // Thickness of ejecta material relative to crater depth

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ejecta")
    float EjectaBlockFrequency = 0.0015f;  // Frequency of ejecta blocks

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ejecta")
    float EjectaBlockAmplitude = 1200.f;  // Height amplitude of ejecta blocks

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ejecta")
    float EjectaBlockSize = 0.08f;  // Size of individual ejecta blocks

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ejecta")
    float OverturnedStrataFrequency = 0.0025f;  // Frequency of overturned strata features

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ejecta")
    float OverturnedStrataAmplitude = 800.f;  // Height of overturned strata

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ejecta")
    float EjectaFadeExponent = 2.0f;  // How quickly ejecta fades with distance

    // Distribution Control
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distribution")
    float CraterDistribution = 0.9f;  // Increased from 0.8 for stronger radial pattern around central crater

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Distribution")
    float ImpactFrequency = 0.00015f;  // Increased from 0.00012 for more frequent impacts and variations

    // Legacy compatibility parameters
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy")
    float Depth = -4500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy")
    float RimHeight = 5500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy")
    float Frequency = 0.00008f;  // Increased from 0.00002 for more frequent craters

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy")
    float ImpactThreshold = -0.2f;  // Raised from -0.5 for easier crater formation

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy")
    float FloorNoiseAmplitude = 300.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy")
    float FloorSlope = 0.60f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy")
    float ShapeDistortion = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy")
    float BorderIrregularity = 0.20f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy")
    float BuildingNoiseFrequency = 0.0015f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy")
    float BuildingNoiseAmplitude = 350.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy")
    float CraterSizeMultiplier = 3.0f;

    /** If true, forces a crater biome boost at the world origin so players always spawn in a crater basin. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy")
    bool bForceCraterAtOrigin = true;

    /** Coordinate offset anchor location to center the forced crater boost over (e.g., spawn coordinates). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Legacy")
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