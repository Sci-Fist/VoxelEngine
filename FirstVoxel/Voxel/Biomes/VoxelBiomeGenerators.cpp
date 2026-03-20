// VoxelBiomeGenerators.cpp
// Shape functions for all surface biomes, skylands, and crystal caverns.
//
// ── CRATER REFACTOR (v2) ──────────────────────────────────────────────────
//  ECraterStyle::Weathered  — eroded rim, filled floor, gentle walls
//  ECraterStyle::Fresh      — sharp rim, open floor, standard ejecta
//  ECraterStyle::Meteor     — spectacular fresh strike:
//    · Central uplift peak (rebound dome at exact impact center)
//    · Impact melt sheet   (smooth glassy floor around the uplift)
//    · Directional ejecta rays (Tycho-style narrow ridges radiating outward)
//
// ── COORDINATE CONTRACT ───────────────────────────────────────────────────
//  ForcedCraterCenter holds the XY of the natural noise crater peak found by
//  FindCraterSpawnLocation(). ConfigureChunk() writes SpawnTargetPos there.
//  GetCraterHeight() measures distances from this point so bowl geometry aligns
//  with the biome weight and player spawn. NEVER replace with dx=X, dy=Y.

#include "VoxelBiomeGenerators.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "VoxelBiomeManager.h"

// =============================================================================
//  NOISE UTILITIES
// =============================================================================
float FVoxelBiomeGenerators::FBM(float X, float Y, float Z, int32 Octaves,
                                  float Lacunarity, float Gain, int32 MaxOctaves)
{
    const int32 N = FMath::Clamp(FMath::Min(Octaves, MaxOctaves), 1, 16);
    float V = 0.f, A = 0.5f, F = 1.f;
    for (int32 i = 0; i < N; ++i)
    {
        V += FastNoise3D(X*F, Y*F, Z*F) * A;
        F *= Lacunarity; A *= Gain;
    }
    return V;
}

// =============================================================================
//  FOREST
// =============================================================================
float FVoxelBiomeGenerators::GetForestHeight(float X, float Y, const FVoxelGenerationConfig& C)
{
    const FForestBiomeConfig& FC = C.Forest;
    const FVector Off = C.GetSeedOffset();
    const float nX = X+Off.X, nY = Y+Off.Y;
    float Base = FBM(nX*FC.NoiseFrequency, nY*FC.NoiseFrequency, 0.f,
                     FC.Octaves, 2.f, 0.5f, C.Performance.MaxNoiseOctaves);
    return C.SeaLevel + FMath::Lerp(FC.HeightMin, FC.HeightMax, (Base+1.f)*0.5f)
           + FastNoise3D(nX*FC.DetailFrequency, nY*FC.DetailFrequency, 0.f)*FC.DetailAmplitude;
}

// =============================================================================
//  DESERT
// =============================================================================
float FVoxelBiomeGenerators::GetDesertHeight(float X, float Y, const FVoxelGenerationConfig& C)
{
    const FDesertBiomeConfig& DC = C.Desert;
    const FVector Off = C.GetSeedOffset();
    const float nX = X+Off.X, nY = Y+Off.Y;
    float Base = FBM(nX*DC.NoiseFrequency, nY*DC.NoiseFrequency, 40.f,
                     DC.Octaves, 2.f, 0.5f, C.Performance.MaxNoiseOctaves);
    float Shaped = FMath::Pow(FMath::Max(0.f, (Base+1.f)*0.5f), DC.Sharpness);
    return C.SeaLevel + FMath::Lerp(DC.HeightMin, DC.HeightMax, Shaped)
           + FastNoise3D(nX*DC.RippleFrequency, nY*DC.RippleFrequency, 0.f)*DC.RippleAmplitude;
}

// =============================================================================
//  PEAKS
// =============================================================================
float FVoxelBiomeGenerators::GetPeaksHeight(float X, float Y, const FVoxelGenerationConfig& C)
{
    const FPeaksBiomeConfig& PC = C.Peaks;
    const FVector Off = C.GetSeedOffset();
    const float nX = X+Off.X, nY = Y+Off.Y;
    float Base = FBM(nX*PC.NoiseFrequency, nY*PC.NoiseFrequency, 10.f,
                     PC.Octaves, 2.f, 0.5f, C.Performance.MaxNoiseOctaves);
    float Shaped = FMath::Clamp(FMath::Pow(FMath::Clamp((Base+1.f)*0.5f,0.f,1.f),PC.Sharpness),0.f,1.f);
    const float MaxDetail = (PC.HeightMax-PC.HeightMin)*0.02f;
    return C.SeaLevel + FMath::Lerp(PC.HeightMin, PC.HeightMax, Shaped)
           + FastNoise3D(nX*PC.NoiseFrequency*4.f, nY*PC.NoiseFrequency*4.f, 0.f)
             * FMath::Min(PC.DetailAmplitude, MaxDetail);
}

// =============================================================================
//  CLIFFS
// =============================================================================
float FVoxelBiomeGenerators::GetCliffsHeight(float X, float Y, const FVoxelGenerationConfig& C)
{
    const FCliffsBiomeConfig& CC = C.Cliffs;
    const FVector Off = C.GetSeedOffset();
    const float nX = X+Off.X, nY = Y+Off.Y;
    float Base = FBM(nX*CC.NoiseFrequency, nY*CC.NoiseFrequency, 15.f,
                     CC.Octaves, 2.1f, 0.55f, C.Performance.MaxNoiseOctaves);
    float Shaped = FMath::Clamp(FMath::Pow(FMath::Abs(Base), CC.Sharpness), 0.f, 1.f);
    if (CC.TerraceSteps>0 && CC.TerraceFactor>0.f)
    {
        const float S = (float)CC.TerraceSteps;
        Shaped = FMath::Lerp(Shaped, FMath::Floor(Shaped*S)/S, CC.TerraceFactor);
    }
    return C.SeaLevel + FMath::Lerp(CC.HeightMin, CC.HeightMax, Shaped)
           + FastNoise3D(nX*CC.NoiseFrequency*6.f, nY*CC.NoiseFrequency*6.f, 0.f)*CC.DetailAmplitude;
}

// =============================================================================
//  MESA
// =============================================================================
float FVoxelBiomeGenerators::GetMesaHeight(float X, float Y, const FVoxelGenerationConfig& C)
{
    const FMesaBiomeConfig& MC = C.Mesa;
    const FVector Off = C.GetSeedOffset();
    const float nX = X+Off.X, nY = Y+Off.Y;
    const float BasePlains = C.SeaLevel + MC.HeightBase;
    float Height = BasePlains;

    const float CombinedProfile = FMath::Max(
        (FBM(nX*MC.MesaFrequency,  nY*MC.MesaFrequency,  20.f, 4, 2.f, 0.5f, C.Performance.MaxNoiseOctaves)+1.f)*0.5f,
        (FBM(nX*MC.ButteFrequency, nY*MC.ButteFrequency, 40.f, 3, 2.f, 0.5f, C.Performance.MaxNoiseOctaves)+1.f)*0.5f);
    const float StepS = (float)MC.PlateauSteps;
    const float Plateau = FMath::Floor(CombinedProfile*StepS)/StepS;
    const float EdgeBlend = FMath::SmoothStep(0.f, 1.f, (CombinedProfile-Plateau)*(MC.EdgeSharpness+4.f));
    Height += (Plateau + FMath::Pow(EdgeBlend,2.f)/StepS) * (MC.HeightMax-MC.HeightBase);

    const float PillarN = FastNoise3D(nX*MC.PillarFrequency, nY*MC.PillarFrequency, 60.f);
    if (PillarN > 0.65f)
    {
        const float I = (PillarN-0.65f)/0.35f;
        float PH = FMath::Lerp(MC.PillarHeightMin, MC.PillarHeightMax, I);
        PH *= FMath::Pow(1.f-FMath::Clamp(1.f-I,0.f,1.f)*MC.PillarConicalFactor, 1.5f);
        if (I > 0.85f) PH += 500.f;
        Height = FMath::Max(Height, BasePlains+PH);
    }

    const float RidgedCh = 1.f - FMath::Abs(FastNoise3D(nX*MC.ChannelFrequency, nY*MC.ChannelFrequency, 80.f));
    if (RidgedCh > 0.75f)
        Height -= FMath::SmoothStep(0.f,1.f,(RidgedCh-0.75f)/0.25f)*MC.ChannelDepth;

    const float Frac = (Height/MC.LayerThickness) - FMath::Floor(Height/MC.LayerThickness);
    if (Frac > MC.LayerHardness)
        Height += FMath::Sin((Frac-MC.LayerHardness)/(1.f-MC.LayerHardness)*3.14159f)*200.f*MC.LayerVariation;
    else Height += 100.f*MC.LayerVariation;

    if (Height > BasePlains+2000.f && CombinedProfile < 0.3f)
        Height += (FastNoise3D(nX*MC.TalusFrequency, nY*MC.TalusFrequency, 100.f)+1.f)*400.f*MC.TalusSpread;

    return Height + FastNoise3D(nX*0.008f, nY*0.008f, 0.f)*160.f;
}

// =============================================================================
//  CRATERS — hierarchical, style-aware
// =============================================================================

namespace
{
// ── Setup bundle ──────────────────────────────────────────────────────────────
struct FCraterSetup
{
    float nX, nY;           // seed-shifted positions
    float dx, dy;           // offset from ForcedCraterCenter
    float Dist;             // ||(dx,dy)||
    float NormDist;         // Dist / CraterRadius
    float CraterRadius;     // CRC.CentralCraterRadius * 0.5
    float Ang;              // atan2(dy,dx) — crater-relative angle
    float BasePlains;       // SeaLevel + 4000 + ambient noise
    float LocalPlains;      // BasePlains with interior floor fade
    float EffectiveDepth;   // style-adjusted crater depth (negative)
    float RandomRimHeight;  // style-adjusted rim height
};

static FCraterSetup ComputeCraterSetup(float X, float Y, const FVoxelGenerationConfig& Config)
{
    FCraterSetup S;
    const FVector Off = Config.GetSeedOffset();
    S.nX = X+Off.X; S.nY = Y+Off.Y;
    S.dx = X - Config.Craters.ForcedCraterCenter.X;
    S.dy = Y - Config.Craters.ForcedCraterCenter.Y;
    S.Dist = FMath::Sqrt(S.dx*S.dx + S.dy*S.dy);
    S.CraterRadius = Config.Craters.CentralCraterRadius * 0.5f;
    S.NormDist = S.Dist / S.CraterRadius;
    S.Ang = FMath::Atan2(S.dy, S.dx);

    const float SurroundNoise = FVoxelBiomeGenerators::FBM(S.nX*0.001f, S.nY*0.001f, 0.f, 3, 2.2f, 0.5f)*1500.f;
    S.BasePlains = Config.SeaLevel + 4000.f + SurroundNoise;

    const float FloorFade = FMath::SmoothStep(0.65f, 0.f, S.NormDist);
    S.LocalPlains = Config.SeaLevel + 4000.f + SurroundNoise*(1.f-FloorFade);

    const FCraterBiomeConfig& CRC = Config.Craters;
    const float ErosionFactor = (CRC.CraterStyle == ECraterStyle::Weathered)
        ? (1.f - CRC.RimErosion * 0.6f) : 1.f;

    S.EffectiveDepth = CRC.CentralCraterDepth * ErosionFactor;

    const float MinRimH = FMath::Abs(S.EffectiveDepth) * 1.5f;
    float BaseRimH = FMath::Max(CRC.CentralCraterRimHeight, MinRimH) * 1.5f;
    if (CRC.CraterStyle == ECraterStyle::Weathered)
        BaseRimH *= (1.f - CRC.RimErosion * 0.5f);
    else if (CRC.CraterStyle == ECraterStyle::Meteor)
        BaseRimH *= 1.2f; // Meteor = tallest rim

    const float RimVariation = FVoxelBiomeGenerators::FastNoise3D(S.nX*0.0005f, S.nY*0.0005f, 0.f)*0.3f;
    S.RandomRimHeight = BaseRimH * (1.f + RimVariation*0.2f);

    return S;
}

// ── Central bowl + rim wall profile ─────────────────────────────────────────
static float ComputeCentralCraterHeight(const FCraterSetup& S, const FCraterBiomeConfig& CRC)
{
    const float RimStart = 0.65f, RimEnd = 0.92f;
    float H = S.LocalPlains + S.EffectiveDepth;

    if (S.NormDist < RimStart)
    {
        const float BowlShape = FMath::Pow(1.f-(S.NormDist/RimStart), 0.6f);
        H = FMath::Lerp(S.LocalPlains+S.EffectiveDepth*0.3f,
                        S.LocalPlains+S.EffectiveDepth, BowlShape);

        // Floor noise — suppressed inside melt sheet for Meteor
        float FloorNoiseAmp = 150.f;
        if (CRC.CraterStyle == ECraterStyle::Meteor && CRC.bEnableImpactMelt)
        {
            const float MeltR = CRC.MeltSheetRadiusFraction;
            const float MeltFade = FMath::SmoothStep(MeltR*0.8f, MeltR, S.NormDist/RimStart);
            FloorNoiseAmp = FMath::Lerp(CRC.MeltFloorNoiseAmplitude, 150.f, MeltFade);
        }
        H += FVoxelBiomeGenerators::FastNoise3D(S.nX*0.003f, S.nY*0.003f, 0.f) * FloorNoiseAmp
           * (1.f - BowlShape*0.5f);
    }
    else if (S.NormDist < RimEnd)
    {
        const float RimT = (S.NormDist-RimStart)/(RimEnd-RimStart);
        const float RimPeak = S.LocalPlains + S.RandomRimHeight*1.5f;
        H = FMath::Lerp(S.LocalPlains+S.EffectiveDepth, RimPeak, FMath::Pow(RimT,0.5f));
        H += FMath::Sin(RimT*3.14159f*0.5f)*200.f*(1.f-RimT*0.6f);
    }
    else
    {
        const float DropT = FMath::SmoothStep(RimEnd, RimEnd+CRC.RimPeakLength, S.NormDist);
        const float RimPeak = S.LocalPlains + S.RandomRimHeight*1.5f;
        H = FMath::Lerp(RimPeak, S.LocalPlains+S.RandomRimHeight*0.4f, DropT);
    }
    return H;
}

// ── Meteor-specific: central uplift peak ─────────────────────────────────────
// A rebound dome forms at the exact center of large impact craters.
// Height = UpliftHeightFraction * |Depth|, profile shape = power curve.
static void ApplyMeteorUplift(float& H, const FCraterSetup& S, const FCraterBiomeConfig& CRC)
{
    if (CRC.CraterStyle != ECraterStyle::Meteor || !CRC.bEnableCentralUplift) return;

    const float UpliftR = CRC.UpliftRadiusFraction; // fraction of CraterRadius
    if (S.NormDist >= UpliftR) return;

    const float tUp = 1.f - S.NormDist/UpliftR;
    const float UpliftH = FMath::Pow(tUp, CRC.UpliftShapeExponent)
                        * FMath::Abs(S.EffectiveDepth) * CRC.UpliftHeightFraction;

    // Noise texture on the uplift surface (rocky rebound dome)
    const float UpliftNoise = FVoxelBiomeGenerators::FastNoise3D(S.nX*0.005f, S.nY*0.005f, 100.f)
                            * CRC.UpliftNoiseAmplitude * tUp;

    H += UpliftH + UpliftNoise;
}

// ── Rim detail overlay (ledges, buttresses, ribs, edge curve) ─────────────────
static void ApplyRimDetails(float& H, const FCraterSetup& S, const FCraterBiomeConfig& CRC)
{
    const float RimStart = 0.65f, RimEnd = 0.92f;

    const float EdgeNoise = FVoxelBiomeGenerators::FastNoise3D(S.nX*0.003f, S.nY*0.003f, 0.f);
    const float EdgeCurve = FMath::Sin(EdgeNoise*3.14159f)*500.f;
    float EdgeFade = 0.f;

    if (S.NormDist >= RimStart && S.NormDist <= RimEnd)
    {
        const float RimT = (S.NormDist-RimStart)/(RimEnd-RimStart);
        EdgeFade = RimT;

        H += FVoxelBiomeGenerators::FastNoise3D(S.nX*0.002f, S.nY*0.002f, 0.f)
           * CRC.RimNoiseAmplitude * 0.8f
           * FMath::SmoothStep(0.f,0.1f,RimT) * FMath::Exp(-RimT*10.f);

        // Ledge shelves
        for (int32 i = 0; i < 2; ++i)
        {
            const float CT = (i==0) ? 0.275f : 0.615f;
            const float FW = (i==0) ? 0.075f : 0.065f;
            if (FMath::Abs(RimT-CT) < FW*2.f)
            {
                const float LF = FMath::SmoothStep(CT-FW,CT,RimT)*FMath::SmoothStep(CT+FW,CT,RimT);
                H = FMath::Lerp(H, S.LocalPlains+S.RandomRimHeight*CT, LF*0.85f);
            }
        }

        const float ButtPos = FMath::Sin(S.Ang*12.f);
        const float ButtN   = FVoxelBiomeGenerators::FastNoise3D(S.nX*0.004f, S.nY*0.004f, 500.f);
        if (ButtPos > 0.3f && ButtN > 0.1f)
            H += 800.f * FMath::SmoothStep(0.3f,0.7f,ButtPos) * FMath::Sin(RimT*3.14159f);

        const float RibN = FVoxelBiomeGenerators::FastNoise3D(S.nX*0.012f, S.nY*0.012f, 300.f);
        if (RibN > 0.4f)
            H += 200.f * FMath::SmoothStep(0.4f,0.7f,RibN) * FMath::Sin(RimT*3.14159f*4.f);
    }
    else if (S.NormDist > RimEnd && S.NormDist < RimEnd+0.05f)
        EdgeFade = 1.f - (S.NormDist-RimEnd)/0.05f;

    H += EdgeCurve * FMath::SmoothStep(0.f,1.f,EdgeFade);

    // Floor-wall rock formations
    const float RockMin = 0.65f*0.75f, RockMax = 0.65f*1.25f;
    if (S.NormDist >= RockMin && S.NormDist <= RockMax)
    {
        const float t = (S.NormDist-RockMin)/(RockMax-RockMin);
        const float RF = FMath::SmoothStep(0.f,0.4f,t)*FMath::SmoothStep(1.f,0.6f,t);
        const float RN = FVoxelBiomeGenerators::FastNoise3D(S.nX*0.006f, S.nY*0.006f, 0.f);
        if (RN > 0.1f) H += (RN-0.1f)*800.f*RF;
    }

    // Curved slabs at rim crest
    if (S.NormDist >= RimEnd && S.NormDist <= RimEnd+CRC.RimPeakLength)
    {
        const float t  = (S.NormDist-RimEnd)/CRC.RimPeakLength;
        const float SF = FMath::SmoothStep(0.f,0.1f,t)*FMath::SmoothStep(1.f,0.9f,t);
        const float TN = FVoxelBiomeGenerators::FastNoise3D(S.nX*0.012f, S.nY*0.012f, 0.f);
        const float Dir = (TN > 0.3f) ? 1.f : ((TN < -0.3f) ? -1.f : 0.f);
        if (Dir != 0.f)
            H += Dir * FMath::Abs(FMath::Sin(t*3.14159f*4.f)) * 6000.f * SF;
    }

    // Jagged micro-peaks
    if (S.NormDist >= RimEnd+0.02f && S.NormDist <= RimEnd+0.07f)
    {
        const float t  = (S.NormDist-(RimEnd+0.02f))/0.05f;
        const float Fade = FMath::SmoothStep(0.f,0.2f,t)*FMath::SmoothStep(1.f,0.8f,t);
        const float PN = FVoxelBiomeGenerators::FastNoise3D(S.nX*0.006f, S.nY*0.006f, 0.f);
        if (PN > 0.2f) H += (PN-0.2f)*600.f*Fade;
    }
}

// ── Ejecta blanket (all styles) ──────────────────────────────────────────────
static void ApplyEjectaBlanket(float& H, const FCraterSetup& S, const FCraterBiomeConfig& CRC)
{
    const float RimEnd = 0.92f;

    if (S.NormDist > RimEnd && S.NormDist <= RimEnd+CRC.EjectaBlanketWidth)
    {
        const float Dist = S.NormDist-RimEnd;
        const float Fade = FMath::Pow(1.f-Dist/CRC.EjectaBlanketWidth, CRC.EjectaFadeExponent);
        H += CRC.EjectaThickness*FMath::Abs(S.EffectiveDepth)*Fade*0.5f
           * FMath::SmoothStep(0.f,0.02f,Dist);

        const float BN = FVoxelBiomeGenerators::FastNoise3D(S.nX*CRC.EjectaBlockFrequency, S.nY*CRC.EjectaBlockFrequency, 0.f);
        if (BN > 0.8f)
            H += (BN-0.8f)*CRC.EjectaBlockAmplitude
               * FMath::SmoothStep(CRC.EjectaBlanketWidth*0.8f, CRC.EjectaBlanketWidth*0.72f, Dist);

        const float SN = FVoxelBiomeGenerators::FastNoise3D(S.nX*CRC.OverturnedStrataFrequency, S.nY*CRC.OverturnedStrataFrequency, 0.f);
        if (SN > 0.7f)
            H += (SN-0.7f)*CRC.OverturnedStrataAmplitude*FMath::Sin(Dist*10.f)*0.5f
               * FMath::SmoothStep(CRC.EjectaBlanketWidth*0.6f, CRC.EjectaBlanketWidth*0.55f, Dist);
    }

    // Outer erosion and rim noise
    const float RimEnd_ = 0.92f;
    if (S.NormDist > RimEnd_ && S.NormDist < RimEnd_+0.10f)
    {
        const float t = (S.NormDist-RimEnd_)/0.10f;
        const float Fade = FMath::SmoothStep(0.f,0.2f,t)*FMath::SmoothStep(1.f,0.8f,t);
        H += FVoxelBiomeGenerators::FastNoise3D(S.nX*0.0015f,S.nY*0.0015f,0.f)*CRC.RimNoiseAmplitude*0.4f
           * Fade * FMath::Exp(-(S.NormDist-RimEnd_)*6.f);
    }
    if (S.NormDist > RimEnd_ && S.NormDist < RimEnd_+0.15f)
    {
        const float t = (S.NormDist-RimEnd_)/0.15f;
        const float Fade = FMath::SmoothStep(0.f,0.2f,t)*FMath::SmoothStep(1.f,0.8f,t);
        H += FVoxelBiomeGenerators::FastNoise3D(S.nX*0.0012f,S.nY*0.0012f,0.f)*CRC.RimNoiseAmplitude
           * Fade * FMath::Exp(-(S.NormDist-RimEnd_)*4.f);
    }
}

// ── Meteor-specific: directional ejecta rays ─────────────────────────────────
// Narrow raised ridges radiating from the rim outward, like Tycho on the Moon.
// Each ray is a Gaussian lobe in angular space, fading linearly with radius.
static void ApplyEjectaRays(float& TotalHeight, const FCraterSetup& S, const FCraterBiomeConfig& CRC)
{
    if (CRC.CraterStyle != ECraterStyle::Meteor || !CRC.bEnableEjectaRays) return;

    const float RimEnd   = 0.92f;
    const float RayStart = RimEnd;                // rays begin at the outer rim edge
    const float RayEnd   = CRC.EjectaRayExtent;   // fraction of CraterRadius
    if (S.NormDist <= RayStart || S.NormDist >= RayEnd) return;

    // Radial fade: full at rim, zero at ray extent
    const float RadialFade = 1.f - (S.NormDist-RayStart)/(RayEnd-RayStart);

    // Ray angle seed — deterministic per seed so each world looks different
    // Use FastNoise3D on a large-scale noise to get a per-crater rotation offset
    const float RayRotOffset = FVoxelBiomeGenerators::FastNoise3D(
        S.nX*0.00002f, S.nY*0.00002f, 777.f) * 3.14159f;

    const float AngleStep = 2.f*3.14159f / (float)CRC.EjectaRayCount;
    float MaxRayWeight = 0.f;

    for (int32 r = 0; r < CRC.EjectaRayCount; ++r)
    {
        const float RayAngle = r * AngleStep + RayRotOffset;
        // Angular distance from this sample to the ray center (wrapped to [-pi, pi])
        float dAng = S.Ang - RayAngle;
        while (dAng >  3.14159f) dAng -= 2.f*3.14159f;
        while (dAng < -3.14159f) dAng += 2.f*3.14159f;

        const float AngDist = FMath::Abs(dAng);
        if (AngDist >= CRC.EjectaRayAngularWidth) continue;

        // Gaussian lobe within the ray angular width
        const float t = AngDist / CRC.EjectaRayAngularWidth;
        const float RayWeight = FMath::SmoothStep(1.f, 0.f, t);
        MaxRayWeight = FMath::Max(MaxRayWeight, RayWeight);
    }

    // Add ray height contribution — only the strongest ray at this point wins
    // (avoids double-stacking where rays are close together)
    if (MaxRayWeight > 0.001f)
    {
        // Noise jitter makes rays look natural rather than perfectly smooth
        const float RayJitter = FVoxelBiomeGenerators::FastNoise3D(
            S.nX*0.003f, S.nY*0.003f, 888.f) * 0.3f + 1.f;
        TotalHeight += CRC.EjectaRayHeight * MaxRayWeight * RadialFade * RayJitter;
    }
}

// ── Secondary craters ────────────────────────────────────────────────────────
static bool TryApplySecondaryCrater(float nX, float nY, float DistFromCenter,
                                     float BasePlains, const FCraterBiomeConfig& CRC,
                                     float& OutH, float& OutW)
{
    if (DistFromCenter <= CRC.CentralCraterRadius * 0.82f) return false;

    const float CellSz = 25000.f;
    const int32 CX = FMath::FloorToInt(nX/CellSz), CY = FMath::FloorToInt(nY/CellSz);
    const float OffX = FVoxelBiomeGenerators::FastNoise3D(CX*13.f,CY* 9.f, 0.f)*0.38f*CellSz;
    const float OffY = FVoxelBiomeGenerators::FastNoise3D(CX*13.f,CY* 9.f,50.f)*0.38f*CellSz;
    const float LX = (CX+0.5f)*CellSz+OffX, LY = (CY+0.5f)*CellSz+OffY;
    const float DtoSec = FMath::Sqrt(FMath::Square(nX-LX)+FMath::Square(nY-LY));
    const float Roll = FVoxelBiomeGenerators::FastNoise3D(CX*7.f,CY*11.f,100.f);
    if (Roll <= 0.1f) return false;

    const float NI = (Roll-0.1f)/0.9f;
    const float SecSize = FMath::Lerp(2000.f, CRC.SecondaryCraterMaxRadius, NI);
    if (DtoSec >= SecSize) return false;

    const float SND = DtoSec/SecSize;
    const float SD = FMath::Lerp(-800.f,-2200.f,NI), SRH = FMath::Lerp(800.f,2000.f,NI);
    float H = BasePlains;
    if (SND < 0.65f) H = BasePlains+SD;
    else if (SND < 0.85f) H = FMath::Lerp(BasePlains+SD, BasePlains+SRH, FMath::SmoothStep(0.65f,0.85f,SND));
    else H = FMath::Lerp(BasePlains+SRH, BasePlains, FMath::SmoothStep(0.85f,1.f,SND));
    if (SND > 0.65f && SND < 1.f)
        H += FVoxelBiomeGenerators::FastNoise3D(nX*0.005f,nY*0.005f,0.f)*300.f*FMath::Sin(SND*3.14159f);
    OutH = H;
    OutW = (1.f-FMath::Pow(SND,4.f))*CRC.SecondaryCraterDensity;
    return true;
}

// ── Tertiary craters — cellular, scattered ────────────────────────────────────
static void ApplyTertiaryCraters(float& Total, float nX, float nY,
                                  float DistFromCenter, float BasePlains,
                                  const FCraterBiomeConfig& CRC)
{
    if (DistFromCenter <= CRC.CentralCraterRadius * 0.3f) return;
    const float CellSz = 8000.f;
    const int32 CX = FMath::FloorToInt(nX/CellSz), CY = FMath::FloorToInt(nY/CellSz);
    const float LX = (CX+0.5f)*CellSz + FVoxelBiomeGenerators::FastNoise3D(CX*17.f,CY*13.f,200.f)*0.4f*CellSz;
    const float LY = (CY+0.5f)*CellSz + FVoxelBiomeGenerators::FastNoise3D(CX*17.f,CY*13.f,300.f)*0.4f*CellSz;
    const float DtoC = FMath::Sqrt(FMath::Square(nX-LX)+FMath::Square(nY-LY));
    const float Roll = FVoxelBiomeGenerators::FastNoise3D(CX*5.f,CY*7.f,500.f);
    const float RadialBias = FMath::Exp(-DistFromCenter/(CRC.CentralCraterRadius*0.8f));
    if (Roll + RadialBias*0.3f <= 0.6f) return;

    const float Intensity = FMath::Clamp((Roll+RadialBias*0.3f-0.6f)/0.4f,0.f,1.f);
    const float TSize = FMath::Lerp(CRC.TertiaryCraterMinRadius, CRC.TertiaryCraterMaxRadius, Intensity);
    if (DtoC >= TSize) return;

    const float TNorm = DtoC/TSize;
    const float Bowl = FMath::Pow(1.f-TNorm,1.3f);
    float TH = BasePlains + (-200.f - Intensity*150.f)*Bowl;
    if (TNorm > 0.1f && TNorm < 0.25f) TH += 200.f*FMath::SmoothStep(0.1f,0.25f,TNorm);
    Total = FMath::Lerp(Total, TH, 0.20f);
}

} // anonymous namespace

// ── Public entry point ────────────────────────────────────────────────────────
float FVoxelBiomeGenerators::GetCraterHeight(float X, float Y, const FVoxelGenerationConfig& Config)
{
    const FCraterBiomeConfig& CRC = Config.Craters;
    const FCraterSetup S = ComputeCraterSetup(X, Y, Config);
    float TotalHeight = S.BasePlains;

    // 1. Central crater
    if (S.Dist < S.CraterRadius * 1.5f)
    {
        const float FadeStart = 0.92f + 0.05f;
        const float FadeEnd   = 0.92f + 0.38f;
        const float Dominance = FMath::SmoothStep(FadeEnd, FadeStart, S.NormDist);

        float CentralH = ComputeCentralCraterHeight(S, CRC);
        ApplyMeteorUplift(CentralH, S, CRC);  // Meteor: dome at center
        ApplyRimDetails(CentralH, S, CRC);
        ApplyEjectaBlanket(CentralH, S, CRC);

        TotalHeight = FMath::Lerp(TotalHeight, CentralH, Dominance);
    }

    // 2. Ejecta rays (Meteor only) — applied to TotalHeight after blending
    ApplyEjectaRays(TotalHeight, S, CRC);

    // 3. Secondary craters
    {
        float SecH = 0.f, SecW = 0.f;
        if (TryApplySecondaryCrater(S.nX, S.nY, S.Dist, S.BasePlains, CRC, SecH, SecW))
            TotalHeight = FMath::Lerp(TotalHeight, SecH, SecW);
    }

    // 4. Tertiary craters
    ApplyTertiaryCraters(TotalHeight, S.nX, S.nY, S.Dist, S.BasePlains, CRC);

    // 5. Floor texture (suppressed in melt zone for Meteor)
    if (TotalHeight < S.BasePlains)
    {
        float NoiseAmp = CRC.BuildingNoiseAmplitude;
        if (CRC.CraterStyle == ECraterStyle::Meteor && CRC.bEnableImpactMelt)
        {
            const float MeltR = CRC.MeltSheetRadiusFraction;
            const float InMelt = FMath::SmoothStep(MeltR, MeltR*0.8f, S.NormDist/0.65f);
            NoiseAmp = FMath::Lerp(NoiseAmp, CRC.MeltFloorNoiseAmplitude, InMelt);
        }
        TotalHeight += FBM(S.nX*CRC.BuildingNoiseFrequency, S.nY*CRC.BuildingNoiseFrequency, 0.f,
                           2, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves)
                     * NoiseAmp * 0.2f;
    }

    return TotalHeight;
}

// =============================================================================
//  SKYLANDS
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

    const float ColTS = FMath::Clamp(
        FMath::Clamp(SurfaceHeight/SC.MaxTerrainReference,0.f,1.f)*1.5f +
        FMath::Clamp(Weights.GetRoughness()/SC.RoughnessReference,0.f,1.f)*0.8f,
        0.f, 1.f);
    const float ShardT = FMath::SmoothStep(0.f, SC.ShardTransitionStrength, ColTS);
    const float GridSize = SC.BaseIslandSize * FMath::Lerp(1.f, 4.f, ShardT);
    if (GridSize <= 0.f) return Cache;

    const int32 CellX = FMath::FloorToInt(X/GridSize), CellY = FMath::FloorToInt(Y/GridSize);
    Cache.bHasSkyland = false;

    for (int32 dx2 = -1; dx2 <= 1; ++dx2)
    for (int32 dy2 = -1; dy2 <= 1; ++dy2)
    {
        const int32 cX = CellX+dx2, cY = CellY+dy2;
        const float nX2 = (float)cX*GridSize+Off.X, nY2 = (float)cY*GridSize+Off.Y;
        const float HX = (FastNoise3D(nX2*0.001f,nY2*0.001f,  0.f)+1.f)*0.5f;
        const float HY = (FastNoise3D(nX2*0.001f,nY2*0.001f,100.f)+1.f)*0.5f;
        const float CX2 = (cX+0.12f+HX*0.76f)*GridSize, CY2 = (cY+0.12f+HY*0.76f)*GridSize;
        const float Dist = FMath::Sqrt(FMath::Square(X-CX2)+FMath::Square(Y-CY2));

        const FVoxelBiomeWeightMap CW = FVoxelBiomeManager::GetBiomeWeightsStatic(CX2,CY2,Config);
        FVoxelBiomeWeightMap NW = CW; NW.SetWeight(EVoxelBiome::Craters,0.f); NW.Normalize();
        const float CH = FVoxelBiomeManager::GetSurfaceHeightStatic(CX2,CY2,NW,Config);

        const float HN = FMath::Clamp(CH/SC.MaxTerrainReference,0.f,1.f);
        const float RN = FMath::Clamp(CW.GetRoughness()/SC.RoughnessReference,0.f,1.f);
        const float CuH = FMath::Pow(FMath::Max(0.f,HN),2.5f);
        const float CuR = FMath::Pow(FMath::Max(0.f,RN),2.f);
        const float TS  = FMath::Clamp(HN*1.5f+RN*0.8f,0.f,1.f);
        const float CST = FMath::SmoothStep(0.f,SC.ShardTransitionStrength,TS);

        const float cnX2 = CX2+Off.X, cnY2 = CY2+Off.Y;
        const float HP = (FastNoise3D(cnX2*0.002f,cnY2*0.002f,200.f)+1.f)*0.5f;
        const float Prob = FMath::Lerp(SC.BaseProbability, SC.BaseProbability+SC.HeightProbabilityBonus, CST);
        if (HP > Prob) continue;

        const float SMN = FMath::Max(0.20f, SC.ShardMinScale);
        const float SF  = (FBM(cnX2*0.00008f,cnY2*0.00008f,50.f,2,2.f,0.5f,2)+1.f)*0.5f;
        const float NR  = FMath::Lerp(0.5f,0.25f,CST);
        float IS = FMath::Lerp(SC.BaseIslandSize*SMN, SC.BaseIslandSize+SC.HeightSizeBonus, CST);
        IS = FMath::Clamp(IS*((1.f-NR)+NR*SF*2.f), 150.f, GridSize*0.48f);
        if (Dist > IS) continue;

        const float AltBase = FMath::Lerp(SC.MinAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, TS);
        float SkyAlt = CH + AltBase + CST*(CuH*SC.HeightAltitudeBonus+CuR*SC.RoughnessAltitudeBonus);
        {
            const float AG = FMath::Max(0.f,SkyAlt-CH);
            IS = FMath::Clamp(IS*FMath::Lerp(FMath::Clamp(AG/FMath::Max(1.f,SC.MinAltitudeAboveTerrain),0.4f,3.f),1.f,CST),150.f,GridSize*0.48f);
            const float HA = AltBase*FMath::Lerp(0.3f,1.f,CST);
            if (CST < 0.3f) SkyAlt = CH+HA*FMath::Lerp(0.2f,0.7f,CST)+CST*(CuH*SC.HeightAltitudeBonus+CuR*SC.RoughnessAltitudeBonus);
            else             SkyAlt = CH+HA+CST*(CuH*SC.HeightAltitudeBonus+CuR*SC.RoughnessAltitudeBonus);
            SkyAlt += FastNoise3D(cnX2*0.006f,cnY2*0.006f,500.f)*FMath::Lerp(5000.f,1500.f,CST);
        }

        const float HA2 = (FastNoise3D(cnX2*0.005f,cnY2*0.005f,300.f)+1.f)*0.5f;
        const float ET  = FMath::Lerp(FMath::Lerp(0.12f,0.25f,HA2), SC.ThicknessRatio, CST);
        float HT = FMath::Min(IS*ET, IS*FMath::Lerp(0.75f,SC.MaxThicknessRatio,CST));
        SkyAlt = FMath::Max(SkyAlt, CH+HT+200.f);

        float Thr = FMath::Lerp(SC.ThresholdAtMinProbability, SC.ThresholdAtMaxProbability, CST)
                  + FMath::Lerp(0.20f,0.f,CST);
        if (CST > 0.5f) Thr -= FMath::Log2(FMath::Max(1.f,IS/SC.BaseIslandSize))*0.05f;

        if (FMath::Square(1.f-Dist/IS) > 0.001f)
        {
            FSkylandIslandData Isl;
            Isl.SkyAlt=SkyAlt; Isl.HalfThick=HT; Isl.Threshold=Thr;
            Isl.ShardT=CST; Isl.HeightNorm=HN; Isl.ShardFalloff=FMath::Pow(FMath::Max(0.f,TS),2.2f);
            Isl.IslandSize=IS;
            const float SR = FMath::Max(1.f,IS/SC.BaseIslandSize);
            Isl.Freq = FMath::Max(FMath::Lerp(SC.ShapeFrequency*6.f, SC.ShapeFrequency/SR, CST), 0.00025f);
            Cache.Islands.Add(Isl);
            Cache.bHasSkyland = true;
        }
    }
    Cache.WX_base = X+Off.X; Cache.WY_base = Y+Off.Y;
    return Cache;
}

float FVoxelBiomeGenerators::GetSkylandDensityFromCache(
    const FSkylandColumnCache& Cache, float X, float Y, float Z,
    const FVoxelGenerationConfig& Config, int32 StepSize)
{
    if (!Cache.bHasSkyland) return -2.f;
    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
    const FVector Off = Config.GetSeedOffset();
    const float WX = Cache.WX_base, WY = Cache.WY_base, WZ = Z+Off.Z;
    float MaxD = -2.f;

    for (const FSkylandIslandData& Isl : Cache.Islands)
    {
        const float Margin = Isl.HalfThick*0.4f;
        if (Z < Isl.SkyAlt-Isl.HalfThick-Margin || Z > Isl.SkyAlt+Isl.HalfThick+Margin) continue;
        const float tC = FMath::Clamp((Z-Isl.SkyAlt)/(Isl.HalfThick+Margin+1.f),-1.f,1.f);

        float Falloff;
        if (tC >= 0.f) { const float FZ=0.35f; Falloff=(tC<FZ)?1.f:FMath::SmoothStep(0.f,1.f,1.f-(tC-FZ)/(1.f-FZ)); }
        else           { Falloff = FMath::SmoothStep(0.f,1.f,1.f-FMath::Pow(-tC,0.85f)); }
        Falloff = FMath::Lerp(FMath::SmoothStep(0.f,1.f,1.f-FMath::Pow(FMath::Abs(tC),0.6f)), Falloff, FMath::Max(0.40f,Isl.ShardT));
        if (Isl.ShardT < 0.3f)
        {
            const float RF = FMath::SmoothStep(0.f,1.f,1.f-FMath::Pow(FMath::Abs(tC),FMath::Lerp(1.f,0.6f,Isl.ShardT)));
            Falloff = FMath::Lerp(RF, Falloff, FMath::Lerp(0.8f,0.2f,Isl.ShardT));
        }

        if (Falloff < 0.001f)
        {
            MaxD = FMath::Max(MaxD, -1.8f - FMath::Max(0.f,FastNoise3D(WX*0.002f,WY*0.002f,WZ*0.001f))
                   * FMath::Lerp(0.10f, FMath::Lerp(0.50f,2.80f,Isl.HeightNorm), Isl.ShardT));
            continue;
        }

        float QX=WX, QY=WY;
        if (SC.bEnableDomainWarping)
        {
            const float WF=SC.DomainWarpFrequency;
            QX += FastNoise3D(QX*WF+10.f,QY*WF+20.f,0.f)*SC.DomainWarpStrength;
            QY += FastNoise3D(QX*WF+50.f,QY*WF+10.f,0.f)*SC.DomainWarpStrength;
        }

        float SD = 0.f;
        const float ZFS=FMath::Lerp(0.50f,0.05f,Isl.ShardT);
        if (Config.Performance.bEnable3DSkylandNoise || Isl.ShardT < 0.5f)
            SD = FastNoise3D(QX*Isl.Freq*0.6f,QY*Isl.Freq*0.6f,WZ*Isl.Freq*ZFS)*FMath::Lerp(0.55f,0.25f,Isl.ShardT);
        const int32 Oct2D = FMath::Clamp(FMath::Min((int32)SC.ShapeOctaves,2),1,Config.Performance.MaxNoiseOctaves);
        const float SZ = (Isl.ShardT<0.5f)?WZ*Isl.Freq:0.f;
        const float Shape = FBM(QX*Isl.Freq,QY*Isl.Freq,SZ,Oct2D,2.f,0.5f,Config.Performance.MaxNoiseOctaves)+SD;

        float RD=0.f;
        if (SC.bEnableHangingRoots && tC<-0.25f)
        {
            const float RZN=FMath::Clamp((-tC-0.25f)/0.75f,0.f,1.f);
            RD = FMath::Max(0.f,FBM(QX*SC.RootFrequency,QY*SC.RootFrequency,WZ*SC.RootFrequency,2,2.f,0.5f,Config.Performance.MaxNoiseOctaves))
               * (1.f-RZN)*0.4f*Falloff;
        }

        float D = FMath::SmoothStep(Isl.Threshold, Isl.Threshold+0.4f, Shape)*Falloff*2.5f
                - (1.f-Falloff)*1.8f + RD;
        const float BU = FMath::Max(0.f,FastNoise3D(WX*0.002f,WY*0.002f,WZ*0.001f))
                       * FMath::Lerp(0.10f, FMath::Lerp(0.50f,2.80f,Isl.HeightNorm), Isl.ShardT);
        float PM = 1.f;
        if (Isl.ShardT>0.5f && tC>0.f) PM = FMath::SmoothStep(0.15f,0.45f,1.f-tC);
        D -= BU*PM;
        MaxD = FMath::Max(MaxD, D);
    }
    return FMath::Clamp(MaxD,-2.f,2.f);
}

// =============================================================================
//  CRYSTAL CAVERNS
// =============================================================================
float FVoxelBiomeGenerators::GetCrystalCavernDelta(float X, float Y, float Z,
                                                     float SurfaceHeight,
                                                     const FVoxelGenerationConfig& Config)
{
    const FCrystalCavernsConfig& CVC = Config.CaveCrystals;
    const FVector Off = Config.GetSeedOffset();
    const float nX=X+Off.X, nY=Y+Off.Y, nZ=Z+Off.Z;
    const float CC = SurfaceHeight-CVC.DepthStart;
    if (Z > CC) return 0.f;
    if (Z < CC-(CVC.FadeDepth+8000.f)) return 0.f;
    const float Fade = FMath::Clamp((CC-Z)/CVC.FadeDepth,0.f,1.f);
    const float CF = FMath::Max(CVC.ChamberFrequency,0.00005f);
    const float Ch1=FMath::Abs(FBM(nX*CF,nY*CF,nZ*CF,4,2.f,0.5f,Config.Performance.MaxNoiseOctaves));
    const float Ch2=FMath::Abs(FBM(nX*CF*0.7f,nY*CF*0.7f,nZ*CF+5678.f,3,2.1f,0.5f,Config.Performance.MaxNoiseOctaves));
    float Carve=FMath::Max(0.f,CVC.ChamberThreshold-FMath::Min(Ch1,Ch2))*CVC.ChamberStrength;
    if (CVC.bEnableConnectingVeins)
    {
        const float VN=FBM(nX*CF*2.5f,nY*CF*2.5f,nZ*CF*2.5f,2,2.f,0.5f,Config.Performance.MaxNoiseOctaves);
        Carve += FMath::Pow(FMath::Max(0.f,1.f-FMath::Abs(VN)),CVC.VeinPower)*CVC.VeinStrength;
    }
    Carve=FMath::Clamp(Carve,0.f,1.5f);
    const float CF2=FMath::Max(0.f,FastNoise3D(nX*CVC.CrystalDetailFrequency,nY*CVC.CrystalDetailFrequency,nZ*CVC.CrystalDetailFrequency)-CVC.CrystalThreshold)*CVC.CrystalAmplitude;
    return (-(Carve*1.3f)+CF2*FMath::Clamp(Carve,0.f,1.f))*Fade;
}
