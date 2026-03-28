// =============================================================================
// VoxelGenerationConfig.h
// =============================================================================
//
// ROOT CONFIGURATION for the FirstVoxel 3-layer procedural world generator.
// Everything that controls how the world looks lives here or in one of the
// sub-config headers this file includes.
//
// ── FILE LAYOUT ──────────────────────────────────────────────────────────────
//  Included sub-headers (edit those files for layer-specific tweaks):
//    Config/SurfaceBiomesConfig.h  — per-biome height-field parameters
//    Config/SkylandsLayerConfig.h  — floating island system
//    Config/CaveLayerConfig.h      — worm tunnels + crystal caverns
//
//  Types defined in THIS file (VoxelGenerationConfig.h):
//    FVoxelPerformanceConfig     — octave caps, optional feature toggles
//    FVoxelFoliageEntry          — one instanced-mesh scatter slot
//    FVoxelBiomeRenderConfig     — material overrides + foliage list per biome
//    FVoxelGlobalWaterConfig     — ocean, rivers, swimming (world-wide)
//    FVoxelBiomeWaterConfig      — per-biome water tuning
//    FVoxelGenerationConfig      — MASTER struct (contains all of the above)
//
// ── THREE-LAYER ARCHITECTURE ─────────────────────────────────────────────────
//
//  ┌─ SURFACE LAYER ───────────────────────────────────────────────────────┐
//  │  2D height-field blending six biomes (Forest/Peaks/Cliffs/Mesa/       │
//  │  Craters/Desert) weighted by temperature × erosion noise.             │
//  │  Evaluated once per XY column. Driven by SurfaceBiomesConfig.h.       │
//  └───────────────────────────────────────────────────────────────────────┘
//  ┌─ SKYLANDS LAYER ──────────────────────────────────────────────────────┐
//  │  Floating islands whose altitude, size and probability scale with     │
//  │  the terrain height and roughness directly below them.                │
//  │  Driven by SkylandsLayerConfig.h.                                     │
//  └───────────────────────────────────────────────────────────────────────┘
//  ┌─ CAVE LAYER ──────────────────────────────────────────────────────────┐
//  │  Two-tunnel worm noise carves passages below the surface.             │
//  │  Crystal cavern chambers open up at greater depth.                    │
//  │  Protected by a bedrock floor. Driven by CaveLayerConfig.h.           │
//  └───────────────────────────────────────────────────────────────────────┘
//
// ── HOW TO TWEAK THE WORLD ───────────────────────────────────────────────────
//  1. Select the AVoxelWorld actor in the Level Editor.
//  2. Expand the "Voxel|Generation" section in the Details panel.
//  3. Edit parameters directly, or assign a UVoxelBiomeDataAsset preset.
//  4. Click "Generate World" (Details panel → Voxel category) to preview.
//
//  To create a new world preset:
//    Content Browser → Right-click → Miscellaneous → Data Asset
//    → pick UVoxelBiomeDataAsset → configure → assign to BiomePreset.
//
// ── PERFORMANCE QUICK-REFERENCE ──────────────────────────────────────────────
//  Halve generation time  : Performance.MaxNoiseOctaves = 2
//  Disable overhangs      : Performance.bEnableOverhangs = false
//  Cheaper skylands       : Performance.bEnable3DSkylandNoise = false
//  Reduce streaming load  : AVoxelWorld.RenderDistanceXY (lower = faster)
//  Limit GPU triangles    : FVoxelGeneratorTask::MaxMeshesPerChunk (15000)
//
// ── SEED SYSTEM ──────────────────────────────────────────────────────────────
//  GenerationConfig.Seed   drives GetSeedOffset() → a 3D float offset that
//  is added to every Perlin noise call. Changing the seed shifts all noise
//  fields simultaneously, producing a completely different world layout.
//  GetSeedOffset() uses three independent LCG hashes (one per axis) to
//  spread seeds uniformly across a ±131 071 cm range — enough for ~13 full
//  noise periods at typical biome frequency (0.0001), so every integer seed
//  produces a visually distinct world.
// =============================================================================

#pragma once
#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
// EVoxelBiome is needed by GetBiomeRender() in FVoxelGenerationConfig
#include "Voxel/Biomes/VoxelBiome.h"

// --- Modular Configurations (Phase 1 Refactoring) ---
#include "SurfaceBiomesConfig.h"
#include "SkylandsLayerConfig.h"
#include "CaveLayerConfig.h"

#include "VoxelGenerationConfig.generated.h"

// ============================================================
//  SURFACE — OVERHANG CONFIG
// ============================================================

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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug",
        meta=(ToolTip="Toggle generating Skylands structure overlay."))
    bool bEnableSkylands = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug",
        meta=(ToolTip="Toggle generating subterranean Worm tunnels and chambers."))
    bool bEnableCaves = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug",
        meta=(ToolTip="Toggle generating Crater biome layouts across surface levels."))
    bool bEnableCraters = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug",
        meta=(ToolTip="Toggle the entire base Surface Layer (Turning off leaves only Skylands if enabled)."))
    bool bEnableSurface = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug")
    bool bEnableForest = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug")
    bool bEnableDesert = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug")
    bool bEnablePeaks = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug")
    bool bEnableCliffs = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug")
    bool bEnableMesa = true;
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="General")
    TObjectPtr<UStaticMesh> Mesh = nullptr;

    /** Per-triangle spawn probability [0..1].  0.03 = sparse  |  0.2 = lush. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="General", meta=(ClampMin="0.0", ClampMax="1.0"))
    float SpawnChance = 0.05f;

    /** Spawn attempts per triangle (1 = normal, 2-8 = dense cluster). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="General", meta=(ClampMin="1", ClampMax="8"))
    int32 SpawnAttemptsPerTriangle = 1;

    /** Min biome weight [0-1] required at spawn point (0 = always, 0.5 = dominant biome only). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filter", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MinBiomeWeight = 0.2f;

    /** Min surface flatness (Normal.Z): 1.0 = flat only | 0.7 = up to 45 deg | 0.0 = any slope. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filter", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MinSlopeAlignment = 0.7f;

    /** Min world Z (cm) for spawning. Set to SeaLevel to block underwater spawns. (-9999999 = no limit) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounds")
    float MinWorldZ = -9999999.f;

    /** Max world Z (cm) for spawning. Use to cap high-altitude vegetation. (9999999 = no limit) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounds")
    float MaxWorldZ = 9999999.f;

    /** Min random scale multiplier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform", meta=(ClampMin="0.01"))
    float ScaleMin = 0.8f;

    /** Max random scale multiplier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform", meta=(ClampMin="0.01"))
    float ScaleMax = 1.2f;

    /** Randomise yaw per instance. Disable for directional props (signs, fences). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform")
    bool bRandomYaw = true;

    /** Fixed yaw in degrees when bRandomYaw = false. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform", meta=(EditCondition="!bRandomYaw", ClampMin="0.0", ClampMax="360.0"))
    float FixedYaw = 0.f;

    /** Align instance up-axis to the surface normal (good for cliff plants / mushrooms). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform")
    bool bAlignToSurface = false;

    /** Z offset after placement (cm). Negative = sink into terrain. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Transform")
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
        meta=(EditCondition="bEnableFoliage", TitleProperty="Mesh"))
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
        meta=(ToolTip="Use voxel-based ocean instead of flat static mesh. Seamless with terrain but may impact horizon visuals."))
    bool bUseVoxelOcean = false;

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
/**
 * @struct FVoxelGenerationConfig
 * @brief Global parameter set for the procedural generation pipeline.
 *
 * This structure contains all seeds, frequencies, amplitudes, and sub-struct 
 * configs (Biomes, Craters, Skylands, Water) required to build the world. 
 * Pass this as a const reference to any generation function to ensure 
 * deterministic output.
 */
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

    /**
     * Dot-product threshold for flat vs slope face classification.
     * abs(FaceNormal.Z) >= SlopeThreshold → FlatMesh (grass/dirt material).
     * abs(FaceNormal.Z) <  SlopeThreshold → SlopeMesh (cliff/rock material).
     * Default 0.7 ≈ 45°. Raise toward 1.0 for more faces classified as slope.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Global",
        meta=(ClampMin="0.0", ClampMax="1.0",
              ToolTip="Normal.Z threshold for flat vs slope face material split. 0.7 = ~45 degrees."))
    float SlopeThreshold = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Global",
        meta=(ToolTip="World Z of the water/sea plane. Terrain at this Z is 'sea level' (cm)."))
    float SeaLevel = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Global",
        meta=(ClampMin="10.0",
              ToolTip="How fast density transitions from solid to air at the surface (cm). Smaller = sharper terrain surface."))
    float SurfaceGradientScale = 2500.f;  // FIX: was 1000 — too tight, made spike transitions abrupt

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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Water",
        meta=(ShowOnlyInnerProperties,
              ToolTip="World-wide water settings: ocean, rivers, swimming. Per-biome water tuning lives in the WaterBiome sections below."))
    FVoxelGlobalWaterConfig Water;

    // ---- Per-biome water (populated at runtime by AVoxelWorld) ----
    // These are NOT exposed as UPROPERTYs here to avoid duplication with the
    // per-biome water sections on the VoxelWorld actor.
    // AVoxelWorld::ConfigureChunk() writes them before each chunk is generated.
    FVoxelBiomeWaterConfig ForestWater;
    FVoxelBiomeWaterConfig DesertWater;
    FVoxelBiomeWaterConfig PeaksWater;
    FVoxelBiomeWaterConfig CliffsWater;
    FVoxelBiomeWaterConfig MesaWater;
    FVoxelBiomeWaterConfig CratersWater;
    FVoxelBiomeWaterConfig OceanWater;
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
    FVoxelBiomeRenderConfig OceanRender;

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
        case EVoxelBiome::Ocean:   return OceanRender;
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
        case EVoxelBiome::Ocean:   return OceanWater;
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
