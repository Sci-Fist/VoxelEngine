// VoxelBiomeGenerators.cpp
// Shape-function implementations for all surface biomes and the skylands /
// crystal cavern layers.

#include "VoxelBiomeGenerators.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "VoxelBiomeManager.h"

// ============================================================
//  FBM
// ============================================================
float FVoxelBiomeGenerators::FBM(float X, float Y, float Z, int32 Octaves,
                                 float Lacunarity, float Gain,
                                 int32 MaxOctaves) {
  const int32 ActualOctaves =
      FMath::Clamp(FMath::Min(Octaves, MaxOctaves), 1, 16);
  float Value = 0.f, Amp = 0.5f, Freq = 1.f;
  for (int32 i = 0; i < ActualOctaves; ++i) {
    Value += FastNoise3D(X * Freq, Y * Freq, Z * Freq) * Amp;
    Freq *= Lacunarity;
    Amp *= Gain;
  }
  return Value;
}

// ============================================================
//  FOREST - rolling hills and plains
// ============================================================
float FVoxelBiomeGenerators::GetForestHeight(
    float X, float Y, const FVoxelGenerationConfig &Config) {
  const FForestBiomeConfig &FC = Config.Forest;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y;

  float Base = FBM(nX * FC.NoiseFrequency, nY * FC.NoiseFrequency, 0.f,
                   FC.Octaves, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);

  float Normalized = (Base + 1.f) * 0.5f;
  float Detail =
      FastNoise3D(nX * FC.DetailFrequency, nY * FC.DetailFrequency, 0.f) *
      FC.DetailAmplitude;

  return Config.SeaLevel + FMath::Lerp(FC.HeightMin, FC.HeightMax, Normalized) +
         Detail;
}

// ============================================================
//  DESERT - rolling sand dunes
// ============================================================
float FVoxelBiomeGenerators::GetDesertHeight(
    float X, float Y, const FVoxelGenerationConfig &Config) {
  const FDesertBiomeConfig &DC = Config.Desert;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y;

  float Base = FBM(nX * DC.NoiseFrequency, nY * DC.NoiseFrequency, 40.f,
                   DC.Octaves, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);

  float Normalized = (Base + 1.f) * 0.5f;
  float Shaped = FMath::Pow(FMath::Max(0.f, Normalized), DC.Sharpness);

  float Detail =
      FastNoise3D(nX * DC.RippleFrequency, nY * DC.RippleFrequency, 0.f) *
      DC.RippleAmplitude;

  return Config.SeaLevel + FMath::Lerp(DC.HeightMin, DC.HeightMax, Shaped) +
         Detail;
}

// ============================================================
//  PEAKS - dramatic alpine mountains
//
//  FIX: Reduced domain warp magnitude from 2000 to 1000 and clamped Shaped
//  to [0,1] before the height lerp.  The old 2000cm warp could fold the noise
//  field back on itself at sharp warp boundaries, creating local normals that
//  shot straight up — the vertical spike artifact.
// ============================================================
float FVoxelBiomeGenerators::GetPeaksHeight(
    float X, float Y, const FVoxelGenerationConfig &Config) {
  const FPeaksBiomeConfig &PC = Config.Peaks;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y;

  const float WF = 0.0002f;
  // FIX: Disabled Domain Warping entirely on Peaks/Cliffs because 
  // infinite-slope creases produce mesh folds that generate spikes on the ground.
  const float WarpX = 0.f; 
  const float WarpY = 0.f; 

  float Base = FBM((nX + WarpX) * PC.NoiseFrequency,
                   (nY + WarpY) * PC.NoiseFrequency,
                   10.f, PC.Octaves, 2.0f, 0.5f,
                   Config.Performance.MaxNoiseOctaves);

  float Normalized = (Base + 1.f) * 0.5f;
  float Shaped = FMath::Pow(FMath::Clamp(Normalized, 0.f, 1.f), PC.Sharpness);
  Shaped = FMath::Clamp(Shaped, 0.f, 1.f); // guarantee no overshoot into HeightMax detail

  const float MaxDetail = (PC.HeightMax - PC.HeightMin) * 0.02f; 
  const float Detail = FastNoise3D(nX * PC.NoiseFrequency * 4.f,
                                   nY * PC.NoiseFrequency * 4.f, 0.f)
                       * FMath::Min(PC.DetailAmplitude, MaxDetail);

  return Config.SeaLevel + FMath::Lerp(PC.HeightMin, PC.HeightMax, Shaped) + Detail;
}

// ============================================================
//  CLIFFS - ridged, terraced terrain
//
//  FIX: Replaced the spike-generating ridged noise formula.
//  Old formula:  Ridge = 1 - Abs(FBM)  then  Pow(Ridge, Sharpness)
//  Problem:      FBM zero-crossings produce infinitely thin ridges. At those
//                crossings Ridge=1, everywhere else Ridge<1. Pow() crushes
//                non-ridge values toward 0, leaving only razor-thin spikes
//                regardless of Sharpness value.
//  New formula:  Use a smooth "billow" noise: Abs(FBM) remapped to [0,1].
//                Billowed noise has broad hills with rounded tops, not spikes.
//                Terracing is applied to the remapped value for cliff steps.
// ============================================================
float FVoxelBiomeGenerators::GetCliffsHeight(
    float X, float Y, const FVoxelGenerationConfig &Config) {
  const FCliffsBiomeConfig &CC = Config.Cliffs;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y;

  const float WF = 0.00015f;
  // FIX: Disabled Domain Warping for Cliff ridges to avoid micro-fold spikes on boundary seams.
  const float WarpX = 0.f;
  const float WarpY = 0.f;

  float Base = FBM((nX + WarpX) * CC.NoiseFrequency,
                   (nY + WarpY) * CC.NoiseFrequency,
                   15.f, CC.Octaves, 2.1f, 0.55f,
                   Config.Performance.MaxNoiseOctaves);

  // Billow noise: FMath::Abs(Base) creates broad rounded tops instead of razor ridges
  float Shaped = FMath::Pow(FMath::Abs(Base), CC.Sharpness); 
  Shaped = FMath::Clamp(Shaped, 0.f, 1.f);

  // Terrace: floor-snap to create cliff ledge steps.
  if (CC.TerraceSteps > 0 && CC.TerraceFactor > 0.f)
  {
    const float StepScale = (float)CC.TerraceSteps;
    const float Terrace   = FMath::Floor(Shaped * StepScale) / StepScale;
    Shaped = FMath::Lerp(Shaped, Terrace, CC.TerraceFactor);
  }

  // Detail noise: small-scale surface roughness, scaled relative to height
  // range so it never dominates the overall silhouette.
  const float Detail = FastNoise3D(nX * CC.NoiseFrequency * 6.f,
                                   nY * CC.NoiseFrequency * 6.f, 0.f)
                       * CC.DetailAmplitude;

  return Config.SeaLevel + FMath::Lerp(CC.HeightMin, CC.HeightMax, Shaped) + Detail;
}

// ============================================================
//  MESA - flat-top plateaus
// ============================================================
float FVoxelBiomeGenerators::GetMesaHeight(
    float X, float Y, const FVoxelGenerationConfig &Config) {
  const FMesaBiomeConfig &MC = Config.Mesa;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y;

  // 1. BASE TERRAIN (Desert Floor)
  const float BasePlains = Config.SeaLevel + MC.HeightBase;
  float Height = BasePlains;

  // 2. MESAS & BUTTES FORMATION
  // Mesa Noise - large scale plateaus
  const float MesaNoise = FBM(nX * MC.MesaFrequency, nY * MC.MesaFrequency, 20.f, 4, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);
  const float MesaNorm = (MesaNoise + 1.f) * 0.5f;

  // Butte Noise - small scale isolated structures
  const float ButteNoise = FBM(nX * MC.ButteFrequency, nY * MC.ButteFrequency, 40.f, 3, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);
  const float ButteNorm = (ButteNoise + 1.f) * 0.5f;

  // Combine Plateaus
  const float CombinedProfile = FMath::Max(MesaNorm, ButteNorm);

  // Apply Plateau Stepping for Flat Tops
  const float StepScale = (float)MC.PlateauSteps;
  const float Plateau = FMath::Floor(CombinedProfile * StepScale) / StepScale;
  const float EdgeBlend = FMath::SmoothStep(0.f, 1.f, (CombinedProfile - Plateau) * (MC.EdgeSharpness + 4.0f));
  const float Shape = Plateau + (FMath::Pow(EdgeBlend, 2.0f) / StepScale); // Steep walls
  
  // Height Range for Mesa/Buttes
  const float MesaHeightRange = MC.HeightMax - MC.HeightBase;
  Height += Shape * MesaHeightRange;

  // 3. PILLAR & HOODOO FORMATION (Tall Columns)
  const float PillarNoise = FastNoise3D(nX * MC.PillarFrequency, nY * MC.PillarFrequency, 60.f);
  const float PillarThreshold = 0.65f; // Sparse distribution
  if (PillarNoise > PillarThreshold) {
    const float PillarIntensity = (PillarNoise - PillarThreshold) / (1.f - PillarThreshold);
    float PillarHeight = FMath::Lerp(MC.PillarHeightMin, MC.PillarHeightMax, PillarIntensity);
    
    // Conical Taper: make taller pillars narrower via altitude falloff proxy
    const float RadialDist = FMath::Clamp(1.0f - PillarIntensity, 0.f, 1.f);
    PillarHeight *= FMath::Pow(1.f - (RadialDist * MC.PillarConicalFactor), 1.5f);
    
    // Add Caprock "Hat" bulge
    if (PillarIntensity > 0.85f) {
      PillarHeight += 500.f; // 5m bump for capped rock
    }
    
    Height = FMath::Max(Height, BasePlains + PillarHeight);
  }

  // 4. EROSION CHANNELS (Subtractive Drainage)
  const float ChannelNoise = FastNoise3D(nX * MC.ChannelFrequency, nY * MC.ChannelFrequency, 80.f);
  // Ridged noise: 1 - Abs(noise) creates channel networks
  const float RidgedChannel = 1.0f - FMath::Abs(ChannelNoise);
  const float ChannelThreshold = 0.75f;
  if (RidgedChannel > ChannelThreshold) {
    const float ChannelWeight = (RidgedChannel - ChannelThreshold) / (1.f - ChannelThreshold);
    const float SmoothChannel = FMath::SmoothStep(0.f, 1.f, ChannelWeight);
    Height -= SmoothChannel * MC.ChannelDepth;
  }

  // 5. LAYERED STRATIFICATION (Stepped Terraced Appearance)
  // Operate on absolute Height coordinate output for continuous flat ledge breaks
  const float LayerScale = Height / MC.LayerThickness;
  const float Fraction = LayerScale - FMath::Floor(LayerScale);
  const float Hardness = MC.LayerHardness; // 0.7 = high resistance
  
  // Create Step profile: flat ledge + sharp cliff drop
  if (Fraction > Hardness) {
    // Sharp shelf drop-offs
    const float DropT = (Fraction - Hardness) / (1.0f - Hardness);
    Height += FMath::Sin(DropT * 3.14159f) * 200.f * MC.LayerVariation; // Edge break variation
  } else {
    // Near-flat ledge
    Height += 100.f * MC.LayerVariation; 
  }

  // 6. TALUS SLOPES (Rock debris at base)
  // Debris settles at bases where vertical cliffs meet plains
  if (Height > BasePlains + 2000.f && Shape < 0.3f) {
    const float DebrisNoise = FastNoise3D(nX * MC.TalusFrequency, nY * MC.TalusFrequency, 100.f);
    const float DebrisHeight = (DebrisNoise + 1.f) * 400.f * MC.TalusSpread;
    Height += DebrisHeight;
  }

  // 7. Surface Micro-detail
  float Crackle = FastNoise3D(nX * 0.008f, nY * 0.008f, 0.f) * 160.f;

  return Height + Crackle;
}

// ============================================================
//  CRATERS - hierarchical impact crater system
//
//  NEW HIERARCHICAL SYSTEM: Creates one large central crater with smaller
//  surrounding impacts for a natural impact field appearance.
//
//  DESIGN PRINCIPLES:
//  - Central crater dominates near world origin (0,0)
//  - Secondary craters appear in surrounding area with natural distribution
//  - Distance-based falloff creates radial pattern from center
//  - Steep rim walls for dramatic appearance (RimWidth=0.04 = very steep)
//  - Flat crater floor for proper impact basin
//  - Natural rim erosion for realistic weathering (RimErosion=0.05 = minimal)
//
//  HIERARCHY:
//  1. Central Crater: Large primary impact with deep flat basin and high rim
//     - CentralCraterRadius: 105000cm (1050m diameter) - much larger than before
//     - CentralCraterDepth: -2500cm (25m deep) - reduced for better stability
//     - CentralCraterRimHeight: 2500cm (25m high) - dramatic but stable walls
//     - RimWidth: 0.04 (4% of radius) - extremely thin, dramatic walls
//     - RimNoiseAmplitude: 150cm - increased for more dramatic rim peaks
//     - RimPeakLength: 0.15 - extended curved peaks beyond rim edge
//
//  2. Secondary Craters: Smaller impacts around the primary crater
//     - SecondaryCraterMaxRadius: 36000cm (360m diameter) - tripled from before
//     - SecondaryCraterDensity: 0.9 - high density for surrounding impacts
//     - ImpactFrequency: 0.00015 - moderate density for natural distribution
//
//  3. Tertiary Craters: Very small impacts in the surrounding area
//     - TertiaryCraterMaxRadius: 4000cm (40m diameter) - small variations
//     - TertiaryCraterMinRadius: 1000cm (10m diameter) - fine detail
//     - TertiaryCraterDensity: 0.7 - moderate density for natural appearance
//
//  ── EJECTA SYSTEM (Auswurfmaterial) ────────────────────────────────────────
//  The ejecta system simulates real impact crater ejecta blankets with three main components:
//
//  1. Ejecta Blanket (Ejecta-Decke): Layer of material thrown out during impact,
//     thickest near rim, thinning outward with exponential fade controlled by
//     EjectaFadeExponent. Creates the characteristic "splash" pattern around craters.
//     - EjectaBlanketWidth: 0.25 (25% of crater radius)
//     - EjectaThickness: 0.15 (15% of crater depth)
//     - EjectaFadeExponent: 2.0 (exponential fade)
//
//  2. Ejecta Blocks (Blockfeld): Large angular rock blocks scattered in ejecta zone,
//     generated using high-frequency noise with EjectaBlockFrequency and
//     EjectaBlockAmplitude. Creates dramatic boulder fields and terrain features.
//     - EjectaBlockFrequency: 0.0015 - moderate block density
//     - EjectaBlockAmplitude: 1200cm (12m) - large impact blocks
//     - EjectaBlockSize: 0.08 - size variation control
//
//  3. Overturned Strata (Überkippte Schichten): Bent and folded rock layers at
//     crater edge, showing geological disruption from impact. Generated with
//     OverturnedStrataFrequency and OverturnedStrataAmplitude for realistic
//     geological formations.
//     - OverturnedStrataFrequency: 0.0025 - moderate strata density
//     - OverturnedStrataAmplitude: 800cm (8m) - dramatic strata features
//
//  All ejecta features are concentrated within EjectaBlanketWidth beyond the rim
//  and fade naturally into the surrounding terrain, creating seamless transitions.
//
//  ── RIM ENHANCEMENTS ───────────────────────────────────────────────────────
//  Enhanced rim generation for more dramatic and realistic crater walls:
//
//  - RimWidth: 0.04 (4% of crater radius) - extremely thin walls for dramatic appearance
//  - RimNoiseAmplitude: 150cm - increased from 100cm for more dramatic rim peaks
//  - RimErosion: 0.05 - minimal erosion for sharp, dramatic rims
//  - RimPeakLength: 0.15 - extended curved peaks beyond rim edge for more dramatic appearance
//
//  Rim generation includes:
//  - Curved rim edge noise for random outward/inward pointing edges
//  - Organic wall noise for jagged, rocky appearance
//  - Extended curved rim top edge detail with peak variations
//  - Jagged rim peak border detail for natural irregularity
//  - Rock formations at rim base where floor meets wall
//
//  ── IMPLEMENTATION NOTES ───────────────────────────────────────────────────
//  The algorithm uses distance-based falloff from world center to create hierarchical
//  crater system. Central crater dominates near origin, secondary craters appear in
//  surrounding area with natural distribution patterns, and tertiary craters provide
//  fine detail in outer regions.
//
//  All ejecta features are integrated into the central crater generation loop and
//  automatically scale with crater size for consistent appearance across different
//  impact scales.
// ============================================================
float FVoxelBiomeGenerators::GetCraterHeight(
    float X, float Y, const FVoxelGenerationConfig &Config) {
  const FCraterBiomeConfig &CRC = Config.Craters;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y;

  // Calculate distance from forced crater center for hierarchical system
  const float dx = X - CRC.ForcedCraterCenter.X;
  const float dy = Y - CRC.ForcedCraterCenter.Y;
  const float DistFromCenter = FMath::Sqrt(dx * dx + dy * dy);
  
  // Central crater dominance falloff
  // Central crater dominance falloff: solid inside the rim, fading outside
  const float CenterDistNorm = DistFromCenter / CRC.CentralCraterRadius;
  const float CentralDominance = FMath::SmoothStep(1.3f, 0.85f, CenterDistNorm); 
  
  // Base terrain height
   // Ambient rolling noise for surrounding terrain to prevent flat lands
  const float SurroundNoise = FBM(nX * 0.001f, nY * 0.001f, 0.f, 3, 2.2f, 0.5f) * 1500.f; 
  const float BasePlains = Config.SeaLevel + 4000.f + SurroundNoise; 
  float TotalHeight = BasePlains;

  // 1. CENTRAL CRATER - dominates near origin
  if (DistFromCenter < CRC.CentralCraterRadius * 1.5f) {
    // Central crater shape calculation
    const float NormalizedDist = FMath::Clamp(DistFromCenter / CRC.CentralCraterRadius, 0.f, 1.f);
    
    // Rim zone definitions - WIDER rim for visible walls
    const float RimStart = 0.65f;   
    const float RimEnd = 0.92f;     
    
    // CRATER SHAPE: Create proper impact crater profile
    const float FloorFade = FMath::SmoothStep(RimStart, 0.0f, NormalizedDist);
    const float LocalPlains = Config.SeaLevel + 4000.f + SurroundNoise * (1.0f - FloorFade);
    float CentralHeight = LocalPlains + CRC.CentralCraterDepth; // Start with floor
    
    // Calculate base rim height with minimum constraint - DRAMATIC rim
    const float MinRimHeight = FMath::Abs(CRC.CentralCraterDepth) * 1.5f; 
    const float BaseRimHeight = FMath::Max(CRC.CentralCraterRimHeight, MinRimHeight) * 4.0f; // Heightened rim from 3.0x up to 4.0x    
    // Add random variation to rim height for natural appearance
    const float RimVariation = FastNoise3D(nX * 0.0005f, nY * 0.0005f, 0.f) * 0.3f;
    const float RandomRimHeight = BaseRimHeight * (1.0f + RimVariation * 0.2f); 

    // 1. BASE HEIGHT PROFILE (Continuous branching)
    if (NormalizedDist < RimStart) {
      // Inside rim: BOWL-SHAPED depression like a real meteor crater
      // Gradual slope from rim to center, deeper at the middle
      const float BowlShape = FMath::Pow(1.0f - (NormalizedDist / RimStart), 0.6f);
      const float FloorDepth = LocalPlains + CRC.CentralCraterDepth;
      const float RimBase = LocalPlains + CRC.CentralCraterDepth * 0.3f; // Higher at rim edge
      CentralHeight = FMath::Lerp(RimBase, FloorDepth, BowlShape);
      
      // Add subtle floor variation for natural debris/sediment
      const float FloorNoise = FastNoise3D(nX * 0.003f, nY * 0.003f, 0.f) * 150.f;
      CentralHeight += FloorNoise * (1.0f - BowlShape * 0.5f); // More noise near edges
    } else if (NormalizedDist < RimEnd) {
      // RISING RIM WALL: Gets thinner and curves upward like ejecta slabs
      const float RimT = (NormalizedDist - RimStart) / (RimEnd - RimStart); 
      
      // Rising curve: starts wide at base, narrows and rises to peak
      const float RisingCurve = FMath::Pow(RimT, 0.5f); // Square root for dramatic rise
      
      // Thinning factor: rim gets thinner as it rises (curved slab effect)
      const float ThinningFactor = 1.0f - RimT * 0.6f; // Narrows to 40% at top
      
      // Height rises dramatically toward rim peak
      const float RimPeak = LocalPlains + RandomRimHeight * 1.5f; // 50% taller
      const float FloorDepth = LocalPlains + CRC.CentralCraterDepth;
      CentralHeight = FMath::Lerp(FloorDepth, RimPeak, RisingCurve);
      
      // Add vertical curvature - upper part curves inward (slab effect)
      const float SlabCurve = FMath::Sin(RimT * 3.14159f * 0.5f) * 200.f * ThinningFactor;
      CentralHeight += SlabCurve;
    } else {
      // Outside rim: GRADUAL slope like a real meteor crater rim
      const float DropT = FMath::SmoothStep(RimEnd, RimEnd + 0.25f, NormalizedDist); 
      const float RimPeak = LocalPlains + RandomRimHeight * 1.5f;
      const float DropTarget = LocalPlains + RandomRimHeight * 0.4f; 
      CentralHeight = FMath::Lerp(RimPeak, DropTarget, DropT);
    }

    // --- CONTINUOUS DETAIL OVERLAYS (No Jumps) ---

    // 2. Add Curved Rim Edge (Pointy Outward/Inward)
    const float EdgeNoise = FastNoise3D(nX * 0.003f, nY * 0.003f, 0.f);
    const float EdgeCurve = FMath::Sin(EdgeNoise * 3.14159f) * 500.f; 
    float EdgeFade = 0.f;
    if (NormalizedDist >= RimStart && NormalizedDist <= RimEnd) {
      EdgeFade = (NormalizedDist - RimStart) / (RimEnd - RimStart);
    } else if (NormalizedDist > RimEnd && NormalizedDist < RimEnd + 0.05f) {
      EdgeFade = 1.0f - (NormalizedDist - RimEnd) / 0.05f;
    }
    CentralHeight += EdgeCurve * FMath::SmoothStep(0.f, 1.f, EdgeFade);

    // 3. Organic Wall Noise (Jagged Rocky Wall)
    if (NormalizedDist >= RimStart && NormalizedDist <= RimEnd) {
      const float WallNoise = FastNoise3D(nX * 0.002f, nY * 0.002f, 0.f) * CRC.RimNoiseAmplitude * 0.8f;
      const float FadeT = (NormalizedDist - RimStart) / (RimEnd - RimStart);
      const float WallFade = FMath::SmoothStep(0.f, 0.1f, FadeT); // fade from 0 at RimStart
      CentralHeight += WallNoise * WallFade * FMath::Exp(-FadeT * 10.0f);
    }
    
    // -------------------------------------------------------------------
    // 4. WALL STRUCTURES: Ledges, Buttresses, and Outcrops (Inner Cliff)
    // -------------------------------------------------------------------
    if (NormalizedDist >= RimStart && NormalizedDist <= RimEnd) {
      const float RimT = (NormalizedDist - RimStart) / (RimEnd - RimStart);
      const float Ang = FMath::Atan2(nY, nX);

      // --- 📌 LEDGES: Flat horizontal shelves on the cliff sides ---
      if ((RimT > 0.20f && RimT < 0.35f) || (RimT > 0.55f && RimT < 0.68f)) {
        const float IsLower = (RimT < 0.4f) ? 1.0f : 0.0f;
        const float CenterT = IsLower ? 0.275f : 0.615f;
        const float FadeW = IsLower ? 0.075f : 0.065f;
        const float LedgeFade = FMath::SmoothStep(CenterT - FadeW, CenterT, RimT) * FMath::SmoothStep(CenterT + FadeW, CenterT, RimT);
        const float LedgeHeight = LocalPlains + RandomRimHeight * CenterT;
        CentralHeight = FMath::Lerp(CentralHeight, LedgeHeight, LedgeFade * 0.85f);
      }

      // --- ⛰️ BUTTRESSES: Steep, massive rock masses projecting from the cliff ---
      const float ButtressPos = FMath::Sin(Ang * 12.0f); // 12 pillars around the crater
      const float ButtressNoise = FastNoise3D(nX * 0.004f, nY * 0.004f, 500.f);
      if (ButtressPos > 0.3f && ButtressNoise > 0.1f) {
        const float ButtressFade = FMath::SmoothStep(0.3f, 0.7f, ButtressPos);
        const float ButtressShape = FMath::Sin(RimT * 3.14159f); // Thicker in the middle
        CentralHeight += 1800.f * ButtressFade * ButtressShape; // Prominent projecting mass
      }

      // --- 🌊 OVERHANGS / RIBS (Protrusions due to erosion) ---
      const float RibNoise = FastNoise3D(nX * 0.012f, nY * 0.012f, 300.f);
      if (RibNoise > 0.4f) {
        const float RibFade = FMath::SmoothStep(0.4f, 0.7f, RibNoise);
        CentralHeight += 600.f * RibFade * FMath::Sin(RimT * 3.14159f * 4.0f); // Ribs curving with slope
      }
    }

    // 5. Enhanced Rock Formations at base (Floor-Wall transition)
    const float RockMin = RimStart * 0.75f;
    const float RockMax = RimStart * 1.25f;
    if (NormalizedDist >= RockMin && NormalizedDist <= RockMax) {
      const float CenterDis = (NormalizedDist - RockMin) / (RockMax - RockMin);
      const float RockFade = FMath::SmoothStep(0.f, 0.4f, CenterDis) * FMath::SmoothStep(1.f, 0.6f, CenterDis);
      const float RockNoise = FastNoise3D(nX * 0.006f, nY * 0.006f, 0.f);
      if (RockNoise > 0.10f) {
        CentralHeight += (RockNoise - 0.10f) * 2500.f * RockFade; 
      }
    }

    // 5. Curved Stone Slabs (Ejecta on top)
    if (NormalizedDist >= RimEnd && NormalizedDist <= RimEnd + CRC.RimPeakLength) {
      const float TopNoise = FastNoise3D(nX * 0.012f, nY * 0.012f, 0.f);
      const float CurveDirection = (TopNoise > 0.3f) ? 1.0f : ((TopNoise < -0.3f) ? -1.0f : 0.0f); 
      const float CenterDis = (NormalizedDist - RimEnd) / CRC.RimPeakLength;
      const float SlabFade = FMath::SmoothStep(0.f, 0.1f, CenterDis) * FMath::SmoothStep(1.f, 0.9f, CenterDis);
      if (CurveDirection != 0.0f) {
        const float CurveShape = FMath::Sin(CenterDis * 3.14159f * 4.0f); 
        const float TopCurve = CurveDirection * FMath::Abs(CurveShape) * 6000.f; // 60m tall curves (Multiplied from 18m)
        CentralHeight += TopCurve * SlabFade;
      }
    }

    // 6. Jagged peaks detail outside top edge
    if (NormalizedDist >= RimEnd + 0.02f && NormalizedDist <= RimEnd + 0.07f) {
      const float CenterDis = (NormalizedDist - (RimEnd + 0.02f)) / 0.05f;
      const float Fade = FMath::SmoothStep(0.f, 0.2f, CenterDis) * FMath::SmoothStep(1.f, 0.8f, CenterDis);
      const float PeakNoise = FastNoise3D(nX * 0.006f, nY * 0.006f, 0.f);
      if (PeakNoise > 0.2f) {
        CentralHeight += (PeakNoise - 0.2f) * 600.f * Fade;
      }
    }
      
      // --- EJECTA BLANKET - Auswurfmaterial around crater rim ---
      // Creates the ejecta blanket (Ejecta-Decke) - layer of material thrown out during impact
      if (NormalizedDist > RimEnd && NormalizedDist <= RimEnd + CRC.EjectaBlanketWidth) {
        // Ejecta thickness fades with distance from rim
        const float EjectaDist = NormalizedDist - RimEnd;
        const float EjectaFade = FMath::Pow(1.0f - (EjectaDist / CRC.EjectaBlanketWidth), CRC.EjectaFadeExponent);
        const float EjectaHeight = CRC.EjectaThickness * FMath::Abs(CRC.CentralCraterDepth) * EjectaFade;
        
        // Inner fade at RimEnd to ensure overlay starts smoothly without a height jump
        const float InnerFade = FMath::SmoothStep(0.0f, 0.02f, EjectaDist);
        CentralHeight += EjectaHeight * 0.5f * InnerFade; 
        
        // --- EJECTA BLOCKS - Blockfeld ausgeworfene Blöcke ---
        // Large, angular or curved rock blocks thrown out during impact
        const float BlockNoise = FastNoise3D(nX * CRC.EjectaBlockFrequency, nY * CRC.EjectaBlockFrequency, 0.f);
        const float BlockThreshold = 0.8f;
        if (BlockNoise > BlockThreshold) {
          const float BlockSize = (BlockNoise - BlockThreshold) * CRC.EjectaBlockSize;
          const float BlockHeight = (BlockNoise - BlockThreshold) * CRC.EjectaBlockAmplitude;
          
          // Apply block with continuous outer fade
          const float BlockFade = FMath::SmoothStep(CRC.EjectaBlanketWidth * 0.8f, CRC.EjectaBlanketWidth * 0.72f, EjectaDist);
          CentralHeight += BlockHeight * BlockFade;
        }
        
        // --- OVERTURNED STRATA - Überkippte Schichten ---
        // Bent and overturned rock layers at the crater edge
        const float StrataNoise = FastNoise3D(nX * CRC.OverturnedStrataFrequency, nY * CRC.OverturnedStrataFrequency, 0.f);
        const float StrataThreshold = 0.7f;
        if (StrataNoise > StrataThreshold) {
          const float StrataHeight = (StrataNoise - StrataThreshold) * CRC.OverturnedStrataAmplitude;
          
          // Apply overturned strata with continuous outer fade
          const float StrataFade = FMath::SmoothStep(CRC.EjectaBlanketWidth * 0.6f, CRC.EjectaBlanketWidth * 0.55f, EjectaDist);
          const float StrataCurve = FMath::Sin(EjectaDist * 10.0f) * 0.5f + 0.5f;
          CentralHeight += StrataHeight * StrataCurve * StrataFade;
        }
      }
      
      // Add organic erosion noise to outer rim
      const float ErosionNoise = FastNoise3D(nX * 0.0015f, nY * 0.0015f, 0.f) * CRC.RimNoiseAmplitude * 0.4f;
      if (NormalizedDist > RimEnd && NormalizedDist < RimEnd + 0.10f) {
        const float CenterDis = (NormalizedDist - RimEnd) / 0.10f;
        const float Fade = FMath::SmoothStep(0.f, 0.2f, CenterDis) * FMath::SmoothStep(1.f, 0.8f, CenterDis); 
        CentralHeight += ErosionNoise * Fade * FMath::Exp(-(NormalizedDist - RimEnd) * 6.0f);
      }

      // Add rim noise for natural irregularity (Continuous Fade)
      const float RimNoise = FastNoise3D(nX * 0.0012f, nY * 0.0012f, 0.f) * CRC.RimNoiseAmplitude;
      if (NormalizedDist > RimEnd && NormalizedDist < RimEnd + 0.15f) {
        const float CenterDis = (NormalizedDist - RimEnd) / 0.15f;
        const float Fade = FMath::SmoothStep(0.f, 0.2f, CenterDis) * FMath::SmoothStep(1.f, 0.8f, CenterDis); 
        CentralHeight += RimNoise * Fade * FMath::Exp(-(NormalizedDist - RimEnd) * 4.0f); 
      }
    
    // Apply central crater with distance-based blending
    TotalHeight = FMath::Lerp(TotalHeight, CentralHeight, CentralDominance);
  }

  // 2. SECONDARY CRATERS - scattered around central area with radial distribution
  if (DistFromCenter > CRC.CentralCraterRadius * 0.2f) {
    // Use much higher frequency for more craters and add strong radial bias
    // 2. SECONDARY CRATERS - Scattered grid-based independent impact craters
    const float CellSz = 25000.f; // 250m grid slots for minor craters
    const int32 CellX  = FMath::FloorToInt(nX / CellSz);
    const int32 CellY  = FMath::FloorToInt(nY / CellSz);
    
    // Hash-based offset for cellular centers
    const float CenterOffX = FastNoise3D(CellX * 13.f, CellY * 9.f,  0.f) * 0.38f * CellSz;
    const float CenterOffY = FastNoise3D(CellX * 13.f, CellY * 9.f, 50.f) * 0.38f * CellSz;
    const float LocalX     = (CellX + 0.5f) * CellSz + CenterOffX;
    const float LocalY     = (CellY + 0.5f) * CellSz + CenterOffY;
    
    const float DistToSecondary = FMath::Sqrt(FMath::Square(nX - LocalX) + FMath::Square(nY - LocalY));
    
    // Roll impact chance in this cell
    const float ImpactRoll = FastNoise3D(CellX * 7.f, CellY * 11.f, 100.f); 
    
    if (ImpactRoll > 0.1f && DistFromCenter > CRC.CentralCraterRadius * 0.82f) { // outside primary rim
      const float NormImpact = (ImpactRoll - 0.1f) / 0.9f;
      const float SecondarySize = FMath::Lerp(2000.f, CRC.SecondaryCraterMaxRadius, NormImpact);
      
      if (DistToSecondary < SecondarySize) {
        const float SecNormDist = DistToSecondary / SecondarySize;
        const float SecondaryDepth = FMath::Lerp(-800.f, -2200.f, NormImpact);
        const float SecondaryRimH  = FMath::Lerp(800.f, 2000.f, NormImpact);
        
        float SecondaryHeight = BasePlains;
        const float SecRimStart = 0.65f; // Wide bowl profile
        const float SecRimEnd   = 0.85f;
        
        if (SecNormDist < SecRimStart) {
          // Flat/shallow bowl floor
          SecondaryHeight = BasePlains + SecondaryDepth;
        } else if (SecNormDist < SecRimEnd) {
          // Rim wall
          const float RimT = FMath::SmoothStep(SecRimStart, SecRimEnd, SecNormDist);
          SecondaryHeight = FMath::Lerp(BasePlains + SecondaryDepth, BasePlains + SecondaryRimH, RimT);
        } else {
          // Inner/outer slope fade
          const float FadeT = FMath::SmoothStep(SecRimEnd, 1.0f, SecNormDist);
          SecondaryHeight = FMath::Lerp(BasePlains + SecondaryRimH, BasePlains, FadeT);
        }
        
        const float RimNoise = FastNoise3D(nX * 0.005f, nY * 0.005f, 0.f) * 300.f;
        if (SecNormDist > SecRimStart && SecNormDist < 1.0f) {
          SecondaryHeight += RimNoise * FMath::Sin(SecNormDist * 3.14159f);
        }

        // Apply crater cutout to TotalHeight with smooth radial blend
        const float CraterWeight = 1.0f - FMath::Pow(SecNormDist, 4.0f);
        TotalHeight = FMath::Lerp(TotalHeight, SecondaryHeight, CraterWeight * CRC.SecondaryCraterDensity);
      }
    }
  }

  // 3. TERTIARY CRATERS - very small impacts in surrounding area with high density
  if (DistFromCenter > CRC.CentralCraterRadius * 0.3f) {
    // Enhanced tertiary crater system for small variations around central crater
    const float TertiaryFreq = CRC.ImpactFrequency * 15.0f; // Higher frequency for more small craters
    float TertiaryImpact = FastNoise3D(nX * TertiaryFreq, nY * TertiaryFreq, 500.f);
    
    // Add additional noise layer for more natural distribution
    const float TertiaryNoise2 = FastNoise3D(nX * TertiaryFreq * 0.5f, nY * TertiaryFreq * 0.5f, 700.f);
    TertiaryImpact = FMath::Max(TertiaryImpact, TertiaryNoise2);
    
    // Add radial bias to concentrate small craters around central crater
    const float RadialBias = FMath::Exp(-DistFromCenter / (CRC.CentralCraterRadius * 0.8f));
    const float BiasedImpact = TertiaryImpact + (RadialBias * 0.3f);
    
    const float TertiaryThreshold = 0.6f; // Lower threshold for more craters
    if (BiasedImpact > TertiaryThreshold) {
      // Use new configuration parameters for tertiary craters
      const float TertiaryDepth = -200.f - (BiasedImpact * 150.f); // Shallower for small craters
      const float TertiarySize = CRC.TertiaryCraterMinRadius + 
                                (BiasedImpact * (CRC.TertiaryCraterMaxRadius - CRC.TertiaryCraterMinRadius));
      
      const float TertiaryDist = DistFromCenter * 1.0f;
      const float TertiaryNormalizedDist = FMath::Clamp(TertiaryDist / TertiarySize, 0.f, 1.f);
      
      // Simple bowl shape for tertiary craters with flatter profile
      const float BowlShape = FMath::Pow(1.f - TertiaryNormalizedDist, 1.3f);
      const float TertiaryHeight = BasePlains + (TertiaryDepth * BowlShape);
      
      // Add small rim for tertiary craters
      if (TertiaryNormalizedDist > 0.1f && TertiaryNormalizedDist < 0.25f) {
        const float RimT = FMath::SmoothStep(0.1f, 0.25f, TertiaryNormalizedDist);
        const float RimBoost = 200.f * RimT; // Smaller rim for tiny craters
        TotalHeight = FMath::Lerp(TotalHeight, TertiaryHeight + RimBoost, 0.20f); // Higher blend for visibility
      } else {
        TotalHeight = FMath::Lerp(TotalHeight, TertiaryHeight, 0.20f);
      }
    }
  }

  // 4. FLOOR DETAIL - add small-scale roughness to crater floors
  const float FloorNoise = FBM(nX * CRC.BuildingNoiseFrequency,
                               nY * CRC.BuildingNoiseFrequency, 0.f,
                               2, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves)
                           * CRC.BuildingNoiseAmplitude;

  // Apply floor detail only where terrain is below base plains (crater areas)
  if (TotalHeight < BasePlains) {
    TotalHeight += FloorNoise * 0.2f; // Subtle floor texture
  }

  return TotalHeight;
}

// ============================================================
//  SKYLANDS - coherent floating islands
//
//  DESIGN PRINCIPLES:
//  - Altitude is CONSTANT per island (no per-voxel AltBoost) to prevent shard
//  artifacts
//  - Shape test is primarily 2D (XY) so island interiors are always solid
//  - Minimal Z-variation in noise prevents vertical holes through islands
//  - Mountains -> high altitude, large, frequent islands
//  - Plains    -> low altitude, small, sparse islands (shard rocks)
//
//  Fix history:
//  - Removed AltBoost = SizeFactor * 12000: was causing altitude to vary 120m
//  per-pixel,
//    which made the Z-gate fire inconsistently within a chunk creating vertical
//    black slabs
//  - Reduced Z frequency from 0.4x to 0.05x: prevents Swiss cheese holes
//  - Primary shape test is 2D (early-exit): guarantees solid island interiors
//  - Capped shape octaves at 2 for stability over flat terrain
// ============================================================
float FVoxelBiomeGenerators::GetSkylandDensity(
    float X, float Y, float Z, float SurfaceHeight,
    const FVoxelBiomeWeightMap &Weights, const FVoxelGenerationConfig &Config,
    int32 StepSize) {
    FSkylandColumnCache Cache = GetSkylandColumnCache(X, Y, SurfaceHeight, Weights, Config);
    return GetSkylandDensityFromCache(Cache, X, Y, Z, Config, StepSize);
}


FSkylandColumnCache FVoxelBiomeGenerators::GetSkylandColumnCache(

    float X, float Y, float SurfaceHeight,

    const FVoxelBiomeWeightMap& Weights,

    const FVoxelGenerationConfig& Config)

{

    FSkylandColumnCache Cache;

    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;

    const FVector Off = Config.GetSeedOffset();

    // ============================================================
    //  SKYLAND GENERATION OVERVIEW & SPIKE FIX
    // ============================================================

    //

    // PURPOSE: Generate floating islands ("skylands") that appear high above terrain.
    // The system uses a cellular grid where each cell may spawn an island. The island's

    // properties (size, altitude, thickness, shape) are determined by the terrain
    // characteristics (height, roughness) beneath that cell.
    //
    // TWO-PHASE EVALUATION:
    //  1. Column Cache (this function): Expensive O(n²) operation run once per XY column.

    //     Samples 9 neighboring grid cells, selects the best one (nearest with valid spawn),
    //     and caches its properties (SkyAlt, HalfThick, Freq, Threshold, etc.).
    //  2. Voxel Density: Cheap O(1) lookup using the cache. Evaluates shape noise and
    //     vertical falloff to produce the final signed distance value.
    //
    // WHY SKYLANDS BECAME SPIKY (pre-fix):
    //  - Thickness scaled linearly with IslandSize: HalfThick = IslandSize * EffThickness
    //  - Horizontal feature size (noise wavelength) scaled with sqrt(IslandSize):

    //      Freq = ShapeFrequency / sqrt(SizeRatio)   where SizeRatio = IslandSize / BaseIslandSize
    //  - As islands grew larger (high terrain), thickness grew faster than horizontal extent.

    //    The aspect ratio (thickness/width) increased ~sqrt(SizeRatio), turning large islands
    //    into tall thin pillars instead of flat discs.
    //
    // THE FIX (implemented below):

    //  1. Linear frequency scaling: Freq = ShapeFrequency / SizeRatio

    //     This makes wavelength proportional to island size, maintaining consistent aspect

    //     ratio across all scales. Large islands are now properly wide and flat.
    //  2. Threshold reduction for large islands:

    //      if (CellShardT > 0.5f) Threshold -= log2(SizeRatio) * 0.05f
    //     Lowers the shape threshold so noise lobes merge into one coherent disc instead of
    //     many separate peaks. Without this, large islands would still be spiky even with
    //     correct frequency scaling.
    //  3. MaxThicknessRatio clamp: Safety net to prevent extreme aspect ratios from
    //     misconfigured parameters. HalfThick = min(HalfThick, IslandSize * MaxThicknessRatio).
    //  4. Clearance fix: After selecting the best cell, raise the island if its bottom would
    //     intersect the local terrain (using the column's SurfaceHeight, not the cell's
    //     AltitudeBase). Ensures visible gap above ground everywhere.
    //
    // ASPECT RATIO CONTROL:
    //  Desired: Aspect = HalfThick / IslandSize ≈ 0.1–0.3 (flat disc)
    //  With EffThickness = 0.1 (shards) to 0.2–0.3 (islands) and proper frequency scaling,

    //  the noise solid region radius ≈ 0.5–0.7 * IslandSize, giving Aspect ≈ 0.14–0.42.
    //  The MaxThicknessRatio (default 0.3) caps aspect at 0.6 even if config is extreme.
    //
    // PERFORMANCE NOTE:

    //  This function is called once per XY column during chunk generation. All expensive
    //  operations (biome sampling, noise evaluations) are confined here. The returned
    //  cache is reused for every Z voxel in that column, making skyland evaluation cheap.
    // ============================================================


    // ---------------------------------------------------------------
    //  SHARD SYSTEM: altitude-driven grid density
    //
    //  High terrain  -> large GridSize  -> islands are rare, large, spaced far apart
    //  Low terrain   -> small GridSize  -> shards are tiny, numerous, scattered
    //
    //  We compute a representative HeightNorm for THIS column first so we can
    //  set the grid size before we search cells. We use the passed-in SurfaceHeight
    //  rather than re-sampling, keeping the column-cache call cheap.
    // ---------------------------------------------------------------
    const float ColHeightNorm    = FMath::Clamp(SurfaceHeight / SC.MaxTerrainReference, 0.f, 1.f);
    const float ColRoughnessNorm = FMath::Clamp(Weights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);
    const float ColTerrainStr    = FMath::Clamp(ColHeightNorm * 1.5f + ColRoughnessNorm * 0.8f, 0.f, 1.f);

    // ShardT: 0 = pure low-altitude shard field, 1 = full-size high-altitude island
    // Uses a smoothstep so the transition from shard -> island is gradual.
    const float ShardT = FMath::SmoothStep(0.0f, SC.ShardTransitionStrength, ColTerrainStr);

    // Grid size: shards use a much tighter grid so there are more of them.
    // High islands:  BaseIslandSize * 4  (wide spacing, few large islands)
    // Low shards:    BaseIslandSize * 1  (tight spacing, many tiny shards)
    const float GridSize = SC.BaseIslandSize * FMath::Lerp(1.0f, 4.0f, ShardT);
    if (GridSize <= 0.f) return Cache;

    const int32 CellX = FMath::FloorToInt(X / GridSize);
    const int32 CellY = FMath::FloorToInt(Y / GridSize);


    float MaxW             = -1.f;
    float BestSkyAlt       = 0.f;
    float BestHalfThick    = 0.f;

    float BestThreshold    = 0.f;

    float BestHeightNorm   = 0.f;

    float BestShardFalloff = 0.f;

    float BestFreq         = 0.f;

    float BestIslandSize   = 0.f;


    float BestCellShardT   = 0.f;

    float BestDistRatio    = FLT_MAX;  // Lower is better - normalized distance to cell center



    for (int32 dx = -1; dx <= 1; ++dx)
    for (int32 dy = -1; dy <= 1; ++dy)
    {
        const int32 currentCellX = CellX + dx;
        const int32 currentCellY = CellY + dy;

        const float nX = (float)currentCellX * GridSize + Off.X;
        const float nY = (float)currentCellY * GridSize + Off.Y;

        const float HashX = (FastNoise3D(nX * 0.001f, nY * 0.001f, 0.f) + 1.f) * 0.5f;
        const float HashY = (FastNoise3D(nX * 0.001f, nY * 0.001f, 100.f) + 1.f) * 0.5f;

        const float CenterX = (currentCellX + 0.12f + HashX * 0.76f) * GridSize;
        const float CenterY = (currentCellY + 0.12f + HashY * 0.76f) * GridSize;

        const float DistSq = FMath::Square(X - CenterX) + FMath::Square(Y - CenterY);
        const float Dist   = FMath::Sqrt(DistSq);

        // --- Evaluate cell terrain ---
        const FVoxelBiomeWeightMap CenterWeights = FVoxelBiomeManager::GetBiomeWeightsStatic(CenterX, CenterY, Config);
        
        // Calculate crater-neutral height so islands do not drop into local depressions
        FVoxelBiomeWeightMap NeutralWeights = CenterWeights;
        NeutralWeights.SetWeight(EVoxelBiome::Craters, 0.f);
        NeutralWeights.Normalize();
        
        const float CenterHeight = FVoxelBiomeManager::GetSurfaceHeightStatic(CenterX, CenterY, NeutralWeights, Config);

        const float HeightNorm    = FMath::Clamp(CenterHeight / SC.MaxTerrainReference, 0.f, 1.f);
        const float RoughnessNorm = FMath::Clamp(CenterWeights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);

        const float CurvedHeight  = FMath::Pow(FMath::Max(0.f, HeightNorm), 2.5f);
        const float CurvedRough   = FMath::Pow(FMath::Max(0.f, RoughnessNorm), 2.0f);
        const float TerrainStr    = FMath::Clamp(HeightNorm * 1.5f + RoughnessNorm * 0.8f, 0.f, 1.f);
        const float ShardFalloff  = FMath::Pow(FMath::Max(0.f, TerrainStr), 2.2f);

        // CellShardT: same altitude ramp but per-cell so size/thickness are
        // evaluated against the cell's own terrain, not the query column.
        const float CellShardT = FMath::SmoothStep(0.0f, SC.ShardTransitionStrength, TerrainStr);

        // Minimum ShardFalloff gate: disabled to allow sparse absolute-Altitude shards over plains
        // if (ShardFalloff < 0.008f) continue;

        const float cnX = CenterX + Off.X;
        const float cnY = CenterY + Off.Y;
        const float HashProb = (FastNoise3D(cnX * 0.002f, cnY * 0.002f, 200.f) + 1.f) * 0.5f;

        // -------------------------------------------------------------------
        //  PROBABILITY
        //  Low terrain  -> BaseProbability only (sparse scattered shards)
        //  High terrain -> BaseProbability + HeightBonus + RoughnessBonus
        //  CurvedHeight applies a power curve so probability rises steeply
        //  only over genuinely tall terrain, not gradual plains.
        // -------------------------------------------------------------------
        float Prob = FMath::Lerp(SC.BaseProbability, SC.BaseProbability + SC.HeightProbabilityBonus, CellShardT);
        if (HashProb > Prob) continue;

        // -------------------------------------------------------------------
        //  SIZE: aggressive altitude falloff
        //  High island:   BaseIslandSize + HeightSizeBonus + RoughnessSizeBonus
        //  Low shard:     BaseIslandSize * ShardMinScale  (very small)
        //
        // FIX: Raise ShardMinScale to ensure shards have a core width that supports 3D noise detail
        const float ShardMinScale = FMath::Max(0.20f, SC.ShardMinScale); // Wide enough to build decent features (was SC.ShardMinScale)
        const float SizeNoise   = FBM(cnX * 0.00008f, cnY * 0.00008f, 50.f, 2, 2.0f, 0.5f, 2);
        const float SizeFactor  = (SizeNoise + 1.f) * 0.5f;

        // Base size at this altitude: lerp from tiny shard to full island.
        float IslandSize = FMath::Lerp(
            SC.BaseIslandSize * ShardMinScale,
            SC.BaseIslandSize + SC.HeightSizeBonus,
            CellShardT);

        // Apply per-cell size noise (±50% at low altitude, ±25% at high).
        const float NoiseRange = FMath::Lerp(0.5f, 0.25f, CellShardT);
        IslandSize *= (1.f - NoiseRange) + NoiseRange * SizeFactor * 2.f;
        IslandSize  = FMath::Max(IslandSize, 150.f);  // absolute minimum: 1.5m

        // Never exceed grid cell to avoid overlapping adjacent cells.
        IslandSize = FMath::Min(IslandSize, GridSize * 0.48f);

        if (Dist > IslandSize) continue;

        // -------------------------------------------------------------------
        //  ALTITUDE: low shards float just above terrain, high islands soar
        // -------------------------------------------------------------------
        const float AltitudeBase = FMath::Lerp(SC.MinAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, TerrainStr);
        // FIX: Removed 150m absolute anchor that forced shards sky-high.
        // Setting DecoupledHeight directly to CenterHeight allows shards to hover contextually just above ground.
        const float DecoupledHeight = CenterHeight; 

        // FIX: Match altitude formula in VoxelWorld_Streaming.cpp exactly.
        // Previously missing the CurvedHeight/Rough curve factors, causing
        // islands and streamed Volumes to drift apart by up to 20 meters.
        float SkyAlt = DecoupledHeight + AltitudeBase
            + CellShardT * (CurvedHeight * SC.HeightAltitudeBonus + CurvedRough * SC.RoughnessAltitudeBonus);

        // SIZE BY ALTITUDE: shards that float higher above local terrain are bigger.
        // A shard barely clearing a hillside = small pebble.
        // A shard soaring 150m above flat plains = dramatic sky boulder.
        // Islands (CellShardT=1) are unaffected (factor lerps to 1.0).
        {
            const float AltGap        = FMath::Max(0.f, SkyAlt - CenterHeight);
            const float RefGap        = FMath::Max(1.f, SC.MinAltitudeAboveTerrain);
            const float AltSizeScale  = FMath::Clamp(AltGap / RefGap, 0.4f, 3.0f);
            IslandSize *= FMath::Lerp(AltSizeScale, 1.0f, CellShardT);
            IslandSize  = FMath::Max(IslandSize, 150.f);         // 1.5m minimum
            IslandSize  = FMath::Min(IslandSize, GridSize * 0.48f); // never overlap cells
        }

        // --- ADJUST HEIGHT POSITION FOR SMALLER SKYSHARDS ---
        // Smaller shards (low CellShardT) should hover closer to ground
        // Larger islands (high CellShardT) should maintain proper altitude
        {
            const float HeightAdjustment = FMath::Lerp(0.3f, 1.0f, CellShardT); // Lower multiplier for smaller shards (was 0.6f)
            const float AdjustedAltitudeBase = AltitudeBase * HeightAdjustment;
            
            // For very small shards, reduce altitude significantly to hover closer to ground
            if (CellShardT < 0.3f) {
                const float ShardAltitudeReduction = FMath::Lerp(0.2f, 0.7f, CellShardT); // 80% to 30% reduction (was 0.4 to 0.8)
                SkyAlt = DecoupledHeight + (AdjustedAltitudeBase * ShardAltitudeReduction)
                    + CellShardT * (CurvedHeight * SC.HeightAltitudeBonus + CurvedRough * SC.RoughnessAltitudeBonus);
            } else {
                // Standard altitude calculation for larger islands
                SkyAlt = DecoupledHeight + AdjustedAltitudeBase
                    + CellShardT * (CurvedHeight * SC.HeightAltitudeBonus + CurvedRough * SC.RoughnessAltitudeBonus);
            }

            // ADD RANDOM ALTITUDE STAGGER (Variance): Smaller shards get wider vertical flight heights
            const float HashStagger = FastNoise3D(cnX * 0.006f, cnY * 0.006f, 500.f);
            const float StaggerAmt  = FMath::Lerp(5000.f, 1500.f, CellShardT); // +-50m variance for small shards
            SkyAlt += HashStagger * StaggerAmt;
        }


        // -------------------------------------------------------------------

        //  THICKNESS: shards are chunky rocks, islands are flat discs
        //
        //  ROCK SHAPE FIX:
        //  Old: EffThickness=0.10 for shards → razor-thin pancake → looks like
        //       a vertical slab/pillar from the side. Surface Nets generates a
        //       1-voxel-thin quad that reads as a pillar.
        //  New: EffThickness=0.65 for shards → near-spherical boulder aspect.
        //       With IslandSize=625cm → HalfThick≈406cm → proper 3D rock shape.
        //
        //  ThicknessRatio (islands, CellShardT=1): stays at SC.ThicknessRatio
        //  (default 0.2) so full skylands remain flat floating platforms.
        // -------------------------------------------------------------------
        // FIX: Add random aspect ratio for shards. Flat pancakes preferred!
        const float HashAspect = (FastNoise3D(cnX * 0.005f, cnY * 0.005f, 300.f) + 1.f) * 0.5f;
        const float ShardThickBase = FMath::Lerp(0.12f, 0.25f, HashAspect); // 12% to 25% wide ratio (was 35-75%)
        const float EffThickness = FMath::Lerp(ShardThickBase, SC.ThicknessRatio, CellShardT);

        float HalfThick = IslandSize * EffThickness;

        // --- 🪨 MAX THICKNESS RATIO: per-type limits ---
        // Shards (rocks):  allow near-spherical aspect (0.75 → height = 75% of radius)
        // Islands (discs): keep SC.MaxThicknessRatio (default 0.3 → flat floating platform)
        const float EffMaxThicknessRatio = FMath::Lerp(0.75f, SC.MaxThicknessRatio, CellShardT);
        const float MaxAllowedHalfThick  = IslandSize * EffMaxThicknessRatio;

        HalfThick = FMath::Min(HalfThick, MaxAllowedHalfThick);

        // FIX: Ensure the bottom of the island doesn't penetrate below ground!
        // Instead of squashing thickness (which alters shape), we push the altitude center UPWARDS.
        const float MinSafeSkyAlt = CenterHeight + HalfThick + 200.f; // 2m clearance from ground
        SkyAlt = FMath::Max(SkyAlt, MinSafeSkyAlt);

        

        // --- CLEARANCE PROTECTION: commented out per user request ---
        // The 200cm buffer was clamping HalfThick down too aggressively,
        // causing shards to lose thickness and appear as thin pillar slabs.
        // Island altitude is already set well above terrain via AltitudeBase;
        // the post-selection column-height adjustment below handles real clipping.
        //
        // const float Clearance = 200.f;
        // const float MaxAllow  = AltitudeBase - Clearance;
        // if (MaxAllow <= 0.f)
        //     HalfThick = 0.01f;
        // else
        //     HalfThick = FMath::Min(HalfThick, MaxAllow);


        // shape threshold

        const float ShardThresholdBoost = FMath::Lerp(0.20f, 0.0f, CellShardT);

        float Threshold = FMath::Lerp(SC.ThresholdAtMinProbability, SC.ThresholdAtMaxProbability, CellShardT)
                                + ShardThresholdBoost;
        
        // Ensure larger islands get lower threshold to merge noise features into a single disc
        // Without this, large islands would be many small peaks instead of one coherent shape
        if (CellShardT > 0.5f) {
            const float SizeRatio = FMath::Max(1.f, IslandSize / SC.BaseIslandSize);
            Threshold -= FMath::Log2(SizeRatio) * 0.05f;  // Lower threshold for larger islands

        }

        
        // Ensure larger islands get lower threshold to merge noise features into a single disc
        // Without this, large islands would be many small peaks instead of one coherent shape
        if (CellShardT > 0.5f) {
            const float SizeRatio = FMath::Max(1.f, IslandSize / SC.BaseIslandSize);
            Threshold -= FMath::Log2(SizeRatio) * 0.05f;  // Lower threshold for larger islands
        }



        // Continuous blend weight (used for MaxW check)

        const float W = FMath::Square(1.f - (Dist / IslandSize));



        // Normalized distance for fair cell selection across different island sizes
        const float DistRatio = Dist / IslandSize;



        // Track the cell with smallest normalized distance (nearest cell)
        if (DistRatio < BestDistRatio && W > 0.001f)  // Only consider cells with meaningful influence

        {

            MaxW             = W;

            BestSkyAlt       = SkyAlt;
            BestHalfThick    = HalfThick;
            BestThreshold    = Threshold;
            BestHeightNorm   = HeightNorm;
            BestShardFalloff = ShardFalloff;
            BestIslandSize   = IslandSize;
            BestCellShardT   = CellShardT;

            const float SizeRatio = FMath::Max(1.f, IslandSize / SC.BaseIslandSize);
            const float IslandFreq = SC.ShapeFrequency / SizeRatio;
            const float ShardFreq  = SC.ShapeFrequency * 6.0f;
            BestFreq = FMath::Lerp(ShardFreq, IslandFreq, CellShardT);

            // FIX: Update the tracking ratio so it correctly selects the NEAREST cell 
            // instead of falling back to the last cell in the grid loop iterator.
            BestDistRatio = DistRatio; 
        }
    }


    if (MaxW <= 0.f) return Cache;




    Cache.SkyAlt       = BestSkyAlt;



    Cache.HalfThick    = BestHalfThick;



    Cache.Threshold    = BestThreshold;



    Cache.HeightNorm   = BestHeightNorm;



    Cache.ShardFalloff = BestShardFalloff;

    // ShardT drives falloff shape and noise in GetSkylandDensityFromCache.
    Cache.ShardT      = BestCellShardT;


    // Post-selection terrain clearance adjustment: commented out per user request.
    // The 200cm forced-rise was pushing islands upward even when they were already
    // correctly placed, which combined with MinAltitudeAboveTerrain was double-offsetting
    // the altitude and causing islands to clip into high terrain on the way up.
    // AltitudeBase already guarantees separation; let the density gate in
    // GetSkylandDensityFromCache (HeightCutoff fade) handle the isosurface boundary.
    //
    // const float Clearance = 200.f;
    // const float MinBottom = SurfaceHeight + Clearance;
    // const float CurrentBottom = Cache.SkyAlt - Cache.HalfThick;
    // if (CurrentBottom < MinBottom)
    // {
    //     const float NeededRise = MinBottom - CurrentBottom;
    //     Cache.SkyAlt += NeededRise;
    // }




    // Freq: shards need much higher frequency noise to look jagged.

    // Low shards: ShapeFrequency * 6  (high-freq = rough, spiky silhouette)

    // High islands: ShapeFrequency / sqrt(SizeRatio)  (smooth, organic)

    // Use the selected cell's island size (not blended) for consistent shape


    const float SizeRatio = FMath::Max(1.f, BestIslandSize / SC.BaseIslandSize);



    // FIX: Linear scaling (was sqrt) to maintain consistent aspect ratio across island sizes.
    // With sqrt scaling, large islands had disproportionately small horizontal extent,

    // causing spikes. Linear scaling makes wavelength ∝ IslandSize, so solid region scales
    // proportionally with thickness → flat discs at all sizes.
    const float IslandFreq = SC.ShapeFrequency / SizeRatio;  // Linear scaling for consistent aspect ratio



    const float ShardFreq = SC.ShapeFrequency * 6.0f;


    Cache.Freq = FMath::Lerp(ShardFreq, IslandFreq, BestCellShardT);

    Cache.Freq = FMath::Max(Cache.Freq, 0.00025f);




    Cache.Prob    = 0.5f;





    Cache.WX_base = X + Off.X;



    Cache.WY_base = Y + Off.Y;



    Cache.WX      = Cache.WX_base;



    Cache.WY      = Cache.WY_base;



    Cache.bHasSkyland = true;



    

    // Aspect Ratio logging removed for performance tuning (was UE_LOG spamming per column).

    

    return Cache;


}

float FVoxelBiomeGenerators::GetSkylandDensityFromCache(
    const FSkylandColumnCache& Cache, float X, float Y, float Z,
    const FVoxelGenerationConfig& Config, int32 StepSize)
{
    if (!Cache.bHasSkyland) return -2.f;

    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
    const FVector Off = Config.GetSeedOffset();

    const float HalfThick = Cache.HalfThick;

    const float Margin = HalfThick * 0.4f;
    if (Z < Cache.SkyAlt - HalfThick - Margin || Z > Cache.SkyAlt + HalfThick + Margin) return -2.f;

    const float FullRange = HalfThick + Margin;
    const float tCenter = FMath::Clamp((Z - Cache.SkyAlt) / (FullRange + 1.f), -1.f, 1.f);

    // FALLOFF SHAPE: blend between rock (spherical) and island (flat-top plateau).
    //
    // Island falloff (ShardT=1): flat top zone (35%) + smooth underside taper.
    //   Creates the "floating platform" look — flat on top, tapered underneath.
    //
    // Rock falloff  (ShardT=0): symmetric spherical — equal taper in all Z directions.
    //   No flat zone → looks like a boulder/rock, not a platform.
    //   Uses pow(|t|, 0.6) for a slightly boxy rock profile (flatter than a perfect
    //   sphere at center, sharper at the edges).

    // --- Island falloff (flat-top) ---
    float IslandFalloff;
    {
        if (tCenter >= 0.f) {
            const float FlatZone = 0.35f;
            if (tCenter < FlatZone) {
                IslandFalloff = 1.0f;
            } else {
                const float nt = (tCenter - FlatZone) / (1.f - FlatZone);
                IslandFalloff = FMath::SmoothStep(0.f, 1.f, 1.f - nt);
            }
        } else {
            const float t = FMath::Clamp(-tCenter, 0.f, 1.f);
            IslandFalloff = FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(t, 0.85f));
        }
    }

    // --- Rock falloff (spherical, no flat zone) ---
    const float tAbs      = FMath::Abs(tCenter);
    const float RockFalloff = FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(tAbs, 0.6f));

    // FIX: Blend at least 40% IslandFalloff onto shards to give them flat tops
    float Falloff = FMath::Lerp(RockFalloff, IslandFalloff, FMath::Max(0.40f, Cache.ShardT));
    
    // --- ROUNDNESS ADJUSTMENT FOR SMALLER SKYSHARDS ---
    // Smaller shards (low ShardT) should be rounder, larger islands (high ShardT) flatter
    if (Cache.ShardT < 0.3f) {
        // For very small shards, make them more spherical/rounded
        const float RoundnessFactor = FMath::Lerp(1.0f, 0.6f, Cache.ShardT); 
        const float RoundedT = FMath::Pow(tAbs, RoundnessFactor);
        const float RoundedFalloff = FMath::SmoothStep(0.f, 1.f, 1.f - RoundedT);
        
        // Blend rounded falloff more for smaller shards
        const float RoundBlend = FMath::Lerp(0.8f, 0.2f, Cache.ShardT);
        Falloff = FMath::Lerp(RoundedFalloff, Falloff, RoundBlend);
        // NO early return: fall through to evaluated 3D Shape noise detail below
    }

    const float WX_base = X + Off.X;
    const float WY_base = Y + Off.Y;
    const float WZ = Z + Off.Z;

    if (Falloff < 0.001f)
    {
        const float MaxBreakUpEO    = FMath::Lerp(0.50f, 2.80f, Cache.HeightNorm);
        const float BreakUpStrengthEO = FMath::Lerp(0.10f, MaxBreakUpEO, Cache.ShardT);
        const float BreakUp = FMath::Max(0.f, FastNoise3D(WX_base * 0.002f, WY_base * 0.002f, WZ * 0.001f)) * BreakUpStrengthEO;
        return FMath::Clamp(-1.8f - BreakUp, -2.f, 2.f);
    }

    float WX = WX_base;
    float WY = WY_base;

    if (SC.bEnableDomainWarping) {
      const float WF = SC.DomainWarpFrequency;
      WX += FastNoise3D(WX * WF + 10.f, WY * WF + 20.f, 0.f) * SC.DomainWarpStrength;
      WY += FastNoise3D(WX * WF + 50.f, WY * WF + 10.f, 0.f) * SC.DomainWarpStrength;
    }

    // 3D SHAPE NOISE:
    // Islands (ShardT=1): very low Z frequency (0.05x) keeps island interior solid —
    //   prevents swiss-cheese vertical holes through large platforms.
    // Rocks  (ShardT=0): higher Z frequency (0.50x) gives irregular 3D boulder surface.
    //   Strength also raised (0.55) so the rock surface is visibly lumpy/craggy.
    //   3D noise is ALWAYS evaluated for shards regardless of bEnable3DSkylandNoise flag.
    float ShapeDetail = 0.f;
    {
        const float ZFreqScale     = FMath::Lerp(0.50f, 0.05f, Cache.ShardT);
        const float DetailStrength = FMath::Lerp(0.55f, 0.25f, Cache.ShardT);
        if (Config.Performance.bEnable3DSkylandNoise || Cache.ShardT < 0.5f)
        {
            ShapeDetail = FastNoise3D(
                WX * Cache.Freq * 0.6f,
                WY * Cache.Freq * 0.6f,
                WZ * Cache.Freq * ZFreqScale) * DetailStrength;
        }
    }

    // Point-wise ShapeXY prevents absolute grid-cell fractures on cell boundaries
    const int32 Oct2D = FMath::Clamp(FMath::Min((int32)SC.ShapeOctaves, 2), 1, Config.Performance.MaxNoiseOctaves);
    
    // FIX: Use 3D noise (absolute Z) for smaller shards to break the continuous 
    // vertical columnar extrusion projections, forming organic 3D boulders.
    const float ShapeZ  = (Cache.ShardT < 0.5f) ? WZ * Cache.Freq : 0.f;
    const float ShapeXY = FBM(WX * Cache.Freq, WY * Cache.Freq, ShapeZ, Oct2D, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);

    const float Shape = ShapeXY + ShapeDetail;

    float RootDensity = 0.f;
    if (SC.bEnableHangingRoots && tCenter < -0.25f) {
      const float RootZNorm = FMath::Clamp((-tCenter - 0.25f) / 0.75f, 0.f, 1.f);
      const float RootNoise = FMath::Max(0.f, FBM(WX * SC.RootFrequency, WY * SC.RootFrequency, WZ * SC.RootFrequency, 2, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves));
      RootDensity = RootNoise * (1.f - RootZNorm) * 0.4f * Falloff;
    }

    const float HorizStrength = FMath::SmoothStep(Cache.Threshold, Cache.Threshold + 0.4f, Shape);
    float D = HorizStrength * Falloff * 2.5f - (1.f - Falloff) * 1.8f + RootDensity;

    // BREAKUP STRENGTH FIX for shards:
    // Old: BreakUpStrength based only on HeightNorm → shards get 0.50, which strips
    //      material from all sides of a thin shape → leaves thin spike tips (pillar artifact).
    // New: Shards (ShardT=0) get minimal breakup (0.10) — they are rocks with irregular
    //      surface from 3D noise, not eroded islands. The ShardT lerp means only high-terrain
    //      islands get the full HeightNorm-scaled breakup for their organic eroded look.
    const float MaxBreakUp     = FMath::Lerp(0.50f, 2.80f, Cache.HeightNorm);
    const float BreakUpStrength = FMath::Lerp(0.10f, MaxBreakUp, Cache.ShardT);
    const float BreakUp = FMath::Max(0.f, FastNoise3D(WX_base * 0.002f, WY_base * 0.002f, WZ * 0.001f)) * BreakUpStrength;
    
    // FIX: Mask breakup on island tops to protect flat plates from forming vertical swiss-cheese holes.
    float PlateauMask = 1.0f;
    if (Cache.ShardT > 0.5f && tCenter > 0.0f) {
        // Safe taper range: fully protects core top center (tCenter -> 1.0) 
        PlateauMask = FMath::SmoothStep(0.15f, 0.45f, 1.0f - tCenter);
    }
    D -= BreakUp * PlateauMask;

    return FMath::Clamp(D, -2.f, 2.f);
}

// ============================================================
//  CRYSTAL CAVERNS - deep underground carved chambers
// ============================================================
float FVoxelBiomeGenerators::GetCrystalCavernDelta(
    float X, float Y, float Z, float SurfaceHeight,
    const FVoxelGenerationConfig &Config) {
  const FCrystalCavernsConfig &CVC = Config.CaveCrystals;
  const FVector Off = Config.GetSeedOffset();
  const float nX = X + Off.X, nY = Y + Off.Y, nZ = Z + Off.Z;

  const float CavernCeiling = SurfaceHeight - CVC.DepthStart;
  if (Z > CavernCeiling)
    return 0.f;

  // --- ⚡ Optimization: Floor gate avoids running 3D noise for endless depths
  // ---
  const float DepthEndLimit =
      CVC.FadeDepth + 8000.f; // Max thickness of cavern layer list
  if (Z < CavernCeiling - DepthEndLimit)
    return 0.f;

  const float Fade =
      FMath::Clamp((CavernCeiling - Z) / CVC.FadeDepth, 0.f, 1.f);

  // Enforce minimum frequency so chamber pattern varies within a chunk;
  // prevents one chamber from filling an entire chunk and creating a single
  // void.
  const float MinChamberFreq = 0.00005f;
  const float CF = FMath::Max(CVC.ChamberFrequency, MinChamberFreq);

  const float Ch1 = FMath::Abs(FBM(nX * CF, nY * CF, nZ * CF, 4, 2.0f, 0.5f,
                                   Config.Performance.MaxNoiseOctaves));
  const float Ch2 =
      FMath::Abs(FBM(nX * CF * 0.7f, nY * CF * 0.7f, nZ * CF + 5678.f, 3, 2.1f,
                     0.5f, Config.Performance.MaxNoiseOctaves));

  float CarveFactor =
      FMath::Max(0.f, CVC.ChamberThreshold - FMath::Min(Ch1, Ch2)) *
      CVC.ChamberStrength;

  float Veins = 0.f;
  if (CVC.bEnableConnectingVeins) {
    const float VeinNoise =
        FBM(nX * CF * 2.5f, nY * CF * 2.5f, nZ * CF * 2.5f, 2, 2.0f, 0.5f,
            Config.Performance.MaxNoiseOctaves);
    Veins = FMath::Pow(FMath::Max(0.f, 1.f - FMath::Abs(VeinNoise)),
                       CVC.VeinPower) *
            CVC.VeinStrength;
  }

  CarveFactor = FMath::Clamp(CarveFactor + Veins, 0.f, 1.5f);

  const float Detail = FastNoise3D(nX * CVC.CrystalDetailFrequency,
                                   nY * CVC.CrystalDetailFrequency,
                                   nZ * CVC.CrystalDetailFrequency);
  const float CrystalFill =
      FMath::Max(0.f, Detail - CVC.CrystalThreshold) * CVC.CrystalAmplitude;

  // --- 💎 CRYSTAL PLACEMENT FIX ---
  // Scale CrystalFill by CarveFactor so geometry only forms inside the chamber 
  // without exceeding the carved magnitude threshold to seal the wall plates.
  const float NetDelta = -(CarveFactor * 1.3f) + (CrystalFill * FMath::Clamp(CarveFactor, 0.f, 1.f));
  return NetDelta * Fade;
}
