// VoxelBiomeGenerators_Craters.cpp
// Crater height system: central bowl, rim, uplift, melt sheet, ejecta rays,
// secondary and tertiary craters. Everything crater-shaped lives here.
//
// The public entry point GetCraterHeight(X, Y, Config, BaseHeight) is defined
// at the bottom. All helpers are in an anonymous namespace.

#include "VoxelBiomeGenerators_Shared.h"
#include "VoxelBiomeGenerators.h"
#include "VoxelBiomeManager.h"
#include "Voxel/Config/VoxelGenerationConfig.h"

namespace
{
// ── Setup bundle ──────────────────────────────────────────────────────────────
struct FCraterSetup
{
    float nX, nY;
    float dx, dy;
    float Dist;
    float NormDist;
    float CraterRadius;
    float Ang;
    float BasePlains;
    float LocalPlains;
    float EffectiveDepth;
    float RandomRimHeight;
};

static FCraterSetup ComputeCraterSetup(float X, float Y, const FVoxelGenerationConfig& Config)
{
    FCraterSetup S;
    const FVector Off = Config.GetSeedOffset();
    S.nX = X+Off.X; S.nY = Y+Off.Y;
    S.dx = X - Config.Craters.ForcedCraterCenter.X;
    S.dy = Y - Config.Craters.ForcedCraterCenter.Y;
    S.Dist         = FMath::Sqrt(S.dx*S.dx + S.dy*S.dy);
    S.CraterRadius = Config.Craters.CentralCraterRadius * 0.5f;
    S.NormDist     = S.Dist / S.CraterRadius;
    S.Ang          = FMath::Atan2(S.dy, S.dx);

    const float SurroundNoise = BG_FBM(S.nX*0.001f, S.nY*0.001f, 0.f, 3, 2.2f, 0.5f, Config.Performance.MaxNoiseOctaves)*1500.f;
    S.BasePlains  = Config.SeaLevel + 4000.f + SurroundNoise;
    const float FloorFade = FMath::SmoothStep(0.65f, 0.f, S.NormDist);
    S.LocalPlains = Config.SeaLevel + 4000.f + SurroundNoise*(1.f-FloorFade);

    const FCraterBiomeConfig& CRC = Config.Craters;
    const float ErosionFactor = (CRC.CraterStyle == ECraterStyle::Weathered)
        ? (1.f - CRC.RimErosion*0.6f) : 1.f;
    S.EffectiveDepth = CRC.CentralCraterDepth * ErosionFactor;

    const float MinRimH = FMath::Abs(S.EffectiveDepth)*1.5f;
    float BaseRimH = FMath::Max(CRC.CentralCraterRimHeight, MinRimH)*1.5f;
    if      (CRC.CraterStyle == ECraterStyle::Weathered) BaseRimH *= (1.f-CRC.RimErosion*0.5f);
    else if (CRC.CraterStyle == ECraterStyle::Meteor)    BaseRimH *= 1.2f;

    const float RimVar = BG_Noise(S.nX*0.0005f, S.nY*0.0005f, 0.f)*0.3f;
    S.RandomRimHeight = BaseRimH*(1.f + RimVar*0.2f);
    return S;
}

static float ComputeCentralCraterHeight(const FCraterSetup& S, const FCraterBiomeConfig& CRC)
{
    const float BowlEnd = 0.82f;
    const float PeakDist = 0.96f;
    
    // Deeper base depth for the massive meteor feel
    const float FloorZ = S.LocalPlains + S.EffectiveDepth;

    float H = S.LocalPlains;

    if (S.NormDist < BowlEnd)
    {
        // Inside the bowl. Deep floor curving up very gradually to the bottom of the steep wall.
        const float t = S.NormDist / BowlEnd; // 0 to 1
        // x^6 gives a very flat, wide floor that suddenly kicks up at the walls
        const float BowlVal = FMath::Pow(t, 6.0f);
        H = FMath::Lerp(FloorZ, S.LocalPlains - FMath::Abs(S.EffectiveDepth)*0.2f, BowlVal);
        
        float FloorAmp = 150.f;
        if (CRC.CraterStyle == ECraterStyle::Meteor && CRC.bEnableImpactMelt)
        {
            const float MeltFade = FMath::SmoothStep(CRC.MeltSheetRadiusFraction*0.8f, CRC.MeltSheetRadiusFraction, S.NormDist/BowlEnd);
            FloorAmp = FMath::Lerp(CRC.MeltFloorNoiseAmplitude, 150.f, MeltFade);
        }
        H += BG_Noise(S.nX*0.003f, S.nY*0.003f, 0.f) * FloorAmp * (1.f - BowlVal*0.7f);
    }
    else if (S.NormDist < PeakDist)
    {
        // Inner rim wall. Rises steeply, curving "inward" (concave up).
        const float RimT = (S.NormDist - BowlEnd) / (PeakDist - BowlEnd); // 0 to 1
        
        // High, thin rim with x^2 or x^3 to make the slope continuously increase until vertical peak
        const float CurveUp = FMath::Pow(RimT, 2.8f); 
        
        // Make the Rim Peak much taller for the meteor style
        const float RimPeak = S.LocalPlains + S.RandomRimHeight * 2.8f; 
        const float BaseWallHeight = S.LocalPlains - FMath::Abs(S.EffectiveDepth)*0.2f;
        
        H = FMath::Lerp(BaseWallHeight, RimPeak, CurveUp);
        
        // Jagged inward slabs sticking out of the cliff
        const float SlabNoise = FMath::Abs(BG_Noise(S.nX*0.015f, S.nY*0.015f, 0.f));
        H += SlabNoise * 1800.f * CurveUp; // Big inward-pointing slabs at the rim peak
    }
    else
    {
        // Outer crater wall. Ejecta blanket slope. Drops back from the tall peak.
        const float OuterEnd = PeakDist + CRC.RimPeakLength * 1.5f;
        const float DropT = FMath::SmoothStep(PeakDist, OuterEnd, S.NormDist);
        
        const float RimPeak = S.LocalPlains + S.RandomRimHeight * 2.8f;
        const float RestH = S.LocalPlains + S.RandomRimHeight * 0.3f;
        
        H = FMath::Lerp(RimPeak, RestH, FMath::Pow(DropT, 0.7f)); // Convex outer slope (gentler drop-off)
    }
    
    return H;
}

// ── Meteor: central uplift ────────────────────────────────────────────────────
static void ApplyMeteorUplift(float& H, const FCraterSetup& S, const FCraterBiomeConfig& CRC)
{
    if (CRC.CraterStyle!=ECraterStyle::Meteor || !CRC.bEnableCentralUplift) return;
    if (S.NormDist >= CRC.UpliftRadiusFraction) return;
    const float tUp = 1.f - S.NormDist/CRC.UpliftRadiusFraction;
    H += FMath::Pow(tUp,CRC.UpliftShapeExponent)*FMath::Abs(S.EffectiveDepth)*CRC.UpliftHeightFraction
       + BG_Noise(S.nX*0.005f, S.nY*0.005f, 100.f)*CRC.UpliftNoiseAmplitude*tUp;
}

// ── Rim details ───────────────────────────────────────────────────────────────
static void ApplyRimDetails(float& H, const FCraterSetup& S, const FCraterBiomeConfig& CRC)
{
    const float RimStart = 0.82f, RimEnd = 0.96f;
    const float EdgeCurve = FMath::Sin(BG_Noise(S.nX*0.003f, S.nY*0.003f, 0.f)*3.14159f)*1000.f;
    float EdgeFade = 0.f;

    const float SlopeEnd = RimEnd + 0.10f;
    float DetailFade = 0.f;

    if (S.NormDist >= RimStart && S.NormDist <= RimEnd)
    {
        const float RimT = (S.NormDist - RimStart)/(RimEnd - RimStart);
        DetailFade = FMath::Pow(RimT, 2.f);
        EdgeFade = RimT;

        const float RibN = BG_Noise(S.nX*0.015f, S.nY*0.015f, 300.f);
        if (RibN > 0.3f) H -= 400.f * FMath::SmoothStep(0.3f, 0.8f, RibN) * FMath::Sin(RimT*3.14159f);
    }
    else if (S.NormDist > RimEnd && S.NormDist <= SlopeEnd)
    {
        DetailFade = 1.f - (S.NormDist - RimEnd)/0.10f;
        EdgeFade = DetailFade;
    }

    if (DetailFade > 0.f)
    {
        H += BG_Noise(S.nX*0.002f, S.nY*0.002f, 0.f) * CRC.RimNoiseAmplitude * 1.5f * DetailFade;
    }

    H += EdgeCurve * FMath::SmoothStep(0.f, 1.f, EdgeFade);

    const float RockMin = RimStart * 0.9f, RockMax = RimEnd * 1.1f;
    if (S.NormDist >= RockMin && S.NormDist <= RockMax)
    {
        const float t = (S.NormDist - RockMin) / (RockMax - RockMin);
        const float RF = FMath::SmoothStep(0.f, 0.5f, t) * FMath::SmoothStep(1.f, 0.5f, t);
        const float RN = BG_Noise(S.nX*0.01f, S.nY*0.01f, 0.f);
        if (RN > 0.15f) H += (RN - 0.15f) * 1500.f * RF; // Rocky bulges
    }
    if (S.NormDist >= RimEnd && S.NormDist <= RimEnd + CRC.RimPeakLength)
    {
        const float t = (S.NormDist - RimEnd) / CRC.RimPeakLength;
        const float SF = FMath::SmoothStep(0.f, 0.1f, t) * FMath::SmoothStep(1.f, 0.9f, t);
        const float TN = BG_Noise(S.nX*0.012f, S.nY*0.012f, 0.f);
        float Dir = 0.f;
        if (TN > 0.0f) Dir = FMath::SmoothStep(0.20f, 0.40f, TN);
        else           Dir = -FMath::SmoothStep(0.20f, 0.40f, -TN);

        if (Dir != 0.f) H += Dir * FMath::Abs(FMath::Sin(t*3.14159f*4.f)) * 4000.f * SF; // Ejecta gouges
    }
}

// ── Ejecta blanket ────────────────────────────────────────────────────────────
static void ApplyEjectaBlanket(float& H, const FCraterSetup& S, const FCraterBiomeConfig& CRC)
{
    const float RimEnd = 0.96f;
    if (S.NormDist > RimEnd && S.NormDist <= RimEnd + CRC.EjectaBlanketWidth)
    {
        const float Dist = S.NormDist - RimEnd;
        const float Fade = FMath::Pow(1.f - Dist/CRC.EjectaBlanketWidth, CRC.EjectaFadeExponent);
        H += CRC.EjectaThickness * FMath::Abs(S.EffectiveDepth) * Fade * 0.5f * FMath::SmoothStep(0.f, 0.05f, Dist);
        const float BN = BG_Noise(S.nX*CRC.EjectaBlockFrequency, S.nY*CRC.EjectaBlockFrequency, 0.f);
        if (BN > 0.8f) H += (BN - 0.8f) * CRC.EjectaBlockAmplitude * FMath::SmoothStep(CRC.EjectaBlanketWidth*0.8f, CRC.EjectaBlanketWidth*0.72f, Dist);
        const float SN = BG_Noise(S.nX*CRC.OverturnedStrataFrequency, S.nY*CRC.OverturnedStrataFrequency, 0.f);
        if (SN > 0.7f) H += (SN - 0.7f) * CRC.OverturnedStrataAmplitude * FMath::Sin(Dist*10.f) * 0.5f * FMath::SmoothStep(CRC.EjectaBlanketWidth*0.6f, CRC.EjectaBlanketWidth*0.55f, Dist);
    }
    if (S.NormDist > RimEnd && S.NormDist < RimEnd + 0.15f)
    {
        const float t = (S.NormDist - RimEnd) / 0.15f;
        H += BG_Noise(S.nX*0.0015f, S.nY*0.0015f, 0.f) * CRC.RimNoiseAmplitude * 0.6f
          * FMath::SmoothStep(0.f, 0.2f, t) * FMath::SmoothStep(1.f, 0.8f, t) * FMath::Exp(-(S.NormDist - RimEnd)*8.f);
    }
}

// ── Meteor: ejecta rays ───────────────────────────────────────────────────────
static void ApplyEjectaRays(float& Total, const FCraterSetup& S, const FCraterBiomeConfig& CRC)
{
    if (CRC.CraterStyle!=ECraterStyle::Meteor || !CRC.bEnableEjectaRays) return;
    if (CRC.EjectaRayCount <= 0) return; // FIX Infinite Loop guard
    const float RayStart=0.96f, RayEnd=CRC.EjectaRayExtent;
    if (S.NormDist<=RayStart || S.NormDist>=RayEnd) return;
    const float RadialFade=1.f-(S.NormDist-RayStart)/(RayEnd-RayStart);
    const float RayRot=BG_Noise(S.nX*0.00002f,S.nY*0.00002f,777.f)*3.14159f;
    const float AngleStep=2.f*3.14159f/(float)CRC.EjectaRayCount;
    float MaxRW=0.f;
    for (int32 r=0;r<CRC.EjectaRayCount;++r)
    {
        float dA=S.Ang-(r*AngleStep+RayRot);
        while (dA>3.14159f) dA-=2.f*3.14159f;
        while (dA<-3.14159f) dA+=2.f*3.14159f;
        if (FMath::Abs(dA)>=CRC.EjectaRayAngularWidth) continue;
        MaxRW=FMath::Max(MaxRW,FMath::SmoothStep(1.f,0.f,FMath::Abs(dA)/CRC.EjectaRayAngularWidth));
    }
    if (MaxRW>0.001f)
        Total += CRC.EjectaRayHeight*MaxRW*RadialFade*(BG_Noise(S.nX*0.003f,S.nY*0.003f,888.f)*0.3f+1.f);
}

// ── Secondary craters ─────────────────────────────────────────────────────────
static bool TryApplySecondaryCrater(float nX, float nY, float DistFromCenter,
                                     float BasePlains, const FCraterBiomeConfig& CRC,
                                     float& OutH, float& OutW)
{
    if (DistFromCenter<=CRC.CentralCraterRadius*0.82f) return false;
    const float CellSz=25000.f;
    const int32 CX=FMath::FloorToInt(nX/CellSz), CY=FMath::FloorToInt(nY/CellSz);
    const float LX=(CX+0.5f)*CellSz+BG_Noise(CX*13.f,CY*9.f,0.f)*0.38f*CellSz;
    const float LY=(CY+0.5f)*CellSz+BG_Noise(CX*13.f,CY*9.f,50.f)*0.38f*CellSz;
    const float Dist=FMath::Sqrt(FMath::Square(nX-LX)+FMath::Square(nY-LY));
    const float Roll=BG_Noise(CX*7.f,CY*11.f,100.f);
    if (Roll<=0.1f) return false;
    const float NI=(Roll-0.1f)/0.9f;
    const float SecSize=FMath::Lerp(2000.f,CRC.SecondaryCraterMaxRadius,NI);
    if (Dist>=SecSize) return false;
    const float SND=Dist/SecSize;
    const float SD=FMath::Lerp(-800.f,-2200.f,NI), SRH=FMath::Lerp(800.f,2000.f,NI);
    float H=BasePlains;
    if      (SND<0.65f) H=BasePlains+SD;
    else if (SND<0.85f) H=FMath::Lerp(BasePlains+SD,BasePlains+SRH,FMath::SmoothStep(0.65f,0.85f,SND));
    else                H=FMath::Lerp(BasePlains+SRH,BasePlains,FMath::SmoothStep(0.85f,1.f,SND));
    if (SND>0.65f&&SND<1.f) H+=BG_Noise(nX*0.005f,nY*0.005f,0.f)*300.f*FMath::Sin(SND*3.14159f);
    OutH=H; OutW=(1.f-FMath::Pow(SND,4.f))*CRC.SecondaryCraterDensity;
    return true;
}

// ── Tertiary craters ──────────────────────────────────────────────────────────
static void ApplyTertiaryCraters(float& Total, float nX, float nY,
                                  float DistFromCenter, float BasePlains,
                                  const FCraterBiomeConfig& CRC)
{
    if (DistFromCenter<=CRC.CentralCraterRadius*0.3f) return;
    const float CellSz=8000.f;
    const int32 CX=FMath::FloorToInt(nX/CellSz), CY=FMath::FloorToInt(nY/CellSz);
    const float LX=(CX+0.5f)*CellSz+BG_Noise(CX*17.f,CY*13.f,200.f)*0.4f*CellSz;
    const float LY=(CY+0.5f)*CellSz+BG_Noise(CX*17.f,CY*13.f,300.f)*0.4f*CellSz;
    const float DtoC=FMath::Sqrt(FMath::Square(nX-LX)+FMath::Square(nY-LY));
    const float Roll=BG_Noise(CX*5.f,CY*7.f,500.f);
    const float RadialBias=FMath::Exp(-DistFromCenter/(CRC.CentralCraterRadius*0.8f));
    if (Roll+RadialBias*0.3f<=0.6f) return;
    const float Intensity=FMath::Clamp((Roll+RadialBias*0.3f-0.6f)/0.4f,0.f,1.f);
    const float TSize=FMath::Lerp(CRC.TertiaryCraterMinRadius,CRC.TertiaryCraterMaxRadius,Intensity);
    if (DtoC>=TSize) return;
    const float TNorm=DtoC/TSize;
    const float Bowl=FMath::Pow(1.f-TNorm,1.3f);
    float TH=BasePlains+(-200.f-Intensity*150.f)*Bowl;
    if (TNorm>0.1f&&TNorm<0.25f) TH+=200.f*FMath::SmoothStep(0.1f,0.25f,TNorm);
    Total=FMath::Lerp(Total,TH,0.20f);
}

} // anonymous namespace

// =============================================================================
//  GetCraterHeight — public entry point
//  BaseHeight = the blended non-crater surface height, used as the initial
//  TotalHeight value so terrain transitions smoothly into crater geometry.
// =============================================================================
float FVoxelBiomeGenerators::GetCraterHeight(float X, float Y,
                                              const FVoxelGenerationConfig& Config,
                                              float BaseHeight)
{
    const FCraterBiomeConfig& CRC = Config.Craters;
    const FCraterSetup S = ComputeCraterSetup(X, Y, Config);

    float TotalHeight = BaseHeight; // smooth boundary transition

    // 1. Central crater
    if (S.Dist < S.CraterRadius * 1.5f)
    {
        const float FadeStart = 0.92f + 0.05f, FadeEnd = 0.92f + 0.38f;
        const float Dominance = FMath::SmoothStep(FadeEnd, FadeStart, S.NormDist);
        float CentralH = ComputeCentralCraterHeight(S, CRC);
        ApplyMeteorUplift(CentralH, S, CRC);
        ApplyRimDetails(CentralH, S, CRC);
        ApplyEjectaBlanket(CentralH, S, CRC);
        TotalHeight = FMath::Lerp(TotalHeight, CentralH, Dominance);
    }

    // 2. Ejecta rays (Meteor only)
    ApplyEjectaRays(TotalHeight, S, CRC);

    // 3. Secondary craters
    float SecH=0.f, SecW=0.f;
    if (TryApplySecondaryCrater(S.nX,S.nY,S.Dist,S.BasePlains,CRC,SecH,SecW))
        TotalHeight = FMath::Lerp(TotalHeight, SecH, SecW);

    // 4. Tertiary craters
    ApplyTertiaryCraters(TotalHeight, S.nX, S.nY, S.Dist, S.BasePlains, CRC);

    // 5. Floor texture (suppressed in melt zone)
    if (TotalHeight < S.BasePlains)
    {
        float NoiseAmp = CRC.BuildingNoiseAmplitude;
        if (CRC.CraterStyle==ECraterStyle::Meteor && CRC.bEnableImpactMelt)
        {
            const float MeltR = CRC.MeltSheetRadiusFraction;
            const float InMelt = FMath::SmoothStep(MeltR, MeltR*0.8f, S.NormDist/0.65f);
            NoiseAmp = FMath::Lerp(NoiseAmp, CRC.MeltFloorNoiseAmplitude, InMelt);
        }
        TotalHeight += BG_FBM(S.nX*CRC.BuildingNoiseFrequency, S.nY*CRC.BuildingNoiseFrequency, 0.f,
                               2, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves) * NoiseAmp * 0.2f;
    }

    return TotalHeight;
}
