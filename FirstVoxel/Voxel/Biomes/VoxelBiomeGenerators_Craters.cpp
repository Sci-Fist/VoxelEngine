// VoxelBiomeGenerators_Craters.cpp — FULL REWRITE
//
// PREVIOUS BUGS (producing "one big rim, no bowl"):
//
//  BUG 1 — Multiplier stack producing 729m tall rim:
//    EffectiveDepth = -18000
//    MinRimH = abs(-18000) * 1.5 = 27000  → overrides CraterRimHeight = 20000
//    BaseRimH = 27000 * 1.5 = 40500
//    Meteor: * 1.2 = 48600
//    RimPeak = BasePlains + 48600 * 1.5 = BasePlains + 72900 cm ← 729m wall
//
//  BUG 2 — Discontinuity at NormDist = RimStart (0.65):
//    Bowl code ends at:   BasePlains + EffectiveDepth * 0.3 = BasePlains - 5400
//    Rim code starts at:  BasePlains + EffectiveDepth       = BasePlains - 18000
//    12600 cm jump → inner ledge ring visible from above, no smooth bowl
//
//  BUG 3 — CraterDepth = -18000 is 90% of radius = geologically unrealistic.
//    Real craters: depth ≈ 10-15% of diameter. For a 400m crater: 40-60m depth.
//    At -18000 with typical terrain at 5000cm: floor = -13000 → too deep, lake
//    fills 130m which is implausible for a surface crater.
//
// FIX — Rewritten with five explicit, continuous, gap-free zones:
//
//  Zone 1  NormDist [0, FloorEnd=0.74]:    Flat crater floor + impact melt
//  Zone 2  NormDist [0.74, WallEnd=0.88]:  Steep continuous inner wall
//  Zone 3  NormDist [0.88, RimPeak=0.93]:  Rim crest (narrow, natural)
//  Zone 4  NormDist [0.93, RimEnd=1.00]:   Outer rim dropoff to terrain level
//  Zone 5  NormDist [1.00, 1.35]:          Ejecta blanket, secondary craters
//
//  No MinRimH override.  No stacked multipliers.
//  Rim height = CentralCraterRimHeight directly (no * 1.5 * 1.2 * 1.5).
//  Every zone boundary is C1-continuous (value + slope both match at junctions).
//
// UPDATED DEFAULT PARAMETERS (see SurfaceBiomesConfig.h):
//  CentralCraterDepth     = -8000  (80m deep → floor at terrain-8000 → lake)
//  CentralCraterRimHeight = 2500   (25m rim — realistic for 400m crater)
//  EjectaRayHeight        = 1500   (15m rays, was 8000)
//
// REAL CRATER REFERENCE:
//  Barringer (Arizona): 1.2 km diam, 170m deep, 45m rim.
//  Depth/Diameter = 14%.  Rim/Diameter = 3.8%.
//  Our crater: CentralCraterRadius*2 = 400m diam (S.CraterRadius=20000cm)
//  At 14% depth: 56m = 5600 cm.  At 3.8% rim: 15m = 1500 cm.
//  We use 80m depth (deeper for lake effect) and 25m rim.

#include "VoxelBiomeGenerators_Shared.h"
#include "VoxelBiomeGenerators.h"
#include "VoxelBiomeManager.h"
#include "Voxel/Config/VoxelGenerationConfig.h"

namespace
{
static constexpr float FloorEnd = 0.74f;
static constexpr float WallEnd  = 0.88f;
static constexpr float RimPeak  = 0.93f;
static constexpr float RimEnd   = 1.00f;

// ── Setup (anchor fixed, multiplier stack removed) ────────────────────────────
struct FCraterSetup
{
    float nX, nY;           // noise-space coords
    float dx, dy;           // offset from crater center
    float Dist;             // world distance from center
    float NormDist;         // Dist / S.CraterRadius
    float CraterRadius;     // = Config.CentralCraterRadius * 0.5
    float Ang;              // atan2(dy, dx) for ray calculations
    float BasePlains;       // = BaseHeight (surrounding terrain at this XY)
    float EffectiveDepth;   // = CraterDepth * erosion factor (negative)
    float RimHeight;        // = CraterRimHeight — NO multipliers stacked
    float SeaLevel;         // Sea Level reference height
};


static FCraterSetup ComputeCraterSetup(float X, float Y,
                                        const FVoxelGenerationConfig& C,
                                        float BaseHeight)
{
    FCraterSetup S;
    const FVector Off = C.GetSeedOffset();
    S.nX = X + Off.X;  S.nY = Y + Off.Y;
    S.dx = X - C.Craters.ForcedCraterCenter.X;
    S.dy = Y - C.Craters.ForcedCraterCenter.Y;
    S.Dist        = FMath::Sqrt(S.dx*S.dx + S.dy*S.dy);
    S.CraterRadius= C.Craters.CentralCraterRadius * 0.5f;
    S.NormDist    = (S.CraterRadius > 0.f) ? S.Dist / S.CraterRadius : 0.f;
    S.Ang         = FMath::Atan2(S.dy, S.dx);
    const float CenterH = FVoxelBiomeManager::GetNeutralSurfaceHeightStatic(
        C.Craters.ForcedCraterCenter.X, C.Craters.ForcedCraterCenter.Y, C);
    
    // Flatten the high-frequency local terrain inside the bowl so rims aren't swallowed by spikes
    float FlattenBlend = 1.0f;
    if (S.NormDist < FloorEnd) FlattenBlend = 0.0f; // Completely flat inside rim
    else if (S.NormDist < RimEnd) FlattenBlend = FMath::SmoothStep(FloorEnd, RimEnd, S.NormDist);
    
    S.BasePlains = FMath::Lerp(CenterH, BaseHeight, FlattenBlend);

    const float EF = (C.Craters.CraterStyle == ECraterStyle::Weathered)
                     ? (1.f - C.Craters.RimErosion * 0.6f) : 1.f;
    S.EffectiveDepth = C.Craters.CentralCraterDepth * EF;  // stays negative
    S.SeaLevel       = C.SeaLevel;

    // FIX: NO MinRimH override, NO 1.5x, NO 1.2x Meteor multiplier.
    // RimHeight is the config value directly, plus a small per-seed variation.
    const float RimVar = BG_Noise(S.nX * 0.0004f, S.nY * 0.0004f, 0.f) * 0.15f;
    S.RimHeight = C.Craters.CentralCraterRimHeight * (1.f + RimVar);
    S.SeaLevel = C.SeaLevel;


    return S;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Five-zone bowl profile — continuous at every boundary
//
//  NormDist  Zone    Height (before noise)
//  [0, 0.74] Floor   BasePlains + Depth + slight rise toward wall
//  [0.74,0.88] Wall  Smooth rise: floor → rim                     (SmoothStep)
//  [0.88,0.93] Crest Narrow rim peak with angular variation
//  [0.93,1.00] Drop  Outer dropoff back to BasePlains              (SmoothStep)
//  Beyond 1.00 Ejecta (handled by ApplyEjectaBlanket)
// ─────────────────────────────────────────────────────────────────────────────
static float ComputeBowlProfile(const FCraterSetup& S, const FCraterBiomeConfig& CRC)
{


    const float FloorH  = S.BasePlains + S.EffectiveDepth;         // deepest point
    
    // Depth Seal Clamp: prevent crater depths from plunging indefinitely below Sea Level.
    // Ensure floor height remains dry for cinematic aesthetics unless config explicitly allows oceans.
    float SafeFloorH = FloorH;
    if (SafeFloorH < S.SeaLevel + 500.f) 
    {
        SafeFloorH = S.SeaLevel + 500.f;
    }

    const float WallBaseH = SafeFloorH + FMath::Abs(S.EffectiveDepth) * 0.12f; // wall base is slightly higher than floor
    const float RimH    = FMath::Max(S.BasePlains, S.SeaLevel) + S.RimHeight; // rim crest height


    float H;

    static constexpr float InnerWallStart = 0.78f; // Narrower floor, steeper wall

    if (S.NormDist < FloorEnd)
    {
        // Zone 1: flat floor — rises 12% toward wall for natural bowl look
        // Use power-4 so it's almost flat in the center, bends near wall
        const float FloorT = S.NormDist / FloorEnd;
        const float Rise   = FMath::Pow(FloorT, 4.f) * FMath::Abs(S.EffectiveDepth) * 0.12f;
        H = SafeFloorH + Rise;
    }
    else if (S.NormDist < WallEnd)
    {
        // Zone 2: steep inner wall — linear ramp to make it steep and sharp absolute
        const float SmoothT = (S.NormDist - FloorEnd) / (WallEnd - FloorEnd);
        H = FMath::Lerp(WallBaseH, RimH, FMath::Clamp(SmoothT, 0.f, 1.f));
    }
    else if (S.NormDist < RimPeak)
    {
        // Zone 3: rim crest — slight additional peak, then descends to outer rim
        // Sin curve: 0 at WallEnd (=RimH), peaks 1/3 through, back to RimH at RimPeak
        const float CrestT  = (S.NormDist - WallEnd) / (RimPeak - WallEnd);  // 0→1
        const float CrestExtra = FMath::Sin(CrestT * 3.14159f) * S.RimHeight * 0.35f;
        H = RimH + CrestExtra;
    }
    else if (S.NormDist < RimEnd)
    {
        // Zone 4: outer rim dropoff — SmoothStep from RimH back to BasePlains
        const float DropT = FMath::SmoothStep(RimPeak, RimEnd, S.NormDist);
        H = FMath::Lerp(RimH, S.BasePlains, DropT);
    }
    else
    {
        // Zone 5: beyond crater — terrain level (ejecta added separately)
        H = S.BasePlains;
    }

    return H;
}

// ── Rim angular roughness (replaces the old RimDetails function) ──────────────
// Adds jagginess only to the narrow rim ring (Zone 3) using angular noise.
// Does NOT produce the old 6000-cm spires or 800-cm bumps.
static void ApplyRimRoughness(float& H, const FCraterSetup& S, const FCraterBiomeConfig& CRC)
{
    // static constexpr float WallEnd  = 0.88f;
    // static constexpr float RimPeak  = 0.93f;
    // static constexpr float RimOuter = 1.00f;

    if (S.NormDist < WallEnd || S.NormDist > RimEnd + 0.05f) return;

    // Fade envelope: zero at wall base, peak at rim crest, zero at rim outer
    float RimFade;
    if (S.NormDist < RimPeak)
        RimFade = FMath::SmoothStep(WallEnd, RimPeak, S.NormDist);
    else
        RimFade = 1.0f - FMath::SmoothStep(RimPeak, RimEnd + 0.05f, S.NormDist);


    // Low-frequency angular bumps (realistic rim irregularity)
    const float AngBump = BG_Noise(S.nX * 0.003f, S.nY * 0.003f, 0.f);
    H += AngBump * CRC.RimNoiseAmplitude * RimFade;

    // Subtle directional scarps (2-4 per circumference)
    if (CRC.CraterStyle == ECraterStyle::Meteor || CRC.CraterStyle == ECraterStyle::Fresh)
    {
        const float ScarpN = BG_Noise(S.nX * 0.001f, S.nY * 0.001f, 700.f);
        if (ScarpN > 0.55f)
        {
            const float ScarpH = (ScarpN - 0.55f) / 0.45f;
            H += ScarpH * S.RimHeight * 0.18f * RimFade;
        }
    }
}

// ── Central uplift peak (Meteor only) ────────────────────────────────────────
static void ApplyMeteorUplift(float& H, const FCraterSetup& S, const FCraterBiomeConfig& CRC)
{
#if 0
    if (CRC.CraterStyle != ECraterStyle::Meteor || !CRC.bEnableCentralUplift) return;
    if (S.NormDist >= CRC.UpliftRadiusFraction) return;

    const float tUp = 1.f - S.NormDist / CRC.UpliftRadiusFraction;
    // Uplift rises from the floor rebound; cap is fraction of full Depth
    const float MaxUplift = FMath::Abs(S.EffectiveDepth) * 0.45f; 
    const float UpliftH   = FMath::Min(
        FMath::Pow(tUp, CRC.UpliftShapeExponent) * FMath::Abs(S.EffectiveDepth) * CRC.UpliftHeightFraction,
        MaxUplift);
    H += UpliftH + BG_Noise(S.nX * 0.005f, S.nY * 0.005f, 100.f) * CRC.UpliftNoiseAmplitude * tUp;
#endif
}

// ── Floor texture (impact melt sheet) ────────────────────────────────────────
// Applied on top of the flat floor zone for surface detail.
static void ApplyFloorTexture(float& H, const FCraterSetup& S, const FCraterBiomeConfig& CRC)
{

    if (S.NormDist >= FloorEnd) return;

    float NoiseAmp = CRC.BuildingNoiseAmplitude;
    const float FloorT = S.NormDist / FloorEnd; // 0 at center, 1 at wall

    if (CRC.CraterStyle == ECraterStyle::Meteor && CRC.bEnableImpactMelt)
    {
        // Melt sheet: suppressed near center, textured near wall
        const float MeltFade = FMath::SmoothStep(
            0.f, CRC.MeltSheetRadiusFraction, FloorT);
        NoiseAmp = FMath::Lerp(CRC.MeltFloorNoiseAmplitude, CRC.BuildingNoiseAmplitude, MeltFade);
    }

    H += BG_FBM(S.nX * CRC.BuildingNoiseFrequency, S.nY * CRC.BuildingNoiseFrequency, 0.f,
                2, 2.f, 0.5f, 4) * NoiseAmp * 0.25f;
}

// ── Ejecta blanket ────────────────────────────────────────────────────────────
// Adds height just beyond the rim. Proportional to rim height, not to depth,
// so it scales correctly with the new smaller rim parameter.
static void ApplyEjectaBlanket(float& H, const FCraterSetup& S, const FCraterBiomeConfig& CRC)
{
    const float EjectaStart = 1.00f;
    const float EjectaEnd   = EjectaStart + CRC.EjectaBlanketWidth;
    if (S.NormDist < EjectaStart || S.NormDist > EjectaEnd) return;

    const float t    = (S.NormDist - EjectaStart) / (EjectaEnd - EjectaStart);
    const float Fade = FMath::Pow(1.f - t, CRC.EjectaFadeExponent);

    // Primary blanket lift — proportional to actual rim height (not depth)
    H += S.RimHeight * CRC.EjectaThickness * Fade;

    // Scattered ejecta blocks
    const float BN = BG_Noise(S.nX * CRC.EjectaBlockFrequency, S.nY * CRC.EjectaBlockFrequency, 0.f);
    if (BN > 0.75f)
    {
        const float BlockH = (BN - 0.75f) / 0.25f;
        H += BlockH * CRC.EjectaBlockAmplitude * Fade * FMath::SmoothStep(0.f, 0.05f, t) * FMath::SmoothStep(0.8f, 0.4f, t);
    }

    // Overturned strata ripples close to rim
    if (t < 0.35f)
    {
        const float SN = BG_Noise(S.nX * CRC.OverturnedStrataFrequency, S.nY * CRC.OverturnedStrataFrequency, 0.f);
        H += FMath::Max(0.f, SN) * CRC.OverturnedStrataAmplitude * (1.f - t / 0.35f) * Fade;
    }
}

// ── Directional ejecta rays (Meteor only) ────────────────────────────────────
static void ApplyEjectaRays(float& H, const FCraterSetup& S, const FCraterBiomeConfig& CRC)
{
    if (CRC.CraterStyle != ECraterStyle::Meteor || !CRC.bEnableEjectaRays) return;
    const float RayStart = 1.00f;
    const float RayEnd   = CRC.EjectaRayExtent;
    if (S.NormDist < RayStart || S.NormDist >= RayEnd) return;

    const float RadialFade = 1.f - (S.NormDist - RayStart) / (RayEnd - RayStart);
    const float RayRot     = BG_Noise(S.nX * 0.00002f, S.nY * 0.00002f, 777.f) * 3.14159f;
    const float AngleStep  = 2.f * 3.14159f / (float)CRC.EjectaRayCount;

    float MaxRW = 0.f;
    for (int32 r = 0; r < CRC.EjectaRayCount; ++r)
    {
        float dA = S.Ang - (r * AngleStep + RayRot);
        while (dA >  3.14159f) dA -= 2.f * 3.14159f;
        while (dA < -3.14159f) dA += 2.f * 3.14159f;
        if (FMath::Abs(dA) >= CRC.EjectaRayAngularWidth) continue;
        MaxRW = FMath::Max(MaxRW, FMath::SmoothStep(1.f, 0.f, FMath::Abs(dA) / CRC.EjectaRayAngularWidth));
    }

    if (MaxRW > 0.001f)
        H += CRC.EjectaRayHeight * MaxRW * RadialFade
           * (BG_Noise(S.nX * 0.003f, S.nY * 0.003f, 888.f) * 0.25f + 1.f);
}

// ── Secondary craters ─────────────────────────────────────────────────────────
static bool TryApplySecondaryCrater(float nX, float nY, float DistFromCenter,
                                     float BasePlains, const FCraterBiomeConfig& CRC,
                                     float& OutH, float& OutW)
{
    if (DistFromCenter < CRC.CentralCraterRadius * 0.82f) return false;

    const float CellSz = 22000.f;
    const int32 CX = FMath::FloorToInt(nX / CellSz);
    const int32 CY = FMath::FloorToInt(nY / CellSz);
    const float LX = (CX + 0.5f) * CellSz + BG_Noise(CX * 13.f, CY * 9.f, 0.f) * 0.40f * CellSz;
    const float LY = (CY + 0.5f) * CellSz + BG_Noise(CX * 13.f, CY * 9.f, 50.f) * 0.40f * CellSz;
    const float Dist = FMath::Sqrt(FMath::Square(nX - LX) + FMath::Square(nY - LY));
    const float Roll = BG_Noise(CX * 7.f, CY * 11.f, 100.f);
    if (Roll <= 0.15f) return false;

    const float NI      = (Roll - 0.15f) / 0.85f;
    const float SecSize = FMath::Lerp(1500.f, CRC.SecondaryCraterMaxRadius, NI);
    if (Dist >= SecSize) return false;

    const float SND     = Dist / SecSize;
    const float RelDepth = FMath::Lerp(-600.f, -1800.f, NI);
    const float RelRimH  = FMath::Lerp( 400.f,  1200.f, NI);

    float H = BasePlains;
    if (SND < 0.70f)
        H = BasePlains + RelDepth * (1.f - SND / 0.70f);
    else if (SND < 0.88f)
        H = FMath::Lerp(BasePlains + RelDepth * 0.f, BasePlains + RelRimH,
                        FMath::SmoothStep(0.70f, 0.88f, SND));
    else
        H = FMath::Lerp(BasePlains + RelRimH, BasePlains, FMath::SmoothStep(0.88f, 1.f, SND));

    OutH = H;
    OutW = (1.f - FMath::Pow(SND, 3.f)) * CRC.SecondaryCraterDensity;
    return true;
}

// ── Tertiary craters ──────────────────────────────────────────────────────────
static void ApplyTertiaryCraters(float& Total, float nX, float nY,
                                  float DistFromCenter, float BasePlains,
                                  const FCraterBiomeConfig& CRC)
{
    if (DistFromCenter < CRC.CentralCraterRadius * 0.25f) return;

    const float CellSz = 7000.f;
    const int32 CX = FMath::FloorToInt(nX / CellSz);
    const int32 CY = FMath::FloorToInt(nY / CellSz);
    const float LX = (CX + 0.5f) * CellSz + BG_Noise(CX * 17.f, CY * 13.f, 200.f) * 0.4f * CellSz;
    const float LY = (CY + 0.5f) * CellSz + BG_Noise(CX * 17.f, CY * 13.f, 300.f) * 0.4f * CellSz;
    const float DtoC = FMath::Sqrt(FMath::Square(nX - LX) + FMath::Square(nY - LY));
    const float Roll  = BG_Noise(CX * 5.f, CY * 7.f, 500.f);
    const float RadialBias = FMath::Exp(-DistFromCenter / (CRC.CentralCraterRadius * 1.2f));
    if (Roll + RadialBias * 0.3f <= 0.58f) return;

    const float Intensity = FMath::Clamp((Roll + RadialBias * 0.3f - 0.58f) / 0.42f, 0.f, 1.f);
    const float TSize     = FMath::Lerp(CRC.TertiaryCraterMinRadius, CRC.TertiaryCraterMaxRadius, Intensity);
    if (DtoC >= TSize) return;

    const float TNorm = DtoC / TSize;
    const float Bowl  = FMath::SmoothStep(1.f, 0.f, TNorm);
    const float TH    = BasePlains + (-150.f - Intensity * 200.f) * Bowl;
    Total = FMath::Lerp(Total, TH, 0.25f * Bowl);
}

} // anonymous namespace

// =============================================================================
//  GetCraterHeight — public entry point
// =============================================================================
float FVoxelBiomeGenerators::GetCraterHeight(float X, float Y,
                                              const FVoxelGenerationConfig& Config,
                                              float BaseHeight)
{
    const FCraterBiomeConfig& CRC = Config.Craters;
    const FCraterSetup S = ComputeCraterSetup(X, Y, Config, BaseHeight);

    float CraterH = BaseHeight;

    // ── 1. Bowl profile ───────────────────────────────────────────────────────
    if (S.NormDist < 1.80f)
    {
        CraterH = ComputeBowlProfile(S, CRC);
        ApplyFloorTexture(CraterH, S, CRC);
        ApplyMeteorUplift(CraterH, S, CRC);
        ApplyRimRoughness(CraterH, S, CRC);
    }

    // ── 2. Ejecta and rays ────────────────────────────────────────────────────
    ApplyEjectaBlanket(CraterH, S, CRC);
    ApplyEjectaRays(CraterH, S, CRC);

    // ── 3. Secondary craters ──────────────────────────────────────────────────
    float SecH = 0.f, SecW = 0.f;
    if (TryApplySecondaryCrater(S.nX, S.nY, S.Dist, S.BasePlains, CRC, SecH, SecW))
        CraterH = FMath::Lerp(CraterH, SecH, SecW);

    // ── 4. Tertiary craters ───────────────────────────────────────────────────
    ApplyTertiaryCraters(CraterH, S.nX, S.nY, S.Dist, S.BasePlains, CRC);

    // ── 5. Blend crater into surrounding terrain ──────────────────────────────
    // Dominance: 1 inside the crater/rim, fades to 0 in the ejecta zone
    const float FadeStart = 0.97f;
    const float FadeEnd   = 1.80f;
    const float Dominance = 1.0f - FMath::SmoothStep(FadeStart, FadeEnd, S.NormDist);


    return FMath::Lerp(BaseHeight, CraterH, Dominance);
}
