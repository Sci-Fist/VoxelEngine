// VoxelBiomeGenerators.cpp
// Shape-function implementations for all surface biomes, the skylands layer,
// and crystal cavern delta evaluation.
//
// ── CRATER COORDINATE CONTRACT ───────────────────────────────────────────────
//
//  Three systems share a single crater center coordinate stored in
//  GenerationConfig.Craters.ForcedCraterCenter (FVector2D):
//
//    System 1  VoxelBiomeManager::GetBiomeWeightsStatic()
//              Perlin-noise CratersW peaks at a natural XY position per seed.
//
//    System 2  AVoxelWorld::FindCraterSpawnLocation()
//              Scans System 1's weights to locate the noise peak.
//              Stores it in SpawnTargetPos → ConfigureChunk writes it into
//              every chunk's GenerationConfig.Craters.ForcedCraterCenter.
//
//    System 3  FVoxelBiomeGenerators::GetCraterHeight()  (this file)
//              Generates the actual terrain geometry — bowl, rim, ejecta.
//              MUST be centered at ForcedCraterCenter (see ComputeDistances()).
//
//  ForcedCraterCenter is the shared coordinate. It is NOT an artificial
//  override — it is simply the handshake variable that tells the shape function
//  where the natural noise crater was found. If you center it at the world
//  origin instead (dx=X, dy=Y), the bowl appears at (0,0) while the player
//  spawns at the noise peak, producing the "wrong crater" symptom.

#include "VoxelBiomeGenerators.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "VoxelBiomeManager.h"

// =============================================================================
//  NOISE UTILITIES
// =============================================================================

float FVoxelBiomeGenerators::FBM(float X, float Y, float Z, int32 Octaves,
                                  float Lacunarity, float Gain, int32 MaxOctaves)
{
    const int32 ActualOctaves = FMath::Clamp(FMath::Min(Octaves, MaxOctaves), 1, 16);
    float Value = 0.f, Amp = 0.5f, Freq = 1.f;
    for (int32 i = 0; i < ActualOctaves; ++i)
    {
        Value += FastNoise3D(X * Freq, Y * Freq, Z * Freq) * Amp;
        Freq  *= Lacunarity;
        Amp   *= Gain;
    }
    return Value;
}

// =============================================================================
//  FOREST
// =============================================================================
float FVoxelBiomeGenerators::GetForestHeight(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FForestBiomeConfig& FC = Config.Forest;
    const FVector Off = Config.GetSeedOffset();
    const float nX = X + Off.X, nY = Y + Off.Y;

    float Base = FBM(nX * FC.NoiseFrequency, nY * FC.NoiseFrequency, 0.f,
                     FC.Octaves, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);
    float Normalized = (Base + 1.f) * 0.5f;
    float Detail     = FastNoise3D(nX * FC.DetailFrequency, nY * FC.DetailFrequency, 0.f) * FC.DetailAmplitude;
    return Config.SeaLevel + FMath::Lerp(FC.HeightMin, FC.HeightMax, Normalized) + Detail;
}

// =============================================================================
//  DESERT
// =============================================================================
float FVoxelBiomeGenerators::GetDesertHeight(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FDesertBiomeConfig& DC = Config.Desert;
    const FVector Off = Config.GetSeedOffset();
    const float nX = X + Off.X, nY = Y + Off.Y;

    float Base       = FBM(nX * DC.NoiseFrequency, nY * DC.NoiseFrequency, 40.f,
                           DC.Octaves, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);
    float Normalized = (Base + 1.f) * 0.5f;
    float Shaped     = FMath::Pow(FMath::Max(0.f, Normalized), DC.Sharpness);
    float Detail     = FastNoise3D(nX * DC.RippleFrequency, nY * DC.RippleFrequency, 0.f) * DC.RippleAmplitude;
    return Config.SeaLevel + FMath::Lerp(DC.HeightMin, DC.HeightMax, Shaped) + Detail;
}

// =============================================================================
//  PEAKS
// =============================================================================
float FVoxelBiomeGenerators::GetPeaksHeight(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FPeaksBiomeConfig& PC = Config.Peaks;
    const FVector Off = Config.GetSeedOffset();
    const float nX = X + Off.X, nY = Y + Off.Y;

    float Base       = FBM(nX * PC.NoiseFrequency, nY * PC.NoiseFrequency, 10.f,
                           PC.Octaves, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);
    float Normalized = (Base + 1.f) * 0.5f;
    float Shaped     = FMath::Clamp(FMath::Pow(FMath::Clamp(Normalized, 0.f, 1.f), PC.Sharpness), 0.f, 1.f);
    const float MaxDetail = (PC.HeightMax - PC.HeightMin) * 0.02f;
    float Detail = FastNoise3D(nX * PC.NoiseFrequency * 4.f, nY * PC.NoiseFrequency * 4.f, 0.f)
                 * FMath::Min(PC.DetailAmplitude, MaxDetail);
    return Config.SeaLevel + FMath::Lerp(PC.HeightMin, PC.HeightMax, Shaped) + Detail;
}

// =============================================================================
//  CLIFFS
// =============================================================================
float FVoxelBiomeGenerators::GetCliffsHeight(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FCliffsBiomeConfig& CC = Config.Cliffs;
    const FVector Off = Config.GetSeedOffset();
    const float nX = X + Off.X, nY = Y + Off.Y;

    float Base   = FBM(nX * CC.NoiseFrequency, nY * CC.NoiseFrequency, 15.f,
                       CC.Octaves, 2.1f, 0.55f, Config.Performance.MaxNoiseOctaves);
    float Shaped = FMath::Clamp(FMath::Pow(FMath::Abs(Base), CC.Sharpness), 0.f, 1.f);

    if (CC.TerraceSteps > 0 && CC.TerraceFactor > 0.f)
    {
        const float StepScale = (float)CC.TerraceSteps;
        const float Terrace   = FMath::Floor(Shaped * StepScale) / StepScale;
        Shaped = FMath::Lerp(Shaped, Terrace, CC.TerraceFactor);
    }

    float Detail = FastNoise3D(nX * CC.NoiseFrequency * 6.f, nY * CC.NoiseFrequency * 6.f, 0.f) * CC.DetailAmplitude;
    return Config.SeaLevel + FMath::Lerp(CC.HeightMin, CC.HeightMax, Shaped) + Detail;
}

// =============================================================================
//  MESA
// =============================================================================
float FVoxelBiomeGenerators::GetMesaHeight(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FMesaBiomeConfig& MC = Config.Mesa;
    const FVector Off = Config.GetSeedOffset();
    const float nX = X + Off.X, nY = Y + Off.Y;

    const float BasePlains = Config.SeaLevel + MC.HeightBase;
    float Height = BasePlains;

    const float MesaNoise = FBM(nX * MC.MesaFrequency, nY * MC.MesaFrequency, 20.f,
                                4, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);
    const float ButteNoise = FBM(nX * MC.ButteFrequency, nY * MC.ButteFrequency, 40.f,
                                 3, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);

    const float CombinedProfile = FMath::Max((MesaNoise + 1.f) * 0.5f, (ButteNoise + 1.f) * 0.5f);
    const float StepScale = (float)MC.PlateauSteps;
    const float Plateau   = FMath::Floor(CombinedProfile * StepScale) / StepScale;
    const float EdgeBlend = FMath::SmoothStep(0.f, 1.f, (CombinedProfile - Plateau) * (MC.EdgeSharpness + 4.f));
    const float Shape     = Plateau + (FMath::Pow(EdgeBlend, 2.f) / StepScale);
    Height += Shape * (MC.HeightMax - MC.HeightBase);

    const float PillarNoise     = FastNoise3D(nX * MC.PillarFrequency, nY * MC.PillarFrequency, 60.f);
    const float PillarThreshold = 0.65f;
    if (PillarNoise > PillarThreshold)
    {
        const float Intensity    = (PillarNoise - PillarThreshold) / (1.f - PillarThreshold);
        float PillarH            = FMath::Lerp(MC.PillarHeightMin, MC.PillarHeightMax, Intensity);
        const float RadialDist   = FMath::Clamp(1.f - Intensity, 0.f, 1.f);
        PillarH                 *= FMath::Pow(1.f - RadialDist * MC.PillarConicalFactor, 1.5f);
        if (Intensity > 0.85f) PillarH += 500.f;
        Height = FMath::Max(Height, BasePlains + PillarH);
    }

    const float RidgedChannel = 1.f - FMath::Abs(FastNoise3D(nX * MC.ChannelFrequency, nY * MC.ChannelFrequency, 80.f));
    if (RidgedChannel > 0.75f)
        Height -= FMath::SmoothStep(0.f, 1.f, (RidgedChannel - 0.75f) / 0.25f) * MC.ChannelDepth;

    const float LayerScale = Height / MC.LayerThickness;
    const float Fraction   = LayerScale - FMath::Floor(LayerScale);
    if (Fraction > MC.LayerHardness)
    {
        const float DropT = (Fraction - MC.LayerHardness) / (1.f - MC.LayerHardness);
        Height += FMath::Sin(DropT * 3.14159f) * 200.f * MC.LayerVariation;
    }
    else Height += 100.f * MC.LayerVariation;

    if (Height > BasePlains + 2000.f && Shape < 0.3f)
        Height += (FastNoise3D(nX * MC.TalusFrequency, nY * MC.TalusFrequency, 100.f) + 1.f) * 400.f * MC.TalusSpread;

    return Height + FastNoise3D(nX * 0.008f, nY * 0.008f, 0.f) * 160.f;
}

// =============================================================================
//  CRATERS — hierarchical impact crater system
//
//  ── STRUCTURE ────────────────────────────────────────────────────────────────
//
//  GetCraterHeight()                     Public entry point
//    CraterSetup{}                       POD bundle of precomputed values
//    ComputeCraterSetup()                Fills the bundle (distances, base plains)
//    ComputeCentralCraterHeight()        Bowl + rim wall profile
//    ApplyRimDetails()                   Ledges, buttresses, ribs, edge curves
//    ApplyEjectaBlanket()                Ejecta blanket, blocks, overturned strata
//    ApplySecondaryCrater()              One cellular secondary crater
//    ApplyTertiaryCraters()              Cellular tertiary craters (FIXED: no longer ring-based)
//
//  ── TERTIARY FIX ─────────────────────────────────────────────────────────────
//
//  The old code computed tertiary craters using DistFromCenter (distance from
//  the MAIN crater center) as each tertiary crater's own normalized distance.
//  This created concentric rings instead of scattered individual craters, because
//  all tertiary craters shared the same center as the primary.
//
//  Fix: tertiary craters now use the same cellular grid approach as secondary
//  craters — each cell rolls its own center position, creating genuinely
//  scattered small-impact craters across the surrounding landscape.
// =============================================================================

namespace // file-scope helpers — not part of the public API
{
    // -------------------------------------------------------------------------
    //  CraterSetup  — precomputed values shared by all sub-functions
    // -------------------------------------------------------------------------
    struct FCraterSetup
    {
        float nX, nY;           // seed-shifted world positions
        float dx, dy;           // offset from ForcedCraterCenter
        float Dist;             // distance from ForcedCraterCenter
        float NormDist;         // Dist / CraterRadius
        float CraterRadius;     // CRC.CentralCraterRadius * 0.5f
        float BasePlains;       // SeaLevel + 4000 + ambient rolling noise
        float LocalPlains;      // BasePlains with interior floor fade
        float RandomRimHeight;  // BaseRimHeight ± per-position noise
    };



    // -------------------------------------------------------------------------
    //  Central crater — bowl + rim wall profile
    //  Returns the raw height for this point (no blending yet).
    // -------------------------------------------------------------------------
    static float ComputeCentralCraterHeight(const FCraterSetup& S, const FCraterBiomeConfig& CRC)
    {
        const float RimStart = 0.65f;
        const float RimEnd   = 0.92f;

        float H = S.LocalPlains + CRC.CentralCraterDepth; // default: floor
        
        // Base edge noise for continuous wall connection
        const float BaseNoise = FVoxelBiomeGenerators::FastNoise3D(S.nX * 0.003f, S.nY * 0.003f, 0.f) * 150.f;

        if (S.NormDist < RimStart)
        {
            // Bowl — deepest at center, shallower toward the rim base
            const float BowlShape = FMath::Pow(1.f - (S.NormDist / RimStart), 1.8f);
            H = FMath::Lerp(S.LocalPlains + CRC.CentralCraterDepth * 0.3f,
                            S.LocalPlains + CRC.CentralCraterDepth,
                            BowlShape);
            H += BaseNoise * (1.f - BowlShape * 0.5f);
        }
        else if (S.NormDist < RimEnd)
        {
            // Rising rim wall
            const float RimT          = (S.NormDist - RimStart) / (RimEnd - RimStart);
            const float RisingCurve   = FMath::Pow(RimT, 0.5f);
            const float ThinningFactor = 1.f - RimT * 0.6f;
            const float RimPeak        = S.LocalPlains + S.RandomRimHeight * 1.3f;
            
            // Lerp from depth * 0.3f to match bowl edge height
            H = FMath::Lerp(S.LocalPlains + CRC.CentralCraterDepth * 0.3f, RimPeak, RisingCurve);
            H += FMath::Sin(RimT * 3.14159f * 0.5f) * 200.f * ThinningFactor;
            
            // Fade seamless noise up the wall
            H += BaseNoise * FMath::SmoothStep(1.f, 0.f, RimT);
        }
        else
        {
            // Outer slope descending from rim peak
            const float DropT = FMath::SmoothStep(RimEnd, RimEnd + CRC.RimPeakLength * 4.0f, S.NormDist);
            H = FMath::Lerp(S.LocalPlains + S.RandomRimHeight * 1.3f,
                            S.LocalPlains + S.RandomRimHeight * 0.4f,
                            DropT);
        }
        return H;
    }

    // -------------------------------------------------------------------------
    //  Rim detail overlay — ledges, buttresses, wall ribs, curved edge
    //  Modifies height in-place for the rim zone only.
    // -------------------------------------------------------------------------
    static void ApplyRimDetails(float& H, const FCraterSetup& S, const FCraterBiomeConfig& CRC)
    {
        const float RimStart = 0.65f;
        const float RimEnd   = 0.92f;

        const float EdgeNoise = FVoxelBiomeGenerators::FastNoise3D(S.nX * 0.003f, S.nY * 0.003f, 0.f);
        const float EdgeCurve = FMath::Sin(EdgeNoise * 3.14159f) * 500.f;
        float EdgeFade = 0.f;

        if (S.NormDist >= RimStart && S.NormDist <= RimEnd)
        {
            const float RimT = (S.NormDist - RimStart) / (RimEnd - RimStart);
            EdgeFade = RimT;

            // Organic wall roughness
            H += FVoxelBiomeGenerators::FastNoise3D(S.nX * 0.002f, S.nY * 0.002f, 0.f) * CRC.RimNoiseAmplitude * 0.8f
               * FMath::SmoothStep(0.f, 0.1f, RimT) * FMath::Exp(-RimT * 10.f);

            // Horizontal ledge shelves at two elevations
            for (int32 i = 0; i < 2; ++i)
            {
                const float CenterT = (i == 0) ? 0.275f : 0.615f;
                const float FadeW   = (i == 0) ? 0.075f : 0.065f;
                if (FMath::Abs(RimT - CenterT) < FadeW * 2.f)
                {
                    const float LF = FMath::SmoothStep(CenterT - FadeW, CenterT, RimT)
                                   * FMath::SmoothStep(CenterT + FadeW, CenterT, RimT);
                    H = FMath::Lerp(H, S.LocalPlains + S.RandomRimHeight * CenterT, LF * 0.85f);
                }
            }

            // Buttresses — 12 projecting masses around the rim
            const float Ang           = FMath::Atan2(S.dy, S.dx);
            const float ButtressPos   = FMath::Sin(Ang * 12.f);
            const float ButtressNoise = FVoxelBiomeGenerators::FastNoise3D(S.nX * 0.004f, S.nY * 0.004f, 500.f);
            if (ButtressPos > 0.3f && ButtressNoise > 0.1f)
                H += 1600.f * FMath::SmoothStep(0.3f, 0.7f, ButtressPos) * FMath::Sin(RimT * 3.14159f);

            // Wall ribs — occasional horizontal protrusions from erosion
            const float RibNoise = FVoxelBiomeGenerators::FastNoise3D(S.nX * 0.012f, S.nY * 0.012f, 300.f);
            if (RibNoise > 0.4f)
                H += 400.f * FMath::SmoothStep(0.4f, 0.7f, RibNoise) * FMath::Sin(RimT * 3.14159f * 4.f);
        }
        else if (S.NormDist > RimEnd && S.NormDist < RimEnd + 0.05f)
        {
            EdgeFade = 1.f - (S.NormDist - RimEnd) / 0.05f;
        }

        H += EdgeCurve * FMath::SmoothStep(0.f, 1.f, EdgeFade);

        // Rock formations at the floor/wall transition zone
        const float RockMin = 0.65f * 0.75f;
        const float RockMax = 0.65f * 1.25f;
        if (S.NormDist >= RockMin && S.NormDist <= RockMax)
        {
            const float t         = (S.NormDist - RockMin) / (RockMax - RockMin);
            const float RockFade  = FMath::SmoothStep(0.f, 0.4f, t) * FMath::SmoothStep(1.f, 0.6f, t);
            const float RockNoise = FVoxelBiomeGenerators::FastNoise3D(S.nX * 0.006f, S.nY * 0.006f, 0.f);
            if (RockNoise > 0.10f)
                H += (RockNoise - 0.10f) * 800.f * RockFade;
        }

        // Curved stone slabs at the rim crest
        const float RimEnd_ = 0.92f;
        if (S.NormDist >= RimEnd_ && S.NormDist <= RimEnd_ + CRC.RimPeakLength)
        {
            const float t         = (S.NormDist - RimEnd_) / CRC.RimPeakLength;
            const float SlabFade  = FMath::SmoothStep(0.f, 0.1f, t) * FMath::SmoothStep(1.f, 0.9f, t);
            const float TopNoise  = FVoxelBiomeGenerators::FastNoise3D(S.nX * 0.012f, S.nY * 0.012f, 0.f);
            const float Dir       = (TopNoise > 0.3f) ? 1.f : ((TopNoise < -0.3f) ? -1.f : 0.f);
            if (Dir != 0.f)
                H += Dir * FMath::Abs(FMath::Sin(t * 3.14159f * 4.f)) * 12000.f * SlabFade;
        }

        // Jagged micro-peaks just beyond the rim crest
        if (S.NormDist >= RimEnd_ + 0.02f && S.NormDist <= RimEnd_ + 0.07f)
        {
            const float t     = (S.NormDist - (RimEnd_ + 0.02f)) / 0.05f;
            const float Fade  = FMath::SmoothStep(0.f, 0.2f, t) * FMath::SmoothStep(1.f, 0.8f, t);
            const float PNoise = FVoxelBiomeGenerators::FastNoise3D(S.nX * 0.006f, S.nY * 0.006f, 0.f);
            if (PNoise > 0.2f)
                H += (PNoise - 0.2f) * 600.f * Fade;
        }
    }

    // -------------------------------------------------------------------------
    //  Ejecta blanket — material thrown outward beyond the rim
    //  Covers: continuous blanket thickness, large angular blocks, overturned strata,
    //  erosion noise, and rim irregularity noise.
    // -------------------------------------------------------------------------
    static void ApplyEjectaBlanket(float& H, const FCraterSetup& S, const FCraterBiomeConfig& CRC)
    {
        const float RimEnd = 0.92f;

        // Ejecta blanket proper
        if (S.NormDist > RimEnd && S.NormDist <= RimEnd + CRC.EjectaBlanketWidth)
        {
            const float Dist      = S.NormDist - RimEnd;
            const float Fade      = FMath::Pow(1.f - Dist / CRC.EjectaBlanketWidth, CRC.EjectaFadeExponent);
            const float InnerFade = FMath::SmoothStep(0.f, 0.02f, Dist);
            H += CRC.EjectaThickness * FMath::Abs(CRC.CentralCraterDepth) * Fade * 1.2f * InnerFade;

            // Angular ejecta blocks
            const float BlockNoise = FVoxelBiomeGenerators::FastNoise3D(S.nX * CRC.EjectaBlockFrequency, S.nY * CRC.EjectaBlockFrequency, 0.f);
            if (BlockNoise > 0.8f)
            {
                const float BlockFade = FMath::SmoothStep(CRC.EjectaBlanketWidth * 0.8f, CRC.EjectaBlanketWidth * 0.72f, Dist);
                H += (BlockNoise - 0.8f) * CRC.EjectaBlockAmplitude * 2.0f * BlockFade;
            }

            // Overturned strata at crater edge
            const float StrataNoise = FVoxelBiomeGenerators::FastNoise3D(S.nX * CRC.OverturnedStrataFrequency, S.nY * CRC.OverturnedStrataFrequency, 0.f);
            if (StrataNoise > 0.7f)
            {
                const float StrataFade = FMath::SmoothStep(CRC.EjectaBlanketWidth * 0.6f, CRC.EjectaBlanketWidth * 0.55f, Dist);
                H += (StrataNoise - 0.7f) * CRC.OverturnedStrataAmplitude * FMath::Sin(Dist * 10.f) * 1.0f * StrataFade;
            }
        }

        // Erosion noise on the outer rim slope
        if (S.NormDist > RimEnd && S.NormDist < RimEnd + 0.10f)
        {
            const float t    = (S.NormDist - RimEnd) / 0.10f;
            const float Fade = FMath::SmoothStep(0.f, 0.2f, t) * FMath::SmoothStep(1.f, 0.8f, t);
            H += FVoxelBiomeGenerators::FastNoise3D(S.nX * 0.0015f, S.nY * 0.0015f, 0.f) * CRC.RimNoiseAmplitude * 0.4f
               * Fade * FMath::Exp(-(S.NormDist - RimEnd) * 6.f);
        }

        // Rim irregularity noise (slightly wider band)
        if (S.NormDist > RimEnd && S.NormDist < RimEnd + 0.15f)
        {
            const float t    = (S.NormDist - RimEnd) / 0.15f;
            const float Fade = FMath::SmoothStep(0.f, 0.2f, t) * FMath::SmoothStep(1.f, 0.8f, t);
            H += FVoxelBiomeGenerators::FastNoise3D(S.nX * 0.0012f, S.nY * 0.0012f, 0.f) * CRC.RimNoiseAmplitude
               * Fade * FMath::Exp(-(S.NormDist - RimEnd) * 4.f);
        }
    }

    // -------------------------------------------------------------------------
    //  Central crater accumulator context
    // -------------------------------------------------------------------------
    static bool TryApplyCentralCrater(
        float X, float Y, float nX, float nY,
        float SurroundNoise, float BasePlains,
        int32 cX, int32 cY, const FVector& Off,
        const FVoxelGenerationConfig& Config,
        float& OutHeight, float& OutDist)
    {
        const FCraterBiomeConfig& CRC = Config.Craters;
        const float CentralCellSz = 100000.f; 
        
        const float COffX = FVoxelBiomeGenerators::FastNoise3D(cX * 13.f, cY * 11.f, 500.f) * 0.35f * CentralCellSz;
        const float COffY = FVoxelBiomeGenerators::FastNoise3D(cX * 13.f, cY * 11.f, 600.f) * 0.35f * CentralCellSz;
        const float CLocalX = (cX + 0.5f) * CentralCellSz + COffX;
        const float CLocalY = (cY + 0.5f) * CentralCellSz + COffY;
        
        const float AbsoluteCenterX = CLocalX - Off.X;
        const float AbsoluteCenterY = CLocalY - Off.Y;

        const FVoxelBiomeWeightMap CenterW = FVoxelBiomeManager::GetBiomeWeightsStatic(AbsoluteCenterX, AbsoluteCenterY, Config);
        if (CenterW.GetWeight(EVoxelBiome::Craters) <= 0.15f)
            return false;

        FCraterSetup S;
        S.nX = nX; S.nY = nY;
        S.dx = X - AbsoluteCenterX;
        S.dy = Y - AbsoluteCenterY;
        S.Dist = FMath::Sqrt(S.dx * S.dx + S.dy * S.dy);
        S.CraterRadius = CRC.CentralCraterRadius * 0.35f;
        
        if (S.Dist >= S.CraterRadius * 1.5f) return false;

        S.NormDist = S.Dist / S.CraterRadius;
        S.BasePlains = BasePlains;

        const float FloorFade = FMath::SmoothStep(0.65f, 0.0f, S.NormDist);
        S.LocalPlains = Config.SeaLevel + 4000.f + SurroundNoise * (1.f - FloorFade);

        const float MinRimHeight = FMath::Abs(CRC.CentralCraterDepth) * 1.5f;
        const float BaseRimH     = FMath::Max(CRC.CentralCraterRimHeight, MinRimHeight) * 1.5f;
        const float RimVariation = FVoxelBiomeGenerators::FastNoise3D(nX * 0.0005f, nY * 0.0005f, 0.f) * 0.3f;
        S.RandomRimHeight        = BaseRimH * (1.f + RimVariation * 0.2f);

        // Calculate heights
        float CentralH = ComputeCentralCraterHeight(S, CRC);
        ApplyRimDetails(CentralH, S, CRC);
        ApplyEjectaBlanket(CentralH, S, CRC);

        const float FadeStart      = 0.92f + 0.05f;
        const float FadeEnd        = 0.92f + 0.38f;
        const float CentralDominance = FMath::SmoothStep(FadeEnd, FadeStart, S.NormDist);

        OutHeight = FMath::Lerp(BasePlains, CentralH, CentralDominance);
        OutDist = S.Dist;
        return true;
    }

    // -------------------------------------------------------------------------
    //  Secondary craters — grid-cell based independent impacts
    //  Returns true and fills OutHeight/OutWeight if this point is inside
    //  a secondary crater.  Caller blends into TotalHeight.
    // -------------------------------------------------------------------------
    static bool TryApplySecondaryCrater(float nX, float nY, float DistFromCenter,
                                         float BasePlains, const FCraterBiomeConfig& CRC,
                                         float& OutHeight, float& OutWeight)
    {
        // Only consider points outside the primary rim
        if (DistFromCenter <= CRC.CentralCraterRadius * 0.82f) return false;

        const float CellSz = 25000.f; // 250m grid cells
        const int32 CX = FMath::FloorToInt(nX / CellSz);
        const int32 CY = FMath::FloorToInt(nY / CellSz);

        float TotalH = 0.f;
        float TotalW = 0.f;
        bool bAnyApplied = false;

        // Check 3x3 neighboring cells for overlapping secondary craters
        for (int32 dx = -1; dx <= 1; ++dx)
        for (int32 dy = -1; dy <= 1; ++dy)
        {
            const int32 cX = CX + dx;
            const int32 cY = CY + dy;

            const float OffX   = FVoxelBiomeGenerators::FastNoise3D(cX * 13.f, cY *  9.f,  0.f) * 0.38f * CellSz;
            const float OffY   = FVoxelBiomeGenerators::FastNoise3D(cX * 13.f, cY *  9.f, 50.f) * 0.38f * CellSz;
            const float LocalX = (cX + 0.5f) * CellSz + OffX;
            const float LocalY = (cY + 0.5f) * CellSz + OffY;

            const float DistToCenter = FMath::Sqrt(FMath::Square(nX - LocalX) + FMath::Square(nY - LocalY));
            const float ImpactRoll   = FVoxelBiomeGenerators::FastNoise3D(cX * 7.f, cY * 11.f, 100.f);

            if (ImpactRoll <= 0.1f) continue;

            const float NormImpact    = (ImpactRoll - 0.1f) / 0.9f;
            const float SecondarySize = FMath::Lerp(3500.f, CRC.SecondaryCraterMaxRadius, NormImpact) * 1.5f;

            if (DistToCenter >= SecondarySize) continue;

            const float SecNormDist   = DistToCenter / SecondarySize;
            const float SecDepth      = FMath::Lerp(-800.f, -2200.f, NormImpact) * 1.4f;
            const float SecRimH       = FMath::Lerp( 800.f,  2000.f, NormImpact) * 1.4f;
            const float SecRimStart   = 0.65f;
            const float SecRimEnd     = 0.85f;

            float H = BasePlains;
            if (SecNormDist < SecRimStart)
            {
                H = BasePlains + SecDepth;
            }
            else if (SecNormDist < SecRimEnd)
            {
                H = FMath::Lerp(BasePlains + SecDepth, BasePlains + SecRimH,
                                FMath::SmoothStep(SecRimStart, SecRimEnd, SecNormDist));
            }
            else
            {
                H = FMath::Lerp(BasePlains + SecRimH, BasePlains,
                                FMath::SmoothStep(SecRimEnd, 1.f, SecNormDist));
            }

            // Rim roughness
            if (SecNormDist > SecRimStart && SecNormDist < 1.f)
            {
                H += FVoxelBiomeGenerators::FastNoise3D(nX * 0.005f, nY * 0.005f, 0.f) * 300.f
                   * FMath::Sin(SecNormDist * 3.14159f);
            }

            const float W = (1.f - FMath::Pow(SecNormDist, 4.f)) * CRC.SecondaryCraterDensity;
            TotalH += H * W;
            TotalW += W;
            bAnyApplied = true;
        }

        if (!bAnyApplied) return false;

        OutHeight = TotalH / (TotalW + 0.0001f);
        OutWeight = FMath::Clamp(TotalW, 0.f, 1.f);
        return true;
    }

    // -------------------------------------------------------------------------
    //  Tertiary craters — small scattered impacts using cellular positioning
    // -------------------------------------------------------------------------
    static void ApplyTertiaryCraters(float& TotalHeight, float nX, float nY,
                                      float DistFromCenter, float BasePlains,
                                      const FCraterBiomeConfig& CRC)
    {
        if (DistFromCenter <= CRC.CentralCraterRadius * 0.3f) return;

        const float CellSz = 8000.f; // 80m cells
        const int32 CX = FMath::FloorToInt(nX / CellSz);
        const int32 CY = FMath::FloorToInt(nY / CellSz);

        // Check 3x3 neighboring cells for overlapping tertiary craters
        for (int32 dx = -1; dx <= 1; ++dx)
        for (int32 dy = -1; dy <= 1; ++dy)
        {
            const int32 cX = CX + dx;
            const int32 cY = CY + dy;

            // Hash a position inside this cell
            const float OffX   = FVoxelBiomeGenerators::FastNoise3D(cX * 17.f, cY * 13.f, 200.f) * 0.4f * CellSz;
            const float OffY   = FVoxelBiomeGenerators::FastNoise3D(cX * 17.f, cY * 13.f, 300.f) * 0.4f * CellSz;
            const float LocalX = (cX + 0.5f) * CellSz + OffX;
            const float LocalY = (cY + 0.5f) * CellSz + OffY;

            const float DistToCenter = FMath::Sqrt(FMath::Square(nX - LocalX) + FMath::Square(nY - LocalY));

            // Spawn probability with radial bias toward primary crater
            const float ImpactRoll  = FVoxelBiomeGenerators::FastNoise3D(cX * 5.f, cY * 7.f, 500.f);
            const float RadialBias  = FMath::Exp(-DistFromCenter / (CRC.CentralCraterRadius * 0.8f));
            const float BiasedRoll  = ImpactRoll + RadialBias * 0.3f;

            if (BiasedRoll <= 0.4f) continue;

            const float Intensity    = FMath::Clamp((BiasedRoll - 0.4f) / 0.6f, 0.f, 1.f);
            const float TertiarySize = FMath::Lerp(CRC.TertiaryCraterMinRadius,
                                                   CRC.TertiaryCraterMaxRadius, Intensity) * 2.2f;

            if (DistToCenter >= TertiarySize) continue;

            const float TNorm     = DistToCenter / TertiarySize;
            const float TDepth    = -200.f - Intensity * 150.f;
            const float BowlShape = FMath::Pow(1.f - TNorm, 1.3f);
            float TH              = BasePlains + TDepth * BowlShape;

            // Small rim ring
            if (TNorm > 0.1f && TNorm < 0.25f)
                TH += 200.f * FMath::SmoothStep(0.1f, 0.25f, TNorm);

            TotalHeight = FMath::Lerp(TotalHeight, TH, 0.20f);
        }
    }

} // anonymous namespace

// ── Public entry point ────────────────────────────────────────────────────────
float FVoxelBiomeGenerators::GetCraterHeight(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FCraterBiomeConfig& CRC = Config.Craters;
    const FVector Off = Config.GetSeedOffset();
    const float nX = X + Off.X;
    const float nY = Y + Off.Y;

    // 1. Point-specific base context
    const float SurroundNoise = FBM(nX * 0.001f, nY * 0.001f, 0.f, 3, 2.2f, 0.5f) * 850.f;
    const float BasePlains = Config.SeaLevel + 4000.f + SurroundNoise;
    float TotalHeight = BasePlains;
    float ClosestCentralDist = 5000000.f; // deter secondary craters

    // 2. Central crater blends — supports overlapping cellular nodes
    {
        const float CentralCellSz = 100000.f; 
        const int32 CCX = FMath::FloorToInt(nX / CentralCellSz);
        const int32 CCY = FMath::FloorToInt(nY / CentralCellSz);

        for (int32 dx = -1; dx <= 1; ++dx)
        for (int32 dy = -1; dy <= 1; ++dy)
        {
            float LocalH = 0.f;
            float LocalDist = 5000000.f;
            if (TryApplyCentralCrater(X, Y, nX, nY, SurroundNoise, BasePlains, CCX + dx, CCY + dy, Off, Config, LocalH, LocalDist))
            {
                TotalHeight = FMath::Min(TotalHeight, LocalH);
                ClosestCentralDist = FMath::Min(ClosestCentralDist, LocalDist);
            }
        }
    }

    // 3. Secondary craters — cellular, independent bowl impacts
    {
        float SecH = 0.f, SecW = 0.f;
        if (TryApplySecondaryCrater(nX, nY, ClosestCentralDist, BasePlains, CRC, SecH, SecW))
            TotalHeight = FMath::Lerp(TotalHeight, SecH, SecW);
    }

    // 4. Tertiary craters — small scattered cellular impacts (FIXED)
    ApplyTertiaryCraters(TotalHeight, nX, nY, ClosestCentralDist, BasePlains, CRC);

    // 5. Subtle floor texture inside the basin
    if (TotalHeight < BasePlains)
    {
        TotalHeight += FBM(nX * CRC.BuildingNoiseFrequency,
                           nY * CRC.BuildingNoiseFrequency, 0.f,
                           2, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves)
                     * CRC.BuildingNoiseAmplitude * 0.2f;
    }

    return TotalHeight;
}

// =============================================================================
//  SKYLANDS — coherent floating islands
// =============================================================================
float FVoxelBiomeGenerators::GetSkylandDensity(
    float X, float Y, float Z, float SurfaceHeight,
    const FVoxelBiomeWeightMap& Weights, const FVoxelGenerationConfig& Config,
    int32 StepSize)
{
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

    const float ColHeightNorm    = FMath::Clamp(SurfaceHeight / SC.MaxTerrainReference, 0.f, 1.f);
    const float ColRoughnessNorm = FMath::Clamp(Weights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);
    const float ColTerrainStr    = FMath::Clamp(ColHeightNorm * 1.5f + ColRoughnessNorm * 0.8f, 0.f, 1.f);
    const float ShardT           = FMath::SmoothStep(0.f, SC.ShardTransitionStrength, ColTerrainStr);

    const float GridSize = SC.BaseIslandSize * FMath::Lerp(1.f, 4.f, ShardT);
    if (GridSize <= 0.f) return Cache;

    const int32 CellX = FMath::FloorToInt(X / GridSize);
    const int32 CellY = FMath::FloorToInt(Y / GridSize);
    Cache.bHasSkyland = false;

    for (int32 dx = -1; dx <= 1; ++dx)
    for (int32 dy = -1; dy <= 1; ++dy)
    {
        const int32 cX = CellX + dx;
        const int32 cY = CellY + dy;
        const float nX = (float)cX * GridSize + Off.X;
        const float nY = (float)cY * GridSize + Off.Y;

        const float HashX   = (FastNoise3D(nX * 0.001f, nY * 0.001f,   0.f) + 1.f) * 0.5f;
        const float HashY   = (FastNoise3D(nX * 0.001f, nY * 0.001f, 100.f) + 1.f) * 0.5f;
        const float CenterX = (cX + 0.12f + HashX * 0.76f) * GridSize;
        const float CenterY = (cY + 0.12f + HashY * 0.76f) * GridSize;
        const float Dist    = FMath::Sqrt(FMath::Square(X - CenterX) + FMath::Square(Y - CenterY));

        const FVoxelBiomeWeightMap CenterWeights = FVoxelBiomeManager::GetBiomeWeightsStatic(CenterX, CenterY, Config);
        FVoxelBiomeWeightMap NeutralW = CenterWeights;
        NeutralW.SetWeight(EVoxelBiome::Craters, 0.f);
        NeutralW.Normalize();
        const float CenterHeight = FVoxelBiomeManager::GetSurfaceHeightStatic(CenterX, CenterY, NeutralW, Config);

        const float HeightNorm    = FMath::Clamp(CenterHeight / SC.MaxTerrainReference, 0.f, 1.f);
        const float RoughnessNorm = FMath::Clamp(CenterWeights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);
        const float CurvedH       = FMath::Pow(FMath::Max(0.f, HeightNorm), 2.5f);
        const float CurvedR       = FMath::Pow(FMath::Max(0.f, RoughnessNorm), 2.f);
        const float TerrainStr    = FMath::Clamp(HeightNorm * 1.5f + RoughnessNorm * 0.8f, 0.f, 1.f);
        const float CellShardT    = FMath::SmoothStep(0.f, SC.ShardTransitionStrength, TerrainStr);

        const float cnX     = CenterX + Off.X;
        const float cnY     = CenterY + Off.Y;
        const float HashProb = (FastNoise3D(cnX * 0.002f, cnY * 0.002f, 200.f) + 1.f) * 0.5f;

        const float Prob = FMath::Lerp(SC.BaseProbability, SC.BaseProbability + SC.HeightProbabilityBonus, CellShardT);
        if (HashProb > Prob) continue;

        const float ShardMinScale = FMath::Max(0.20f, SC.ShardMinScale);
        const float SizeFactor    = (FBM(cnX * 0.00008f, cnY * 0.00008f, 50.f, 2, 2.f, 0.5f, 2) + 1.f) * 0.5f;
        const float NoiseRange    = FMath::Lerp(0.5f, 0.25f, CellShardT);

        float IslandSize = FMath::Lerp(SC.BaseIslandSize * ShardMinScale,
                                       SC.BaseIslandSize + SC.HeightSizeBonus, CellShardT);
        IslandSize *= (1.f - NoiseRange) + NoiseRange * SizeFactor * 2.f;
        IslandSize  = FMath::Clamp(IslandSize, 150.f, GridSize * 0.48f);

        if (Dist > IslandSize) continue;

        const float AltBase      = FMath::Lerp(SC.MinAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, TerrainStr);
        float SkyAlt             = CenterHeight + AltBase
                                 + CellShardT * (CurvedH * SC.HeightAltitudeBonus + CurvedR * SC.RoughnessAltitudeBonus);

        {
            const float AltGap       = FMath::Max(0.f, SkyAlt - CenterHeight);
            const float AltSizeScale = FMath::Clamp(AltGap / FMath::Max(1.f, SC.MinAltitudeAboveTerrain), 0.4f, 3.f);
            IslandSize *= FMath::Lerp(AltSizeScale, 1.f, CellShardT);
            IslandSize  = FMath::Clamp(IslandSize, 150.f, GridSize * 0.48f);
        }

        {
            const float HA  = FMath::Lerp(0.3f, 1.f, CellShardT);
            const float AAB = AltBase * HA;
            if (CellShardT < 0.3f)
            {
                const float AR = FMath::Lerp(0.2f, 0.7f, CellShardT);
                SkyAlt = CenterHeight + AAB * AR
                       + CellShardT * (CurvedH * SC.HeightAltitudeBonus + CurvedR * SC.RoughnessAltitudeBonus);
            }
            else
            {
                SkyAlt = CenterHeight + AAB
                       + CellShardT * (CurvedH * SC.HeightAltitudeBonus + CurvedR * SC.RoughnessAltitudeBonus);
            }
            SkyAlt += FastNoise3D(cnX * 0.006f, cnY * 0.006f, 500.f) * FMath::Lerp(5000.f, 1500.f, CellShardT);
        }

        const float HashAspect  = (FastNoise3D(cnX * 0.005f, cnY * 0.005f, 300.f) + 1.f) * 0.5f;
        const float ShardThickB = FMath::Lerp(0.12f, 0.25f, HashAspect);
        const float EffThick    = FMath::Lerp(ShardThickB, SC.ThicknessRatio, CellShardT);
        float HalfThick         = FMath::Min(IslandSize * EffThick,
                                             IslandSize * FMath::Lerp(0.75f, SC.MaxThicknessRatio, CellShardT));
        SkyAlt = FMath::Max(SkyAlt, CenterHeight + HalfThick + 200.f);

        const float ShardThreshBoost = FMath::Lerp(0.20f, 0.f, CellShardT);
        float Threshold = FMath::Lerp(SC.ThresholdAtMinProbability, SC.ThresholdAtMaxProbability, CellShardT) + ShardThreshBoost;
        if (CellShardT > 0.5f)
        {
            const float SR = FMath::Max(1.f, IslandSize / SC.BaseIslandSize);
            Threshold -= FMath::Log2(SR) * 0.05f;
        }

        if (FMath::Square(1.f - Dist / IslandSize) > 0.001f)
        {
            FSkylandIslandData Island;
            Island.SkyAlt       = SkyAlt;
            Island.HalfThick    = HalfThick;
            Island.Threshold    = Threshold;
            Island.ShardT       = CellShardT;
            Island.HeightNorm   = HeightNorm;
            Island.ShardFalloff = FMath::Pow(FMath::Max(0.f, TerrainStr), 2.2f);
            Island.IslandSize   = IslandSize;
            const float SR      = FMath::Max(1.f, IslandSize / SC.BaseIslandSize);
            Island.Freq         = FMath::Max(FMath::Lerp(SC.ShapeFrequency * 6.f, SC.ShapeFrequency / SR, CellShardT), 0.00025f);
            Cache.Islands.Add(Island);
            Cache.bHasSkyland = true;
        }
    }

    Cache.WX_base = X + Off.X;
    Cache.WY_base = Y + Off.Y;
    return Cache;
}

float FVoxelBiomeGenerators::GetSkylandDensityFromCache(
    const FSkylandColumnCache& Cache, float X, float Y, float Z,
    const FVoxelGenerationConfig& Config, int32 StepSize)
{
    if (!Cache.bHasSkyland) return -2.f;

    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
    const FVector Off = Config.GetSeedOffset();
    const float WX = Cache.WX_base, WY = Cache.WY_base, WZ = Z + Off.Z;

    float MaxD = -2.f;

    for (const FSkylandIslandData& Island : Cache.Islands)
    {
        const float Margin    = Island.HalfThick * 0.4f;
        const float FullRange = Island.HalfThick + Margin;

        if (Z < Island.SkyAlt - Island.HalfThick - Margin ||
            Z > Island.SkyAlt + Island.HalfThick + Margin) continue;

        const float tCenter = FMath::Clamp((Z - Island.SkyAlt) / (FullRange + 1.f), -1.f, 1.f);

        // Vertical falloff
        float Falloff;
        if (tCenter >= 0.f)
        {
            const float FlatZone = 0.35f;
            Falloff = (tCenter < FlatZone) ? 1.f
                      : FMath::SmoothStep(0.f, 1.f, 1.f - (tCenter - FlatZone) / (1.f - FlatZone));
        }
        else
        {
            Falloff = FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(-tCenter, 0.85f));
        }
        Falloff = FMath::Lerp(FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(FMath::Abs(tCenter), 0.6f)),
                              Falloff, FMath::Max(0.40f, Island.ShardT));

        if (Island.ShardT < 0.3f)
        {
            const float RF = FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(FMath::Abs(tCenter), FMath::Lerp(1.f, 0.6f, Island.ShardT)));
            Falloff = FMath::Lerp(RF, Falloff, FMath::Lerp(0.8f, 0.2f, Island.ShardT));
        }

        if (Falloff < 0.001f)
        {
            const float BU = FMath::Max(0.f, FastNoise3D(WX * 0.002f, WY * 0.002f, WZ * 0.001f))
                           * FMath::Lerp(0.10f, FMath::Lerp(0.50f, 2.80f, Island.HeightNorm), Island.ShardT);
            MaxD = FMath::Max(MaxD, -1.8f - BU);
            continue;
        }

        float QX = WX, QY = WY;
        if (SC.bEnableDomainWarping)
        {
            const float WF = SC.DomainWarpFrequency;
            QX += FastNoise3D(QX * WF + 10.f, QY * WF + 20.f, 0.f) * SC.DomainWarpStrength;
            QY += FastNoise3D(QX * WF + 50.f, QY * WF + 10.f, 0.f) * SC.DomainWarpStrength;
        }

        const float ZFreqScale     = FMath::Lerp(0.50f, 0.05f, Island.ShardT);
        const float DetailStrength = FMath::Lerp(0.55f, 0.25f, Island.ShardT);
        float ShapeDetail = 0.f;
        if (Config.Performance.bEnable3DSkylandNoise || Island.ShardT < 0.5f)
            ShapeDetail = FastNoise3D(QX * Island.Freq * 0.6f, QY * Island.Freq * 0.6f,
                                      WZ * Island.Freq * ZFreqScale) * DetailStrength;

        const int32 Oct2D = FMath::Clamp(FMath::Min((int32)SC.ShapeOctaves, 2), 1, Config.Performance.MaxNoiseOctaves);
        const float ShapeZ = (Island.ShardT < 0.5f) ? WZ * Island.Freq : 0.f;
        const float Shape  = FBM(QX * Island.Freq, QY * Island.Freq, ShapeZ,
                                 Oct2D, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves) + ShapeDetail;

        float RootDensity = 0.f;
        if (SC.bEnableHangingRoots && tCenter < -0.25f)
        {
            const float RootZNorm = FMath::Clamp((-tCenter - 0.25f) / 0.75f, 0.f, 1.f);
            const float RootNoise = FMath::Max(0.f, FBM(QX * SC.RootFrequency, QY * SC.RootFrequency,
                                                        WZ * SC.RootFrequency, 2, 2.f, 0.5f,
                                                        Config.Performance.MaxNoiseOctaves));
            RootDensity = RootNoise * (1.f - RootZNorm) * 0.4f * Falloff;
        }

        const float HorizStrength = FMath::SmoothStep(Island.Threshold, Island.Threshold + 0.4f, Shape);
        float D = HorizStrength * Falloff * 2.5f - (1.f - Falloff) * 1.8f + RootDensity;

        const float MaxBU  = FMath::Lerp(0.50f, 2.80f, Island.HeightNorm);
        const float BUStr  = FMath::Lerp(0.10f, MaxBU, Island.ShardT);
        const float BreakUp = FMath::Max(0.f, FastNoise3D(WX * 0.002f, WY * 0.002f, WZ * 0.001f)) * BUStr;

        float PlateauMask = 1.f;
        if (Island.ShardT > 0.5f && tCenter > 0.f)
            PlateauMask = FMath::SmoothStep(0.15f, 0.45f, 1.f - tCenter);

        D -= BreakUp * PlateauMask;
        MaxD = FMath::Max(MaxD, D);
    }

    return FMath::Clamp(MaxD, -2.f, 2.f);
}

// =============================================================================
//  CRYSTAL CAVERNS
// =============================================================================
float FVoxelBiomeGenerators::GetCrystalCavernDelta(
    float X, float Y, float Z, float SurfaceHeight,
    const FVoxelGenerationConfig& Config)
{
    const FCrystalCavernsConfig& CVC = Config.CaveCrystals;
    const FVector Off = Config.GetSeedOffset();
    const float nX = X + Off.X, nY = Y + Off.Y, nZ = Z + Off.Z;

    const float CavernCeiling = SurfaceHeight - CVC.DepthStart;
    if (Z > CavernCeiling) return 0.f;
    if (Z < CavernCeiling - (CVC.FadeDepth + 8000.f)) return 0.f;

    const float Fade = FMath::Clamp((CavernCeiling - Z) / CVC.FadeDepth, 0.f, 1.f);
    const float CF   = FMath::Max(CVC.ChamberFrequency, 0.00005f);

    const float Ch1 = FMath::Abs(FBM(nX * CF,         nY * CF,         nZ * CF,          4, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves));
    const float Ch2 = FMath::Abs(FBM(nX * CF * 0.7f,  nY * CF * 0.7f,  nZ * CF + 5678.f, 3, 2.1f, 0.5f, Config.Performance.MaxNoiseOctaves));

    float CarveFactor = FMath::Max(0.f, CVC.ChamberThreshold - FMath::Min(Ch1, Ch2)) * CVC.ChamberStrength;

    if (CVC.bEnableConnectingVeins)
    {
        const float VN = FBM(nX * CF * 2.5f, nY * CF * 2.5f, nZ * CF * 2.5f, 2, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves);
        CarveFactor += FMath::Pow(FMath::Max(0.f, 1.f - FMath::Abs(VN)), CVC.VeinPower) * CVC.VeinStrength;
    }

    CarveFactor = FMath::Clamp(CarveFactor, 0.f, 1.5f);

    const float Detail     = FastNoise3D(nX * CVC.CrystalDetailFrequency,
                                         nY * CVC.CrystalDetailFrequency,
                                         nZ * CVC.CrystalDetailFrequency);
    const float CrystalFill = FMath::Max(0.f, Detail - CVC.CrystalThreshold) * CVC.CrystalAmplitude;

    return (-(CarveFactor * 1.3f) + CrystalFill * FMath::Clamp(CarveFactor, 0.f, 1.f)) * Fade;
}
