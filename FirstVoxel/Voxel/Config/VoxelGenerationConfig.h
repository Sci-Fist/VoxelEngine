// VoxelGenerationConfig.h
// Master configuration hierarchy for the 3-layer voxel world generator.
//
// LAYER ARCHITECTURE:
//   Surface Layer  — height-field terrain blending multiple biomes (Forest/Peaks/Cliffs/Mesa/Craters)
//   Skylands Layer — floating islands driven by the terrain height and roughness BELOW them
//   Cave Layer     — worm tunnels + crystal cavern chambers carved below the surface
//
// Every parameter here is exposed to the Details Panel. Create a UVoxelBiomeDataAsset
// in the Content Browser to save named presets and swap them on any AVoxelWorld at runtime.

#pragma once
#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
// EVoxelBiome is needed by GetBiomeRender() in FVoxelGenerationConfig
#include "Voxel/Biomes/VoxelBiome.h"
#include "VoxelGenerationConfig.generated.h"

// ============================================================
//  BIOME BLEND CONTROL
//  Controls how the surface biomes are spatially distributed.
// ============================================================
USTRUCT(BlueprintType)
struct FBiomeBlendConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend",
        meta=(ToolTip="Scale of the Temperature noise field. Lower = larger biome regions."))
    float TemperatureFrequency = 0.00015f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend",
        meta=(ToolTip="Scale of the Erosion (flat vs rough) noise field. Lower = bigger smooth zones."))
    float ErosionFrequency = 0.00010f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend",
        meta=(ClampMin="0.0", ClampMax="2.0",
              ToolTip="Multiplier on the Peak biome weight. Raise to make mountains more dominant."))
    float PeaksStrength = 1.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend",
        meta=(ClampMin="0.0", ClampMax="2.0",
              ToolTip="Multiplier on the Cliffs biome weight. Raise for more ridged terrain."))
    float CliffsStrength = 1.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend",
        meta=(ClampMin="0.0", ClampMax="2.0",
              ToolTip="Multiplier on the Mesa biome weight."))
    float MesaStrength = 1.0f;
};

// ============================================================
//  SURFACE — FOREST BIOME
// ============================================================
USTRUCT(BlueprintType)
struct FForestBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest",
        meta=(ToolTip="Noise frequency for rolling hills. Lower = wider, smoother plains."))
    float NoiseFrequency = 0.00007f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest",
        meta=(ToolTip="Minimum height of Forest plains above sea level (cm)."))
    float HeightMin = 500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest",
        meta=(ToolTip="Maximum height of Forest hills above sea level (cm)."))
    float HeightMax = 6000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest",
        meta=(ClampMin="1", ClampMax="8",
              ToolTip="FBM octave count. More = more surface detail at higher CPU cost."))
    int32 Octaves = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest",
        meta=(ToolTip="Frequency of fine surface bumps / pebble noise."))
    float DetailFrequency = 0.0003f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest",
        meta=(ClampMin="0.0",
              ToolTip="Amplitude of the fine surface detail noise (cm). 0 = perfectly smooth."))
    float DetailAmplitude = 200.f;
};

// ============================================================
//  SURFACE — DESERT BIOME
// ============================================================
USTRUCT(BlueprintType)
struct FDesertBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert",
        meta=(ToolTip="Noise frequency for sand dunes. Lower = larger, sweeping dunes."))
    float NoiseFrequency = 0.00012f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert",
        meta=(ToolTip="Minimum dune floor height (cm)."))
    float HeightMin = 800.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert",
        meta=(ToolTip="Maximum dune crest height (cm)."))
    float HeightMax = 8000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert",
        meta=(ClampMin="1", ClampMax="8"))
    int32 Octaves = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert",
        meta=(ClampMin="0.1", ClampMax="5.0", ToolTip="Dune ridge sharpness. Higher = more pointed crests."))
    float Sharpness = 1.6f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert",
        meta=(ToolTip="Frequency of fine ripple details on sand surfaces."))
    float RippleFrequency = 0.002f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert",
        meta=(ClampMin="0.0", ToolTip="Amplitude of fine sand ripple details (cm)."))
    float RippleAmplitude = 40.f;
};

// ============================================================
//  SURFACE — PEAKS BIOME
// ============================================================
USTRUCT(BlueprintType)
struct FPeaksBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks",
        meta=(ToolTip="Noise frequency for mountain shapes. Lower = fewer, broader mountains."))
    float NoiseFrequency = 0.00015f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks",
        meta=(ToolTip="Minimum mountain height (cm)."))
    float HeightMin = 3000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks",
        meta=(ToolTip="Maximum mountain height / tallest peaks (cm)."))
    float HeightMax = 80000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks",
        meta=(ClampMin="1", ClampMax="8"))
    int32 Octaves = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks",
        meta=(ClampMin="0.5", ClampMax="8.0",
              ToolTip="Peak sharpness power curve. Higher = more pointed alpine peaks."))
    float Sharpness = 2.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks",
        meta=(ClampMin="0.0",
              ToolTip="Amplitude of rocky surface detail noise on peak faces (cm)."))
    float DetailAmplitude = 500.f;
};

// ============================================================
//  SURFACE — CLIFFS BIOME
// ============================================================
USTRUCT(BlueprintType)
struct FCliffsBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs",
        meta=(ToolTip="Noise frequency for cliff ridge patterns."))
    float NoiseFrequency = 0.00035f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs",
        meta=(ToolTip="Minimum cliff height (cm)."))
    float HeightMin = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs",
        meta=(ToolTip="Maximum cliff height (cm)."))
    float HeightMax = 40000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs",
        meta=(ClampMin="1", ClampMax="8"))
    int32 Octaves = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs",
        meta=(ClampMin="0.5", ClampMax="8.0",
              ToolTip="Ridged noise sharpness. Higher = more dramatic knife-edge ridges."))
    float Sharpness = 3.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="0 = smooth ridges, 1 = fully terraced sedimentary strata."))
    float TerraceFactor = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs",
        meta=(ClampMin="2", ClampMax="32",
              ToolTip="Number of discrete terrace step levels in the cliff face."))
    int32 TerraceSteps = 12;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs",
        meta=(ClampMin="0.0",
              ToolTip="Amplitude of rock-gravel surface detail noise (cm)."))
    float DetailAmplitude = 800.f;
};

// ============================================================
//  SURFACE — MESA BIOME
// ============================================================
USTRUCT(BlueprintType)
struct FMesaBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa",
        meta=(ToolTip="Height of the lowest mesa plateau level (cm above sea level)."))
    float HeightBase = 12000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa",
        meta=(ToolTip="Height of the tallest mesa column (cm above sea level)."))
    float HeightMax = 30000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa",
        meta=(ClampMin="1", ClampMax="16",
              ToolTip="Number of distinct flat plateau levels stacked vertically."))
    int32 PlateauSteps = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa",
        meta=(ClampMin="1.0", ClampMax="20.0",
              ToolTip="How sharp the step edges are. Higher = more abrupt vertical cliff faces."))
    float EdgeSharpness = 10.f;
};

// ============================================================
//  SURFACE — CRATER BIOME
// ============================================================
USTRUCT(BlueprintType)
struct FCraterBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters",
        meta=(ToolTip="Noise frequency controlling crater placement density. Lower = fewer, larger impact sites."))
    float Frequency = 0.00002f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters",
        meta=(ToolTip="Depth of the crater bowl floor below surrounding terrain (cm). Use negative values."))
    float Depth = -1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters",
        meta=(ClampMin="0.0",
              ToolTip="Height of the raised crater rim above surrounding terrain (cm)."))
    float RimHeight = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters",
        meta=(ClampMin="-1.0", ClampMax="0.0",
              ToolTip="Noise threshold that triggers a crater. More negative = fewer craters."))
    float ImpactThreshold = -0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters",
        meta=(ClampMin="0.0",
              ToolTip="Amplitude of jagged noise applied to the crater rim (cm)."))
    float RimNoiseAmplitude = 800.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters",
        meta=(ClampMin="0.0",
              ToolTip="Amplitude of noise on the flat crater floor (cm)."))
    float FloorNoiseAmplitude = 300.f;
};

// ============================================================
//  SKYLANDS LAYER
//  Islands float ABOVE the surface. Their altitude, size, and
//  probability all scale with the terrain directly BELOW them.
//
//  High/rough terrain (mountains) → islands fly high, are large, and spawn often.
//  Low/flat terrain (plains)      → islands fly low, are small, and spawn rarely.
// ============================================================
USTRUCT(BlueprintType)
struct FSkylandsLayerConfig
{
    GENERATED_BODY()

    // --- Altitude ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude",
        meta=(ClampMin="0.0",
              ToolTip="Minimum altitude above terrain for tiny shards over flat/low ground (cm). 1200 = 12m = tall stone pillar height. Lerped toward BaseAltitudeAboveTerrain as terrain gets taller/rougher."))
    float MinAltitudeAboveTerrain = 1400.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude",
        meta=(ClampMin="1000.0",
              ToolTip="Altitude above terrain for large islands over tall/rough ground (cm). Over flat plains this is lerped down toward MinAltitudeAboveTerrain."))
    float BaseAltitudeAboveTerrain = 7500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude",
        meta=(ClampMin="0.0",
              ToolTip="Extra altitude added when terrain below is at MaxTerrainReference height. Mountains push islands much higher into the sky."))
    float HeightAltitudeBonus = 22000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude",
        meta=(ClampMin="0.0",
              ToolTip="Extra altitude added when terrain roughness is at its maximum (fully mountainous). Rugged terrain sends islands even higher."))
    float RoughnessAltitudeBonus = 9000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Altitude",
        meta=(ClampMin="0.0",
              ToolTip="Additional altitude applied using the low-terrain falloff curve. Keeps tiny shards hovering close to the ground while allowing tall terrain to lift islands higher."))
    float LowTerrainAltitudeBoost = 1200.f;

    // --- Probability ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability",
        meta=(ClampMin="0.001", ClampMax="0.15",
              ToolTip="Baseline probability that any sky position has an island. This is the floor value, applied even over flat plains. Keep below 0.15 to prevent chunk-filling."))
    float BaseProbability = 0.004f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability",
        meta=(ClampMin="0.0", ClampMax="0.8",
              ToolTip="Probability bonus added at maximum terrain height. High mountains dramatically increase island spawning directly above. Keep below 0.8 to prevent extreme generation."))
    float HeightProbabilityBonus = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Probability",
        meta=(ClampMin="0.0", ClampMax="0.6",
              ToolTip="Probability bonus for rough/mountainous terrain. Rocky biomes spawn more islands above them. Keep below 0.6 to prevent extreme generation."))
    float RoughnessProbabilityBonus = 0.25f;

    // --- Size ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size",
        meta=(ClampMin="500.0",
              ToolTip="Minimum island horizontal radius over flat terrain (cm). Small on flatlands so rare shards look like single floating rocks, not wide plateaus."))
    float BaseIslandSize = 1600.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size",
        meta=(ClampMin="0.0",
              ToolTip="Additional island radius at maximum terrain height. Mountains spawn much larger islands above them."))
    float HeightSizeBonus = 12000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size",
        meta=(ClampMin="0.0",
              ToolTip="Additional island radius at maximum roughness. Rocky terrain spawns wider islands."))
    float RoughnessSizeBonus = 6000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Size",
        meta=(ClampMin="0.1", ClampMax="2.0",
              ToolTip="Island vertical thickness as a fraction of its horizontal radius. 0.5 = half as tall as wide."))
    float ThicknessRatio = 0.48f;

    // --- Shape ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shape",
        meta=(ToolTip="Base noise frequency for island outlines. Larger islands automatically use scaled-down frequencies for appropriate detail."))
    float ShapeFrequency = 0.0004f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shape",
        meta=(ClampMin="1", ClampMax="8",
              ToolTip="FBM octave count for island shape noise."))
    int32 ShapeOctaves = 4;

    // ThresholdAtMaxProbability is NEGATIVE: at high probability (mountains) even most
    // noise values pass (-0.28 is exceeded by ~64% of Perlin values) so islands are dense.
    // ThresholdAtMinProbability is HIGH POSITIVE: at low probability (flat plains) only
    // the top ~7% of noise values pass (>0.58), making flatland shards genuinely rare.
    // These two values lerp based on Prob so mountains get dense coverage, flatlands get almost none.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Absolute Noise Bounds",
        meta=(ClampMin="-1.0", ClampMax="1.0",
              ToolTip="Noise threshold at maximum probability (mountains). Negative = islands appear often. -0.28 = ~64% of positions qualify."))
    float ThresholdAtMaxProbability = -0.18f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Absolute Noise Bounds",
        meta=(ClampMin="-1.0", ClampMax="1.0",
              ToolTip="Noise threshold at minimum probability (flat plains). 0.58 = only top ~7% of positions qualify, making flatland shards rare."))
    float ThresholdAtMinProbability = 0.68f;


    // --- Domain Warping ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Domain Warp",
        meta=(ToolTip="Distort island edges with secondary noise for more organic, non-circular island shapes."))
    bool bEnableDomainWarping = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Domain Warp",
        meta=(EditCondition="bEnableDomainWarping", ClampMin="0.0",
              ToolTip="Strength of edge distortion. Higher = more tortured, irregular island silhouettes."))
    float DomainWarpStrength = 1200.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Domain Warp",
        meta=(EditCondition="bEnableDomainWarping", ToolTip="Frequency of the domain warping noise."))
    float DomainWarpFrequency = 0.0007f;

    // --- Hanging Roots ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hanging Roots",
        meta=(ToolTip="Generate tapering rock/dirt tendrils hanging downward from island bottoms."))
    bool bEnableHangingRoots = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hanging Roots",
        meta=(EditCondition="bEnableHangingRoots", ClampMin="0.0", ClampMax="1.5",
              ToolTip="How far roots hang below the island, as a fraction of the half-thickness. 1.0 = roots as long as island half-height."))
    float RootTaperLength = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hanging Roots",
        meta=(EditCondition="bEnableHangingRoots", ToolTip="Noise frequency of root tendrils. Higher = more, thinner roots."))
    float RootFrequency = 0.0018f;

    // --- Reference calibration ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="References",
        meta=(ClampMin="1000.0",
              ToolTip="Terrain height considered 'maximum' for computing altitude/size/probability bonuses. Set this to the tallest peak height in your world."))
    float MaxTerrainReference = 80000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="References",
        meta=(ClampMin="0.01", ClampMax="1.0",
              ToolTip="Biome roughness value considered 'maximum'. Roughness is derived from Peaks + Cliffs biome weights summed (range 0-1)."))
    float RoughnessReference = 0.8f;
};

// ============================================================
//  CAVE LAYER — WORM TUNNELS
// ============================================================
USTRUCT(BlueprintType)
struct FCaveTunnelsConfig
{
    GENERATED_BODY()

    // Tuned down to reduce over-carving in the cave layer.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tunnels",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="Absolute noise threshold for carving tunnels. Higher = thinner, rarer tunnels."))
    float Threshold = 0.55f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tunnels",
        meta=(ClampMin="0.0001",
              ToolTip="Scale of the 3D worm noise. Higher = smaller, tighter tunnel networks."))
    float Scale = 0.009f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tunnels",
        meta=(ClampMin="0.0",
              ToolTip="Carve strength. Higher ensures tunnels are fully hollow with no stray voxels."))
    float Strength = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tunnels",
        meta=(ClampMin="0.0",
              ToolTip="Minimum depth below the surface where tunnels can appear (cm)."))
    float MinDepthBelowSurface = 2000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tunnels",
        meta=(ClampMin="100.0",
              ToolTip="Vertical fade-in distance below MinDepthBelowSurface (cm). Prevents tunnels from breaking through the surface grass."))
    float SurfaceFadeDepth = 400.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tunnels",
        meta=(ToolTip="Frequency of thickness-wobble noise. This varies tunnel diameter organically."))
    float WobbleFrequency = 0.0002f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tunnels",
        meta=(ClampMin="0.0", ClampMax="0.4",
              ToolTip="Amplitude of tunnel thickness wobble. Higher = more bulbous cavern sections."))
    float WobbleAmplitude = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bedrock",
        meta=(ToolTip="World Z below which everything is always solid bedrock (cm). Tunnels and caves cannot reach below this."))
    float BedrockDepth = -5000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bedrock",
        meta=(ToolTip="Noise frequency for jagged bedrock floor variation."))
    float BedrockJagFrequency = 0.001f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bedrock",
        meta=(ClampMin="0.0",
              ToolTip="Maximum vertical variation of the jagged bedrock floor (cm)."))
    float BedrockJagAmplitude = 800.f;
};

// ============================================================
//  CAVE LAYER — CRYSTAL CAVERNS
//  Large carved voids deep underground, filled with crystal
//  stalagmite and stalactite detail.
// ============================================================
USTRUCT(BlueprintType)
struct FCrystalCavernsConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Caverns",
        meta=(ClampMin="0.0",
              ToolTip="Depth below the terrain surface where crystal cavern chambers begin to appear (cm)."))
    float DepthStart = 3000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Caverns",
        meta=(ClampMin="100.0",
              ToolTip="Smooth fade-in transition depth below DepthStart (cm). Prevents abrupt ceiling pop-ins."))
    float FadeDepth = 500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Caverns",
        meta=(ToolTip="Noise frequency for large chamber volumes. Lower = fewer, bigger chambers."))
    float ChamberFrequency = 0.0012f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Caverns",
        meta=(ClampMin="0.0", ClampMax="0.5",
              ToolTip="Noise threshold below which chambers carve. Lower = larger, more open chambers."))
    float ChamberThreshold = 0.34f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Caverns",
        meta=(ClampMin="0.0",
              ToolTip="Carve multiplier for chambers. Higher ensures chambers are fully hollow."))
    float ChamberStrength = 6.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Veins",
        meta=(ToolTip="Add thin ridged tunnels connecting chamber volumes."))
    bool bEnableConnectingVeins = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Veins",
        meta=(EditCondition="bEnableConnectingVeins", ClampMin="2.0", ClampMax="32.0",
              ToolTip="Power curve applied to vein ridges. Higher = thinner, more precise tunnel channels."))
    float VeinPower = 16.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Veins",
        meta=(EditCondition="bEnableConnectingVeins", ClampMin="0.0",
              ToolTip="Contribution strength of connecting veins to the carve field."))
    float VeinStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crystals",
        meta=(ToolTip="Noise frequency for individual crystal stalagmite / stalactite spires."))
    float CrystalDetailFrequency = 0.002f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crystals",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="Noise threshold above which crystal fill geometry appears. Higher = sparser crystals."))
    float CrystalThreshold = 0.28f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crystals",
        meta=(ClampMin="0.0",
              ToolTip="How prominently crystal spires fill cavern floors and ceilings."))
    float CrystalAmplitude = 2.5f;
};

// ============================================================
//  SURFACE — OVERHANG CONFIG
// ============================================================
USTRUCT(BlueprintType)
struct FOverhangConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overhangs",
        meta=(ClampMin="0.0",
              ToolTip="Maximum distance from the terrain surface within which overhangs can form (cm)."))
    float MaxDistFromSurface = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overhangs",
        meta=(ToolTip="Noise frequency of overhang perturbations."))
    float NoiseFrequency = 0.0007f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overhangs",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="Max density contribution from overhang noise. Higher = more dramatic, deeper overhangs."))
    float Amplitude = 0.35f;
};

// ============================================================
//  PERFORMANCE TUNING
// ============================================================
USTRUCT(BlueprintType)
struct FVoxelPerformanceConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Optimization",
        meta=(ClampMin="1", ClampMax="8",
              ToolTip="Hard cap on FBM octave count across all biomes. Reducing from 4 to 2 roughly halves generation time."))
    int32 MaxNoiseOctaves = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Optimization",
        meta=(ToolTip="Generate ledges and overhangs on cliff faces near the surface. Disable for better performance or cleaner terrain."))
    bool bEnableOverhangs = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Optimization",
        meta=(ToolTip="Enable full 3D island noise for skylands. Disabling replaces it with cheap 2D approximation."))
    bool bEnable3DSkylandNoise = true;
};

// ============================================================
//  PER-BIOME FOLIAGE ENTRY
//  Describes one type of instanced mesh to scatter on a biome.
//  Add multiple entries per biome for layered vegetation.
// ============================================================
USTRUCT(BlueprintType)
struct FVoxelFoliageEntry
{
    GENERATED_BODY()

    /** Mesh to scatter. Leave null to disable this slot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<UStaticMesh> Mesh = nullptr;

    /** Per-triangle spawn probability [0..1].  0.03 = sparse  |  0.2 = lush. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="1.0"))
    float SpawnChance = 0.05f;

    /** Spawn attempts per triangle (1 = normal, 2-8 = dense cluster). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1", ClampMax="8"))
    int32 SpawnAttemptsPerTriangle = 1;

    /** Min biome weight [0-1] required at spawn point (0 = always, 0.5 = dominant biome only). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="1.0"))
    float MinBiomeWeight = 0.2f;

    /** Min surface flatness (Normal.Z): 1.0 = flat only | 0.7 = up to 45 deg | 0.0 = any slope. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="1.0"))
    float MinSlopeAlignment = 0.7f;

    /** Min world Z (cm) for spawning. Set to SeaLevel to block underwater spawns. (-9999999 = no limit) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MinWorldZ = -9999999.f;

    /** Max world Z (cm) for spawning. Use to cap high-altitude vegetation. (9999999 = no limit) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MaxWorldZ = 9999999.f;

    /** Min random scale multiplier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.01"))
    float ScaleMin = 0.8f;

    /** Max random scale multiplier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.01"))
    float ScaleMax = 1.2f;

    /** Randomise yaw per instance. Disable for directional props (signs, fences). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bRandomYaw = true;

    /** Fixed yaw in degrees when bRandomYaw = false. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(EditCondition="!bRandomYaw", ClampMin="0.0", ClampMax="360.0"))
    float FixedYaw = 0.f;

    /** Align instance up-axis to the surface normal (good for cliff plants / mushrooms). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bAlignToSurface = false;

    /** Z offset after placement (cm). Negative = sink into terrain. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float HeightOffset = 0.f;
};

// ============================================================
//  PER-BIOME RENDER CONFIG
//  Material overrides + foliage layer list for one biome.
//  Null material overrides fall back to the world global materials.
// ============================================================
USTRUCT(BlueprintType)
struct FVoxelBiomeRenderConfig
{
    GENERATED_BODY()

    /** Enable or disable material overrides for this biome. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bEnableMaterialOverride = true;

    /** Enable or disable foliage spawning for this biome. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bEnableFoliage = true;

    /** Override the flat/top-surface material for this biome. Null = use global MasterFlatMaterial. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite,
        meta=(EditCondition="bEnableMaterialOverride"))
    TObjectPtr<UMaterialInterface> FlatMaterialOverride = nullptr;

    /** Override the slope/cliff material for this biome. Null = use global MasterSlopeMaterial. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite,
        meta=(EditCondition="bEnableMaterialOverride"))
    TObjectPtr<UMaterialInterface> SlopeMaterialOverride = nullptr;

    /** Minimum biome weight [0-1] before material override activates (0 = always, 0.5 = dominant only). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite,
        meta=(EditCondition="bEnableMaterialOverride", ClampMin="0.0", ClampMax="1.0"))
    float MaterialOverrideThreshold = 0.4f;

    /**
     * Foliage layers for this biome.
     * Click + to add a new entry. Each entry is one mesh type (tree, grass, rock, flower…)
     * with its own spawn probability, scale, slope filter, and height range.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite,
        meta=(EditCondition="bEnableFoliage"))
    TArray<FVoxelFoliageEntry> FoliageTypes;
};

// ============================================================
//  GLOBAL WATER CONFIG
//  World-wide water settings: ocean, rivers, swimming physics.
//  Individual biome water behaviours live in FVoxelBiomeWaterConfig.
// ============================================================
USTRUCT(BlueprintType)
struct FVoxelGlobalWaterConfig
{
    GENERATED_BODY()

    // --- Ocean ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Ocean",
        meta=(ToolTip="Enable the implicit ocean that fills all terrain below SeaLevel."))
    bool bEnableOcean = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Ocean",
        meta=(ToolTip="Water material used for the ocean surface. Assign a translucent water material here."))
    TObjectPtr<UMaterialInterface> OceanMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Ocean",
        meta=(ToolTip="Base tint multiplied onto the ocean material. Use to shift the ocean from tropical turquoise to arctic grey."))
    FLinearColor OceanSurfaceColor = FLinearColor(0.08f, 0.32f, 0.72f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Ocean",
        meta=(ClampMin="0.0", ClampMax="200.0",
              ToolTip="Peak-to-trough wave height on the ocean surface (cm). 0 = flat calm."))
    float OceanWaveAmplitude = 30.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Ocean",
        meta=(ClampMin="0.0", ClampMax="5.0",
              ToolTip="Speed multiplier for ocean wave animation. 1.0 = normal swell."))
    float OceanWaveSpeed = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Ocean",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="Opacity of deep ocean water. 0 = fully transparent, 1 = opaque abyss."))
    float OceanDeepOpacity = 0.92f;

    // --- Rivers ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Rivers",
        meta=(ToolTip="Enable procedural river generation. Rivers are carved into terrain at world generation time."))
    bool bEnableRivers = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Rivers",
        meta=(ClampMin="0", ClampMax="32",
              ToolTip="Number of river sources to attempt seeding per large world region."))
    int32 NumRiverSources = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Rivers",
        meta=(ClampMin="0.0",
              ToolTip="Minimum terrain height (cm) above SeaLevel where a river source can spawn. Too low = rivers start at sea."))
    float MinRiverSourceAltitude = 4000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Rivers",
        meta=(ClampMin="100.0",
              ToolTip="Base half-width of a new river channel at its source (cm)."))
    float BaseRiverWidth = 400.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Rivers",
        meta=(ClampMin="50.0",
              ToolTip="Depth a river carves below the surrounding terrain at its source (cm). Increases toward the mouth."))
    float BaseRiverDepth = 300.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Rivers",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="Fractional noise applied to river width so banks are organic, not perfectly straight."))
    float RiverWidthNoise = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Rivers",
        meta=(ClampMin="0.0", ClampMax="5.0",
              ToolTip="How much the river widens from source to mouth as a multiplier. 2.5 = mouth is 2.5x the source width."))
    float RiverMouthWidenMultiplier = 2.5f;

    // --- Simulation ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Simulation",
        meta=(ClampMin="1", ClampMax="16",
              ToolTip="Maximum number of water chunks simulated per game tick. Higher = faster propagation but more CPU per frame."))
    int32 MaxSimChunksPerTick = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Simulation",
        meta=(ClampMin="0.05", ClampMax="2.0",
              ToolTip="Seconds between water simulation steps. Lower = faster flow but more CPU."))
    float SimStepInterval = 0.15f;

    // --- Swimming / Gameplay ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Swimming",
        meta=(ToolTip="Allow the player character to enter swim mode when submerged."))
    bool bEnableSwimming = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Swimming",
        meta=(ClampMin="0.5", ClampMax="2.0",
              ToolTip="Default buoyancy applied to the player in water. Values above 1.0 push the player toward the surface."))
    float DefaultBuoyancy = 1.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Swimming",
        meta=(ClampMin="50.0",
              ToolTip="Default maximum swim speed (cm/s)."))
    float DefaultMaxSwimSpeed = 320.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Swimming",
        meta=(ClampMin="0.0",
              ToolTip="Minimum water depth (cm) required before the player transitions to swim mode."))
    float SwimTransitionDepth = 80.f;
};

// ============================================================
//  PER-BIOME WATER CONFIG
//  Controls lake spawning, river behaviour, water appearance,
//  and gameplay properties for a single biome.
//  One instance exists for each surface biome + Skylands.
// ============================================================
USTRUCT(BlueprintType)
struct FVoxelBiomeWaterConfig
{
    GENERATED_BODY()

    // --- Lakes ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Lakes",
        meta=(ToolTip="Allow lakes to form in terrain depressions of this biome."))
    bool bEnableLakes = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Lakes",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="Probability that a detected terrain depression becomes a lake. 0 = never, 1 = always."))
    float LakeSpawnProbability = 0.45f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Lakes",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="How full the lake is relative to its basin rim. 0.85 = 85% full, leaving a narrow dry beach."))
    float LakeFillLevel = 0.85f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Lakes",
        meta=(ClampMin="0.0",
              ToolTip="Minimum depression depth (cm) before a lake can form. Prevents puddles on nearly-flat terrain."))
    float LakeMinDepth = 250.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Lakes",
        meta=(ClampMin="0.0",
              ToolTip="Maximum lake depth (cm). Basins deeper than this are artificially floored."))
    float LakeMaxDepth = 4000.f;

    // --- Rivers ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Rivers",
        meta=(ToolTip="Allow rivers to flow through this biome. Disabling prevents river carving and water source placement in this biome."))
    bool bEnableRivers = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Rivers",
        meta=(ClampMin="0.0", ClampMax="4.0",
              ToolTip="Multiplier on the global BaseRiverWidth for rivers passing through this biome. >1 = wider rivers."))
    float RiverWidthMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Rivers",
        meta=(ClampMin="0.0", ClampMax="4.0",
              ToolTip="Multiplier on the global BaseRiverDepth for rivers in this biome. >1 = deeper channels."))
    float RiverDepthMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Rivers",
        meta=(ClampMin="0.0", ClampMax="3.0",
              ToolTip="How fast river current flows in this biome, as an animation speed multiplier. 0 = still pool, 2 = rapids."))
    float RiverCurrentSpeed = 1.0f;

    // --- Appearance ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Appearance",
        meta=(ToolTip="Override the water material for this biome's water surfaces. Null = use the global OceanMaterial."))
    TObjectPtr<UMaterialInterface> WaterMaterialOverride = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Appearance",
        meta=(ToolTip="Color tint multiplied on top of the water material. Use to shift from blue to murky green or lava red."))
    FLinearColor SurfaceColorTint = FLinearColor(1.f, 1.f, 1.f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Appearance",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="Water clarity. 0 = crystal clear (alpine lake), 1 = completely opaque (swamp/lava)."))
    float Turbidity = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Appearance",
        meta=(ClampMin="0.0", ClampMax="200.0",
              ToolTip="Wave height on the water surface mesh (cm). 0 = glassy still."))
    float SurfaceWaveAmplitude = 15.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Appearance",
        meta=(ClampMin="0.0", ClampMax="5.0",
              ToolTip="Wave animation speed multiplier for this biome's water."))
    float SurfaceWaveSpeed = 1.0f;

    // --- Shoreline ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Shoreline",
        meta=(ToolTip="Enable foam / surf at the water's edge where it meets dry terrain."))
    bool bEnableShorelineFoam = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Shoreline",
        meta=(ClampMin="0.0",
              ToolTip="Width of the foam band at shorelines (cm)."))
    float ShorelineFoamWidth = 120.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Shoreline",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="Opacity of the shoreline foam. 0 = invisible, 1 = fully opaque white surf."))
    float ShorelineFoamOpacity = 0.75f;

    // --- Gameplay ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Gameplay",
        meta=(ToolTip="Water in this biome is toxic. Deals damage per second to the player while submerged."))
    bool bIsToxic = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Gameplay",
        meta=(ClampMin="0.0",
              ToolTip="Damage per second dealt to the player while swimming in toxic water. Ignored when bIsToxic is false."))
    float ToxicDamagePerSecond = 5.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Gameplay",
        meta=(ToolTip="Treat this water as lava: massively higher damage, bright glow, no swimming."))
    bool bIsLava = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Gameplay",
        meta=(ClampMin="0.0",
              ToolTip="Damage per second from lava. Ignored when bIsLava is false."))
    float LavaDamagePerSecond = 50.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Gameplay",
        meta=(ClampMin="0.1", ClampMax="3.0",
              ToolTip="Swim speed multiplier relative to the global default. <1 = sluggish (swamp), >1 = fast (clear alpine stream)."))
    float SwimmingSpeedMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water|Gameplay",
        meta=(ClampMin="0.5", ClampMax="2.0",
              ToolTip="Buoyancy override for this biome. Higher = player floats more. 1.0 = global default."))
    float BuoyancyOverride = 1.0f;
};

// Helper: build a desert-biome water config (oasis — rare, warm, murky)
static FVoxelBiomeWaterConfig MakeDesertWaterDefaults()
{
    FVoxelBiomeWaterConfig C;
    C.LakeSpawnProbability   = 0.06f;  // Very rare — only oases
    C.LakeFillLevel          = 0.60f;  // Shallow, evaporating
    C.LakeMinDepth           = 100.f;  // Even shallow pans count
    C.bEnableRivers          = false;  // No surface rivers in dry desert
    C.SurfaceColorTint       = FLinearColor(0.78f, 0.65f, 0.30f, 1.f); // Sandy warm tint
    C.Turbidity              = 0.55f;  // Murky from sand
    C.SurfaceWaveAmplitude   = 4.f;    // Very still
    C.SurfaceWaveSpeed       = 0.3f;
    C.bEnableShorelineFoam   = false;  // No surf, just dry sand meeting water
    C.SwimmingSpeedMultiplier = 0.85f; // Slightly sluggish — warm thick water
    return C;
}

// Helper: build a peaks-biome water config (alpine lakes, glacial streams)
static FVoxelBiomeWaterConfig MakePeaksWaterDefaults()
{
    FVoxelBiomeWaterConfig C;
    C.LakeSpawnProbability   = 0.55f;
    C.LakeFillLevel          = 0.90f;
    C.LakeMinDepth           = 300.f;  // Only real basins fill
    C.LakeMaxDepth           = 8000.f; // Deep mountain tarns
    C.bEnableRivers          = true;
    C.RiverWidthMultiplier   = 0.65f;  // Narrow glacial streams
    C.RiverDepthMultiplier   = 0.80f;
    C.RiverCurrentSpeed      = 2.0f;   // Fast rapids
    C.SurfaceColorTint       = FLinearColor(0.72f, 0.88f, 1.0f, 1.f); // Ice-blue
    C.Turbidity              = 0.04f;  // Crystal clear
    C.SurfaceWaveAmplitude   = 8.f;
    C.SurfaceWaveSpeed       = 1.8f;
    C.SwimmingSpeedMultiplier = 1.15f; // Easy to swim — cold buoyant water
    C.BuoyancyOverride       = 1.15f;
    return C;
}

// Helper: build a cliffs-biome water config (gorge rivers, waterfall pools)
static FVoxelBiomeWaterConfig MakeCliffsWaterDefaults()
{
    FVoxelBiomeWaterConfig C;
    C.bEnableLakes           = false;  // No still lakes — gorges drain
    C.bEnableRivers          = true;
    C.RiverWidthMultiplier   = 0.75f;  // Narrow gorge channels
    C.RiverDepthMultiplier   = 1.80f;  // Deep cut into rock
    C.RiverCurrentSpeed      = 2.4f;   // Very fast — waterfall country
    C.SurfaceColorTint       = FLinearColor(0.55f, 0.75f, 0.85f, 1.f); // Grey-blue
    C.Turbidity              = 0.30f;  // Aerated white water
    C.SurfaceWaveAmplitude   = 35.f;   // Choppy
    C.SurfaceWaveSpeed       = 2.5f;
    C.bEnableShorelineFoam   = true;
    C.ShorelineFoamWidth     = 200.f;  // Wide spray zone
    C.ShorelineFoamOpacity   = 0.90f;
    C.SwimmingSpeedMultiplier = 0.70f; // Hard to swim against current
    return C;
}

// Helper: build a mesa-biome water config (rare plateau pockets, dry overall)
static FVoxelBiomeWaterConfig MakeMesaWaterDefaults()
{
    FVoxelBiomeWaterConfig C;
    C.LakeSpawnProbability   = 0.10f;  // Rare plateau pockets
    C.LakeFillLevel          = 0.50f;  // Half-full — evaporating pools
    C.LakeMinDepth           = 150.f;
    C.bEnableRivers          = false;
    C.SurfaceColorTint       = FLinearColor(0.70f, 0.48f, 0.28f, 1.f); // Reddish-brown
    C.Turbidity              = 0.45f;  // Sediment-laden
    C.SurfaceWaveAmplitude   = 3.f;    // Nearly still
    C.SurfaceWaveSpeed       = 0.25f;
    C.bEnableShorelineFoam   = false;
    C.SwimmingSpeedMultiplier = 0.80f;
    return C;
}

// Helper: build a craters-biome water config (bowl fills completely — crater lakes)
static FVoxelBiomeWaterConfig MakeCratersWaterDefaults()
{
    FVoxelBiomeWaterConfig C;
    C.LakeSpawnProbability   = 0.92f;  // Craters almost always fill
    C.LakeFillLevel          = 0.95f;  // Nearly to the rim
    C.LakeMinDepth           = 200.f;
    C.LakeMaxDepth           = 3000.f;
    C.bEnableRivers          = false;
    C.SurfaceColorTint       = FLinearColor(0.20f, 0.45f, 0.35f, 1.f); // Deep teal-green
    C.Turbidity              = 0.50f;  // Mysterious, mineral-rich
    C.SurfaceWaveAmplitude   = 10.f;
    C.SurfaceWaveSpeed       = 0.6f;
    C.bEnableShorelineFoam   = true;
    C.ShorelineFoamWidth     = 80.f;
    C.SwimmingSpeedMultiplier = 0.90f;
    return C;
}

// ============================================================
//  MASTER GENERATION CONFIG
//  This is the root struct that drives the entire world.
//  Exposed on AVoxelWorld and on UVoxelBiomeDataAsset presets.
// ============================================================
USTRUCT(BlueprintType)
struct FVoxelGenerationConfig
{
    GENERATED_BODY()

    // Initialise per-biome water configs with their tuned defaults.
    // Forest and Skylands use the generic FVoxelBiomeWaterConfig defaults (clear blue lakes, normal rivers).
    // The four non-default biomes get their own helper-built configs.
    FVoxelGenerationConfig()
        : DesertWater (MakeDesertWaterDefaults())
        , PeaksWater  (MakePeaksWaterDefaults())
        , CliffsWater (MakeCliffsWaterDefaults())
        , MesaWater   (MakeMesaWaterDefaults())
        , CratersWater(MakeCratersWaterDefaults())
    {}

    // --- Global ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Global",
        meta=(ToolTip="World seed. Any integer change produces a completely different world layout."))
    int32 Seed = 1337;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Global",
        meta=(ToolTip="World Z of the water/sea plane. Terrain at this Z is 'sea level' (cm)."))
    float SeaLevel = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Global",
        meta=(ClampMin="10.0",
              ToolTip="How fast density transitions from solid to air at the surface (cm). Smaller = sharper terrain surface."))
    float SurfaceGradientScale = 500.f;

    // --- Biome blending ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Biome Blend",
        meta=(ShowOnlyInnerProperties))
    FBiomeBlendConfig BiomeBlend;

    // --- Surface layer ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SurfaceForest",
        meta=(ShowOnlyInnerProperties))
    FForestBiomeConfig Forest;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SurfaceDesert",
        meta=(ShowOnlyInnerProperties))
    FDesertBiomeConfig Desert;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SurfacePeaks",
        meta=(ShowOnlyInnerProperties))
    FPeaksBiomeConfig Peaks;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SurfaceCliffs",
        meta=(ShowOnlyInnerProperties))
    FCliffsBiomeConfig Cliffs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SurfaceMesa",
        meta=(ShowOnlyInnerProperties))
    FMesaBiomeConfig Mesa;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SurfaceCraters",
        meta=(ShowOnlyInnerProperties))
    FCraterBiomeConfig Craters;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SurfaceOverhangs",
        meta=(ShowOnlyInnerProperties))
    FOverhangConfig Overhangs;

    // --- Skylands layer ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SkylandsLayer",
        meta=(ShowOnlyInnerProperties))
    FSkylandsLayerConfig SkylandsLayer;

    // --- Cave layer ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CaveTunnels",
        meta=(ShowOnlyInnerProperties))
    FCaveTunnelsConfig CaveTunnels;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CaveCrystals",
        meta=(ShowOnlyInnerProperties))
    FCrystalCavernsConfig CaveCrystals;

    // --- Performance ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Performance",
        meta=(ShowOnlyInnerProperties))
    FVoxelPerformanceConfig Performance;

    // --- Global Water ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Water",
        meta=(ShowOnlyInnerProperties,
              ToolTip="World-wide water settings: ocean, rivers, swimming. Per-biome water tuning lives in the WaterBiome sections below."))
    FVoxelGlobalWaterConfig Water;

    // --- Per-biome water ---
    // Each section controls lake probability, river width/depth, water color,
    // turbidity, shoreline foam, and gameplay effects for that specific biome.
    // Expand the section to see all options. Defaults are tuned per biome character.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="WaterBiome|Forest",
        meta=(ShowOnlyInnerProperties,
              ToolTip="Forest: moderate lake probability, clear rivers, blue water with gentle foam."))
    FVoxelBiomeWaterConfig ForestWater;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="WaterBiome|Desert",
        meta=(ShowOnlyInnerProperties,
              ToolTip="Desert: rare oasis lakes, no rivers, warm murky sandy water, no foam."))
    FVoxelBiomeWaterConfig DesertWater;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="WaterBiome|Peaks",
        meta=(ShowOnlyInnerProperties,
              ToolTip="Peaks: high probability alpine tarns, narrow fast glacial rivers, ice-blue crystal-clear water."))
    FVoxelBiomeWaterConfig PeaksWater;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="WaterBiome|Cliffs",
        meta=(ShowOnlyInnerProperties,
              ToolTip="Cliffs: no still lakes, deep fast gorge rivers, choppy aerated water with heavy shoreline spray."))
    FVoxelBiomeWaterConfig CliffsWater;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="WaterBiome|Mesa",
        meta=(ShowOnlyInnerProperties,
              ToolTip="Mesa: rare drying plateau pools, no rivers, reddish-brown sediment-laden water."))
    FVoxelBiomeWaterConfig MesaWater;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="WaterBiome|Craters",
        meta=(ShowOnlyInnerProperties,
              ToolTip="Craters: bowl almost always fills to the rim, deep teal-green mineral water."))
    FVoxelBiomeWaterConfig CratersWater;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="WaterBiome|Skylands",
        meta=(ShowOnlyInnerProperties,
              ToolTip="Skylands: small pools on island tops, no rivers, pure clear water."))
    FVoxelBiomeWaterConfig SkylandsWater;

    // ---- Per-biome rendering (populated at runtime by AVoxelWorld) ----
    // These are NOT exposed as UPROPERTYs here to avoid duplication with the
    // top-level biome sections on the VoxelWorld actor.
    // AVoxelWorld::ConfigureChunk() writes them before each chunk is generated.
    FVoxelBiomeRenderConfig ForestRender;
    FVoxelBiomeRenderConfig DesertRender;
    FVoxelBiomeRenderConfig PeaksRender;
    FVoxelBiomeRenderConfig CliffsRender;
    FVoxelBiomeRenderConfig MesaRender;
    FVoxelBiomeRenderConfig CratersRender;

    // Skylands is not a surface biome (no EVoxelBiome entry) so it lives here
    // as a first-class render config, separate from GetBiomeRender().
    // Materials override the island surface; foliage populates island tops.
    FVoxelBiomeRenderConfig SkylandsRender;

    /** Returns the render config for a given biome enum value. */
    const FVoxelBiomeRenderConfig& GetBiomeRender(EVoxelBiome Biome) const
    {
        switch (Biome)
        {
        case EVoxelBiome::Peaks:   return PeaksRender;
        case EVoxelBiome::Cliffs:  return CliffsRender;
        case EVoxelBiome::Mesa:    return MesaRender;
        case EVoxelBiome::Craters: return CratersRender;
        case EVoxelBiome::Desert:  return DesertRender;
        default:                   return ForestRender;   // EVoxelBiome::Forest
        }
    }

    /** Returns the water config for a given biome enum value. */
    const FVoxelBiomeWaterConfig& GetBiomeWater(EVoxelBiome Biome) const
    {
        switch (Biome)
        {
        case EVoxelBiome::Peaks:   return PeaksWater;
        case EVoxelBiome::Cliffs:  return CliffsWater;
        case EVoxelBiome::Mesa:    return MesaWater;
        case EVoxelBiome::Craters: return CratersWater;
        case EVoxelBiome::Desert:  return DesertWater;
        default:                   return ForestWater;   // EVoxelBiome::Forest
        }
    }

    /**
     * Returns a deterministic world-space offset derived from Seed.
     * Kept within ±50000 range to prevent float precision loss in Perlin noise.
     */
    FORCEINLINE FVector GetSeedOffset() const
    {
        // LCG hash per axis — range extended from ±32768 to ±131071 cm (4x wider).
        // The old 16-bit range meant seeds that differed only in low bits would sample
        // very nearby noise regions and produce visually similar worlds.
        // 18 bits (±131071) at typical biome frequency 0.0001 gives ~13 full noise
        // periods of separation between seeds — effectively a unique world per seed.
        const int32 Hx = Seed * 1664525  + 1013904223;
        const int32 Hy = Seed * 22695477 + 1;
        const int32 Hz = Seed * 214013   + 2531011;
        // 0x3FFFF = 262143  |  0x1FFFF = 131071  →  range [-131071, 131072]
        const float Sx = (float)((Hx & 0x3FFFF) - 0x1FFFF);
        const float Sy = (float)((Hy & 0x3FFFF) - 0x1FFFF);
        const float Sz = (float)((Hz & 0x3FFFF) - 0x1FFFF);
        return FVector(Sx, Sy, Sz);
    }
};
