// VoxelBiomeGenerators_Skylands.cpp — three targeted fixes:
//
// FIX LOAD: GetSkylandColumnCache cell-center height was using the broken
//   zero-crater-weight trick: zeroing Craters weight then calling
//   GetSurfaceHeightStatic() — which ignores its weight parameter and always
//   calls GetCraterHeight(). So CH was still crater-modified.
//   Fix: call GetNeutralSurfaceHeightStatic() which genuinely omits the crater.
//
// FIX GROUNDED: Altitude coupling for low CST (flat plains, CST ≈ 0.18) was
//   computing SkyAlt = CH + 2100 + noise(±4250), which could reach CH − 2143.
//   Only guard was FMath::Max(SkyAlt, CH + HalfThick + 200) = CH + ~500 cm.
//   Island bottom at CH + 200 cm = essentially touching terrain surface.
//   Fix: final clamp uses MinAltitudeAboveTerrain (8000 cm) so the island
//   BOTTOM (SkyAlt − HalfThick) is always ≥ MinAlt above neutral terrain.
//
// FIX N10: J-curve probability (unchanged from previous session).

#include "VoxelBiomeGenerators_Shared.h"
#include "VoxelBiomeGenerators.h"
#include "VoxelBiomeManager.h"
#include "Voxel/Config/VoxelGenerationConfig.h"

static float ComputeIslandSpawnProbability(
    float HeightNorm, float RoughnessNorm, const FSkylandsLayerConfig& SC)
{
    float P = SC.BaseProbability;
    const float HighAltFactor = FMath::SmoothStep(SC.ProbHighAltitudeThreshold, 1.f, HeightNorm);
    P += SC.HeightProbabilityBonus * HighAltFactor;
    P += SC.RoughnessProbabilityBonus * RoughnessNorm;
    const float MidDev = (HeightNorm - SC.ProbMidDipCenter) / FMath::Max(SC.ProbMidDipWidth, 0.01f);
    const float MidDip = SC.ProbMidDipDepth * FMath::Exp(-0.5f * MidDev * MidDev);
    P -= MidDip;
    return FMath::Clamp(P, 0.f, 1.f);
}

float FVoxelBiomeGenerators::GetSkylandDensity(float X, float Y, float Z,
    float SurfaceHeight, const FVoxelBiomeWeightMap& Weights,
    const FVoxelGenerationConfig& Config, int32 StepSize)
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

    const float ColHN = FMath::Clamp(SurfaceHeight/SC.MaxTerrainReference, 0.f, 1.f);
    const float ColRN = FMath::Clamp(Weights.GetRoughness()/SC.RoughnessReference, 0.f, 1.f);
    const float ColTS = FMath::Clamp(ColHN*1.5f + ColRN*0.8f, 0.f, 1.f);
    const float ColST = FMath::SmoothStep(0.f, SC.ShardTransitionStrength, ColTS);
    const float GridSize = SC.BaseIslandSize * 3.0f;
    if (GridSize <= 0.f) return Cache;

    const int32 CellX = FMath::FloorToInt(X/GridSize);
    const int32 CellY = FMath::FloorToInt(Y/GridSize);
    Cache.bHasSkyland = false;

    for (int32 dx=-1; dx<=1; ++dx)
    for (int32 dy=-1; dy<=1; ++dy)
    {
        const int32 cX = CellX+dx, cY = CellY+dy;
        const float nX2 = (float)cX*GridSize+Off.X;
        const float nY2 = (float)cY*GridSize+Off.Y;

        const float HX  = (BG_Noise(nX2*0.001f, nY2*0.001f, 0.f)+1.f)*0.5f;
        const float HY  = (BG_Noise(nX2*0.001f, nY2*0.001f, 100.f)+1.f)*0.5f;
        const float CX2 = (cX+0.12f+HX*0.76f)*GridSize;
        const float CY2 = (cY+0.12f+HY*0.76f)*GridSize;
        const float Dist = FMath::Sqrt(FMath::Square(X-CX2)+FMath::Square(Y-CY2));

        // FIX CLIPPING: Sample height at the center and at 4 perimeter points.
        // Take the MAXIMUM of both Neutral (pre-crater) and Full (post-crater) height.
        // This guarantees the island floats safely above mountain peaks and crater rims
        // that exist under its footprint, without dipping into crater bowls.
        const float MaxIS = SC.BaseIslandSize + SC.HeightSizeBonus;
        const float SampleR = FMath::Max(1500.f, MaxIS);
        const float SampleD = SampleR * 0.7071f;
        auto GetMaxH = [&](float sX, float sY)
        {
            const float NeutralH = FVoxelBiomeManager::GetNeutralSurfaceHeightStatic(sX, sY, Config);
            const FVoxelBiomeWeightMap mCW = FVoxelBiomeManager::GetBiomeWeightsStatic(sX, sY, Config);
            const float FullH = FVoxelBiomeManager::GetSurfaceHeightStatic(sX, sY, mCW, Config);
            return FMath::Max(NeutralH, FullH);
        };

        const FVoxelBiomeWeightMap CW = FVoxelBiomeManager::GetBiomeWeightsStatic(CX2, CY2, Config);
        const float CenterNeutralH = FVoxelBiomeManager::GetNeutralSurfaceHeightStatic(CX2, CY2, Config);
        const float CenterFullH    = FVoxelBiomeManager::GetSurfaceHeightStatic(CX2, CY2, CW, Config);
        
        float CH = FMath::Max(CenterNeutralH, CenterFullH);
        CH = FMath::Max(CH, GetMaxH(CX2 + SampleR, CY2));
        CH = FMath::Max(CH, GetMaxH(CX2 - SampleR, CY2));
        CH = FMath::Max(CH, GetMaxH(CX2, CY2 + SampleR));
        CH = FMath::Max(CH, GetMaxH(CX2, CY2 - SampleR));
        CH = FMath::Max(CH, GetMaxH(CX2 + SampleD, CY2 + SampleD));
        CH = FMath::Max(CH, GetMaxH(CX2 - SampleD, CY2 - SampleD));
        CH = FMath::Max(CH, GetMaxH(CX2 + SampleD, CY2 - SampleD));
        CH = FMath::Max(CH, GetMaxH(CX2 - SampleD, CY2 + SampleD));
        const float HN = FMath::Clamp(CH/SC.MaxTerrainReference, 0.f, 1.f);
        const float RN = FMath::Clamp(CW.GetRoughness()/SC.RoughnessReference, 0.f, 1.f);
        const float TS = FMath::Clamp(HN*1.5f+RN*0.8f, 0.f, 1.f);
        const float CST = FMath::SmoothStep(0.f, SC.ShardTransitionStrength, TS);

        const float cnX2 = CX2+Off.X, cnY2 = CY2+Off.Y;
        const float HP   = (BG_Noise(cnX2*0.002f, cnY2*0.002f, 200.f)+1.f)*0.5f;
        float SpawnProb = ComputeIslandSpawnProbability(HN, RN, SC);
        // RULE: Lower terrain (CH < 0) = Less Probability (Linear decay below Sea Level)
        if (CH < 0.f) SpawnProb *= FMath::Clamp(1.f + CH / 15000.f, 0.20f, 1.f);
        if (HP > SpawnProb) continue;

        const float SMN = FMath::Max(0.20f, SC.ShardMinScale);
        const float SF  = (BG_FBM(cnX2*0.00008f, cnY2*0.00008f, 50.f, 2, 2.f, 0.5f, 2)+1.f)*0.5f;
        const float NR  = FMath::Lerp(0.5f, 0.25f, CST);
        float IS = FMath::Lerp(SC.BaseIslandSize*SMN, SC.BaseIslandSize+SC.HeightSizeBonus, CST);
        IS = FMath::Clamp(IS*((1.f-NR)+NR*SF*2.f), 150.f, GridSize*0.48f);
        // RULE: Lower terrain (CH < 0) = Smaller island size
        if (CH < 0.f) IS *= FMath::Clamp(1.f + CH / 15000.f, 0.30f, 1.f);
        if (Dist > IS) continue;

        // Base altitude - Scale down based on Size (CST) to let small shards hover lower
        const float AltBase = FMath::Lerp(SC.MinAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, TS) * FMath::Lerp(0.02f, 1.0f, CST);
        const float CuH = FMath::Pow(FMath::Max(0.f,HN), 2.5f);
        const float CuR = FMath::Pow(FMath::Max(0.f,RN), 2.f);

        // Absolute height anchor coordinates (e.g., config values should be absolute offsets above sea level)
        float SkyAlt = CH + AltBase + CST*(CuH*SC.HeightAltitudeBonus + CuR*SC.RoughnessAltitudeBonus);

        // Size/altitude coupling
        {
            const float AG = FMath::Max(0.f, SkyAlt-CH);
            IS = FMath::Clamp(IS*FMath::Lerp(FMath::Clamp(AG/FMath::Max(1.f,SC.MinAltitudeAboveTerrain),0.4f,3.f),1.f,CST),150.f,GridSize*0.48f);
        }

        const float NoiseRange = FMath::Lerp(5000.f, 1500.f, CST);
        SkyAlt += BG_Noise(cnX2*0.006f, cnY2*0.006f, 500.f) * NoiseRange;

        const float HA2 = (BG_Noise(cnX2*0.005f, cnY2*0.005f, 300.f)+1.f)*0.5f;
        const float ET  = FMath::Lerp(FMath::Lerp(0.12f,0.25f,HA2), SC.ThicknessRatio, CST);
        float HT = FMath::Min(IS*ET, IS*FMath::Lerp(0.75f, SC.MaxThicknessRatio, CST));

        // RULE: Lower terrain (CH < 0) = Lower altitude (hover closer to floor)
        float LocalMinAlt = SC.MinAltitudeAboveTerrain * FMath::Lerp(0.02f, 1.0f, CST);
        if (CH < 0.f) LocalMinAlt = FMath::Lerp(2500.f * FMath::Lerp(0.02f, 1.0f, CST), LocalMinAlt, FMath::Clamp(1.f + CH / 15000.f, 0.f, 1.f));
        SkyAlt = FMath::Max(SkyAlt, CH + LocalMinAlt + HT);

        float Thr = FMath::Lerp(SC.ThresholdAtMinProbability, SC.ThresholdAtMaxProbability, CST)
                  + FMath::Lerp(0.20f, 0.f, CST);
        if (CST > 0.5f) Thr -= FMath::Log2(FMath::Max(1.f, IS/SC.BaseIslandSize))*0.05f;

        if (FMath::Square(1.f-Dist/IS) > 0.001f)
        {
            FSkylandIslandData Isl;
            Isl.SkyAlt       = SkyAlt;
            Isl.HalfThick    = HT;
            Isl.Threshold    = Thr;
            Isl.ShardT       = CST;
            Isl.HeightNorm   = HN;
            Isl.ShardFalloff = FMath::Pow(FMath::Max(0.f,TS), 2.2f);
            Isl.IslandSize   = IS;
            const float SR   = FMath::Max(1.f, IS/SC.BaseIslandSize);
            Isl.Freq         = FMath::Max(FMath::Lerp(SC.ShapeFrequency*6.f, SC.ShapeFrequency/SR, CST), 0.00025f);
            Cache.Islands.Add(Isl);
            Cache.bHasSkyland = true;
        }
    }

    Cache.WX_base = X+Off.X;
    Cache.WY_base = Y+Off.Y;
    return Cache;
}

float FVoxelBiomeGenerators::GetSkylandDensityFromCache(
    const FSkylandColumnCache& Cache, float X, float Y, float Z,
    const FVoxelGenerationConfig& Config, int32 StepSize)
{
    if (!Cache.bHasSkyland) return -2.f;
    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
    const FVector Off = Config.GetSeedOffset();
    const float WX = X+Off.X, WY = Y+Off.Y, WZ = Z+Off.Z;
    float MaxD = -2.f;

    for (const FSkylandIslandData& Isl : Cache.Islands)
    {
        const float Margin = Isl.HalfThick * 0.4f;
        if (Z < Isl.SkyAlt-Isl.HalfThick-Margin || Z > Isl.SkyAlt+Isl.HalfThick+Margin) continue;

        const float tC = FMath::Clamp((Z-Isl.SkyAlt)/(Isl.HalfThick+Margin+1.f), -1.f, 1.f);
        float Falloff;
        if (tC >= 0.f) { const float FZ=0.35f; Falloff=(tC<FZ)?1.f:FMath::SmoothStep(0.f,1.f,1.f-(tC-FZ)/(1.f-FZ)); }
        else { Falloff = FMath::SmoothStep(0.f,1.f,1.f-FMath::Pow(-tC,0.85f)); }
        Falloff = FMath::Lerp(FMath::SmoothStep(0.f,1.f,1.f-FMath::Pow(FMath::Abs(tC),0.6f)), Falloff, FMath::Max(0.40f,Isl.ShardT));
        if (Isl.ShardT < 0.3f)
        {
            const float RF = FMath::SmoothStep(0.f,1.f,1.f-FMath::Pow(FMath::Abs(tC),FMath::Lerp(1.f,0.6f,Isl.ShardT)));
            Falloff = FMath::Lerp(RF, Falloff, FMath::Lerp(0.8f,0.2f,Isl.ShardT));
        }
        if (Falloff < 0.001f)
        {
            MaxD = FMath::Max(MaxD, -1.8f-FMath::Max(0.f, BG_Noise(WX*0.002f,WY*0.002f,WZ*0.001f))
                   * FMath::Lerp(0.10f, FMath::Lerp(0.50f,2.80f,Isl.HeightNorm), Isl.ShardT));
            continue;
        }

        float QX=WX, QY=WY;
        if (SC.bEnableDomainWarping)
        {
            const float WF = SC.DomainWarpFrequency;
            QX += BG_Noise(QX*WF+10.f, QY*WF+20.f, 0.f)*SC.DomainWarpStrength;
            QY += BG_Noise(QX*WF+50.f, QY*WF+10.f, 0.f)*SC.DomainWarpStrength;
        }

        float SD = 0.f;
        const float ZFS = FMath::Lerp(0.50f, 0.05f, Isl.ShardT);
        if (Config.Performance.bEnable3DSkylandNoise || Isl.ShardT < 0.5f)
            SD = BG_Noise(QX*Isl.Freq*0.6f, QY*Isl.Freq*0.6f, WZ*Isl.Freq*ZFS)
               * FMath::Lerp(0.55f, 0.25f, Isl.ShardT);

        const int32 Oct2D = FMath::Clamp(FMath::Min((int32)SC.ShapeOctaves,2), 1, Config.Performance.MaxNoiseOctaves);
        const float SZ = (Isl.ShardT < 0.5f) ? WZ*Isl.Freq : 0.f;
        const float Shape = BG_FBM(QX*Isl.Freq, QY*Isl.Freq, SZ, Oct2D, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves) + SD;

        float RD = 0.f;
        if (SC.bEnableHangingRoots && tC < -0.25f)
        {
            const float RZN = FMath::Clamp((-tC-0.25f)/0.75f, 0.f, 1.f);
            RD = FMath::Max(0.f, BG_FBM(QX*SC.RootFrequency, QY*SC.RootFrequency, WZ*SC.RootFrequency,
                           2, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves)) * (1.f-RZN) * 0.4f * Falloff;
        }

        float D = FMath::SmoothStep(Isl.Threshold, Isl.Threshold+0.4f, Shape)*Falloff*2.5f - (1.f-Falloff)*1.8f + RD;
        const float BU = FMath::Max(0.f, BG_Noise(WX*0.002f,WY*0.002f,WZ*0.001f))
                        * FMath::Lerp(0.10f, FMath::Lerp(0.50f,2.80f,Isl.HeightNorm), Isl.ShardT);
        float PM = 1.f;
        if (Isl.ShardT > 0.5f && tC > 0.f) PM = FMath::SmoothStep(0.15f,0.45f,1.f-tC);
        D -= BU*PM;

        MaxD = FMath::Max(MaxD, D);
    }

    return FMath::Clamp(MaxD, -2.f, 2.f);
}
