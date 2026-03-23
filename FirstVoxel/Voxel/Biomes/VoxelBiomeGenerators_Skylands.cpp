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
    float HeightNorm, float RoughnessNorm, float AbsoluteHeight, const FSkylandsLayerConfig& SC)
{
    // Skyland probability grows with terrain height
    // Low terrain (crater floor) = almost no skylands
    // High terrain (above mountain peaks) = many skylands
    
    // Base probability is extremely low - skylands almost never spawn at low heights
    float P = 0.001f;
    
    // Height bonus: probability increases dramatically with absolute terrain height
    // Only terrain above 10000cm (100m - mountain peaks) gets significant skylands
    // At 0cm: bonus = 0
    // At 10000cm (100m): bonus = 0.16
    // At 20000cm (200m - high peaks): bonus = 0.65
    const float HeightBonus = SC.HeightProbabilityBonus * FMath::SmoothStep(0.f, 20000.f, AbsoluteHeight);
    P += HeightBonus;
    
    // Roughness bonus: adds variation (reduced)
    P += SC.RoughnessProbabilityBonus * RoughnessNorm * 0.2f;
    
    // Altitude scale: ensures skylands only spawn above mountain peaks (10000cm = 100m)
    const float AltitudeScale = FMath::SmoothStep(10000.f, 25000.f, AbsoluteHeight);
    P *= AltitudeScale;
    
    // Height fade: extremely steep falloff - only terrain above 100m gets skylands
    float HeightFade = FMath::SmoothStep(10000.f, 25000.f, AbsoluteHeight);
    HeightFade = FMath::Pow(HeightFade, 2.0f);
    P *= HeightFade;

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
    const FVoxelGenerationConfig& Config,
    TMap<FIntPoint, TArray<FSkylandIslandData>>* CacheMap)
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

    // crater probability boost weighting
    const float CraterW = Weights.GetWeight(EVoxelBiome::Craters);

    for (int32 dx=-1; dx<=1; ++dx)
    for (int32 dy=-1; dy<=1; ++dy)
    {
        const int32 cX = CellX+dx, cY = CellY+dy;
        const FIntPoint Key(cX, cY);

        const float nX2 = (float)cX*GridSize+Off.X;
        const float nY2 = (float)cY*GridSize+Off.Y;

        const float HX  = (BG_Noise(nX2*0.001f, nY2*0.001f, 0.f)+1.f)*0.5f;
        const float HY  = (BG_Noise(nX2*0.001f, nY2*0.001f, 100.f)+1.f)*0.5f;
        const float CX2 = (cX+0.12f+HX*0.76f)*GridSize;
        const float CY2 = (cY+0.12f+HY*0.76f)*GridSize;

        const float WarpAmt = SC.BaseIslandSize * 0.70f;
        
        // --- NEW: FBM Shoreline fractal warping for irregular jagged edges ---
        const float WarpX = BG_FBM(X * 0.003f, Y * 0.003f, 700.f, 2, 2.f, 0.5f, 4) * WarpAmt;
        const float WarpY = BG_FBM(X * 0.003f, Y * 0.003f, 800.f, 2, 2.f, 0.5f, 4) * WarpAmt;
        
        const float Dist = FMath::Sqrt(FMath::Square(X + WarpX - CX2) + FMath::Square(Y + WarpY - CY2));

        // --- SPEEDUP CACHE LOOKUP ---
        if (CacheMap)
        {
            if (TArray<FSkylandIslandData>* Precalc = CacheMap->Find(Key))
            {
                for (const auto& Isl : *Precalc)
                {
                    // Recalculate distance using column X/Y to evaluate hull boundary
                    const float dX = X + WarpX - Isl.CX2;
                    const float dY = Y + WarpY - Isl.CY2;
                    const float d2 = FMath::Sqrt(dX*dX + dY*dY);
                    if (d2 <= Isl.IslandSize)
                    {
                        Cache.Islands.Add(Isl);
                        Cache.bHasSkyland = true;
                    }
                }
                continue;
            }
        }

        const FVoxelBiomeWeightMap CW = FVoxelBiomeManager::GetBiomeWeightsStatic(CX2, CY2, Config);
        const float CenterNeutralH = FVoxelBiomeManager::GetNeutralSurfaceHeightStatic(CX2, CY2, Config);
        const float CenterFullH    = FVoxelBiomeManager::GetSurfaceHeightStatic(CX2, CY2, CW, Config);
        
        auto GetMaxH = [&](float sX, float sY)
        {
            const float NeutralH = FVoxelBiomeManager::GetNeutralSurfaceHeightStatic(sX, sY, Config);
            const FVoxelBiomeWeightMap mCW = FVoxelBiomeManager::GetBiomeWeightsStatic(sX, sY, Config);
            const float FullH = FVoxelBiomeManager::GetSurfaceHeightStatic(sX, sY, mCW, Config);
            return FMath::Max(NeutralH, FullH);
        };

        const float MaxIS = SC.BaseIslandSize + SC.HeightSizeBonus;
        const float SampleR = FMath::Max(1500.f, MaxIS);

        // OPT-2: 4 axis-aligned samples instead of 8 (N/S/E/W only).
        // Diagonal samples offered no meaningful improvement over axis at these radii.
        float CH = FMath::Max(CenterNeutralH, CenterFullH);
        CH = FMath::Max(CH, GetMaxH(CX2 + SampleR, CY2));
        CH = FMath::Max(CH, GetMaxH(CX2 - SampleR, CY2));
        CH = FMath::Max(CH, GetMaxH(CX2, CY2 + SampleR));
        CH = FMath::Max(CH, GetMaxH(CX2, CY2 - SampleR));

        const float HN = FMath::Clamp(CH/SC.MaxTerrainReference, 0.f, 1.f);
        const float RN = FMath::Clamp(CW.GetRoughness()/SC.RoughnessReference, 0.f, 1.f);
        const float TS = FMath::Clamp(HN*1.5f+RN*0.8f, 0.f, 1.f);
        const float CST = FMath::SmoothStep(0.f, SC.ShardTransitionStrength, TS);

        const float cnX2 = CX2+Off.X, cnY2 = CY2+Off.Y;
        const float HP   = (BG_Noise(cnX2*0.002f, cnY2*0.002f, 200.f)+1.f)*0.5f;
        float SpawnProb = ComputeIslandSpawnProbability(HN, RN, CH, SC);

        // Altitude System fully empowers probability continuously starting from Crater Floor up up aloft!

        // Cleaned up redundant clamp
        if (HP > SpawnProb) continue;

        // --- Size -----------------------------------------------------------
        // Lower terrain (CST ≈ 0) = tiny shapes; Higher terrain (CST ≈ 1) = massive islands.
        const float SF  = (BG_FBM(cnX2*0.00008f, cnY2*0.00008f, 50.f, 2, 2.f, 0.5f, 2)+1.f)*0.5f;
        const float NR  = FMath::Lerp(0.5f, 0.25f, CST);
        
        float IS = FMath::Lerp(SC.BaseIslandSize * SC.ShardMinScale, SC.BaseIslandSize + SC.HeightSizeBonus, CST);
        IS = FMath::Clamp(IS*((1.f-NR)+NR*SF*2.f), 150.f, GridSize*0.48f);

        if (CH < 0.f && CraterW < 0.4f) IS *= FMath::Clamp(1.f + CH / 15000.f, 0.30f, 1.f);

        // --- Altitude --------------------------------------------------------
        // SKYL_BEHAVIOR:
        //   Shards over flat/low terrain (CST≈0) hover very low (ShardAltitudeAboveTerrain).
        //   Islands over peaks (CST≈1) scale up using altitude bonuses.
        const float AltBase = FMath::Lerp(SC.ShardAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, CST);
        const float CuH = FMath::Pow(FMath::Max(0.f,HN), 2.5f);
        const float CuR = FMath::Pow(FMath::Max(0.f,RN), 2.f);

        // Absolute height anchor coordinates
        const float AnchorH = FMath::Max(CH, Config.SeaLevel);
        float SkyAlt = AnchorH + AltBase + CST*(CuH*SC.HeightAltitudeBonus + CuR*SC.RoughnessAltitudeBonus);

        // Size/altitude coupling
        {
            const float AG = FMath::Max(0.f, SkyAlt-CH);
            IS = FMath::Clamp(IS*FMath::Lerp(FMath::Clamp(AG/FMath::Max(1.f,SC.MinAltitudeAboveTerrain),0.4f,3.f),1.f,CST),150.f,GridSize*0.48f);
        }

        // --- Jitter ----------------------------------------------------------
        // Low shards stay in a tight cluster band; High islands spread up/down.
        const float NoiseRange = FMath::Lerp(SC.ShardAltitudeJitter, SC.IslandAltitudeJitter, CST);
        SkyAlt += BG_Noise(cnX2*0.006f, cnY2*0.006f, 500.f) * NoiseRange;

        // --- Shape & Roundness -----------------------------------------------
        // Low shards (CST≈0) use ShardThicknessRatio (near-sphere: ~0.80) to look like round boulders.
        // High islands (CST≈1) use ThicknessRatio (pancake: ~0.15) for plateau profiles.
        const float ET = FMath::Lerp(SC.ShardThicknessRatio, SC.ThicknessRatio, CST);
        float HT = IS * ET;

        // RULE: Lower terrain (CH < 0) = Lower altitude
        float LocalMinAlt = SC.MinAltitudeAboveTerrain;
        if (CH < 0.f) LocalMinAlt = FMath::Lerp(2500.f, LocalMinAlt, FMath::Clamp(1.f + CH / 15000.f, 0.f, 1.f));
        SkyAlt = FMath::Max(SkyAlt, AnchorH + LocalMinAlt + HT);

        float Thr = FMath::Lerp(SC.ThresholdAtMinProbability, SC.ThresholdAtMaxProbability, CST) + FMath::Lerp(0.20f, 0.f, CST);
        if (CST > 0.5f) Thr -= FMath::Log2(FMath::Max(1.f, IS/SC.BaseIslandSize))*0.05f;

        // Populate constant island data for memoization
        FSkylandIslandData PrecalcIsl;
        PrecalcIsl.SkyAlt       = SkyAlt;
        PrecalcIsl.HalfThick    = HT;
        PrecalcIsl.Threshold    = Thr;
        PrecalcIsl.ShardT       = CST;
        PrecalcIsl.HeightNorm   = HN;
        PrecalcIsl.ShardFalloff = FMath::Pow(FMath::Max(0.f,TS), 2.2f);
        PrecalcIsl.IslandSize   = IS;
        const float SR   = FMath::Max(1.f, IS/SC.BaseIslandSize);
        PrecalcIsl.Freq         = FMath::Max(FMath::Lerp(SC.ShapeFrequency*6.f, SC.ShapeFrequency/SR, CST), 0.00025f);
        PrecalcIsl.CX2          = CX2;
        PrecalcIsl.CY2          = CY2;

        TArray<FSkylandIslandData> LocalList;
        TArray<FSkylandIslandData>& PrecalcList = CacheMap ? CacheMap->FindOrAdd(Key) : LocalList;
        PrecalcList.Add(PrecalcIsl);

        if (Dist <= IS)
        {
            Cache.Islands.Add(PrecalcIsl);
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
        else 
        { 
            const float BottomWarp = BG_Noise(WX * 0.003f, WY * 0.003f, (WZ + 500.f) * 0.006f) * 0.20f;
            const float AdjustedTC = FMath::Max(0.f, -tC + BottomWarp);
            Falloff = FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(AdjustedTC, 0.85f)); 
        }
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
            QX += BG_Noise(QX*WF+10.f, QY*WF+20.f, WZ*WF)*SC.DomainWarpStrength;
            QY += BG_Noise(QX*WF+50.f, QY*WF+10.f, WZ*WF+100.f)*SC.DomainWarpStrength;
        }

        float SD = 0.f;
        const float ZFS = FMath::Lerp(0.50f, 0.05f, Isl.ShardT);
        if (Config.Performance.bEnable3DSkylandNoise || Isl.ShardT < 0.5f)
            SD = BG_Noise(QX*Isl.Freq*0.6f, QY*Isl.Freq*0.6f, WZ*Isl.Freq*ZFS)
               * FMath::Lerp(0.55f, 0.25f, Isl.ShardT);

        const int32 Oct2D = FMath::Clamp(FMath::Min((int32)SC.ShapeOctaves,4), 1, Config.Performance.MaxNoiseOctaves);
        const float SZ = WZ*Isl.Freq; // FIX: Keep 3D noise enabled always so walls are not extruded cylinders (no continuous slabs)
        const float Shape = BG_FBM(QX*Isl.Freq, QY*Isl.Freq, SZ, Oct2D, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves) + SD;

        float RD = 0.f;
        if (SC.bEnableHangingRoots && tC < -0.25f)
        {
            const float RZN = FMath::Clamp((-tC-0.25f)/0.75f, 0.f, 1.f);
            RD = FMath::Max(0.f, BG_FBM(QX*SC.RootFrequency, QY*SC.RootFrequency, WZ*SC.RootFrequency,
                           2, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves)) * (1.f-RZN) * 0.4f * Falloff;
        }

        // Taper bottom radius inwards to create a bulbous cone/keel shape (from sketch)
        float Taper = 0.f;
        if (tC < 0.0f) 
        {
            const float TaperAmt = 0.50f; // Increase max taper offset at the bottom tip
            const float N = -tC; // goes from 0.0 at center to 1.0 at absolute bottom
            Taper = FMath::Pow(N, 2.0f) * TaperAmt; // Concave/bulbous bow out curves inwards beautifully
        }

        float D = FMath::SmoothStep(Isl.Threshold + Taper, Isl.Threshold+0.4f + Taper, Shape)*Falloff*2.5f - (1.f-Falloff)*1.8f + RD;
        const float BU = FMath::Max(0.f, BG_Noise(WX*0.002f,WY*0.002f,WZ*0.001f))
                        * FMath::Lerp(0.10f, FMath::Lerp(0.50f,2.80f,Isl.HeightNorm), Isl.ShardT);
        float PM = 1.f;
        if (Isl.ShardT > 0.5f && tC > 0.f) PM = FMath::SmoothStep(0.15f,0.45f,1.f-tC);
        D -= BU*PM;

        MaxD = FMath::Max(MaxD, D);
    }

    return FMath::Clamp(MaxD, -2.f, 2.f);
}

void FVoxelBiomeGenerators::GetSkylandAltitudeBounds(
    float SurfaceHeight, 
    float Roughness, 
    const FVoxelGenerationConfig& Config, 
    float& OutMinAlt, 
    float& OutMaxAlt)
{
    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;

    // 1. Calculate continuous weights/norms (Mirrors GetSkylandColumnCache layout)
    const float HN = FMath::Clamp(SurfaceHeight / SC.MaxTerrainReference, 0.f, 1.f);
    const float RN = FMath::Clamp(Roughness / SC.RoughnessReference, 0.f, 1.f);
    const float TS = FMath::Clamp(HN * 1.5f + RN * 0.8f, 0.f, 1.f);
    const float CST = FMath::SmoothStep(0.f, SC.ShardTransitionStrength, TS);

    // 2. Altitude base & stretch
    const float AltBase = FMath::Lerp(SC.ShardAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, CST);
    const float Stretched = FMath::Pow(FMath::Max(0.f, HN), SC.StretchedPowerExponent) * SC.MaxTerrainReference * SC.StretchedMultiplier;
    
    // 3. Anchor Height
    const float AnchorH = FMath::Max(SurfaceHeight, Config.SeaLevel);
    const float CuH = FMath::Pow(FMath::Max(0.f, HN), 2.5f);
    const float CuR = FMath::Pow(FMath::Max(0.f, RN), 2.f);

    float SkyAlt = AnchorH + AltBase + CST * (CuH * SC.HeightAltitudeBonus + CuR * SC.RoughnessAltitudeBonus);
    SkyAlt = FMath::Max(SkyAlt, SC.AbsoluteMinAltitude);

    // 4. Maximum Jitter/Noise additions for safety bounds
    const float NoiseRange = FMath::Lerp(SC.ShardAltitudeJitter, SC.IslandAltitudeJitter, CST);
    float MaxSkyAlt = SkyAlt + NoiseRange;
    float MinSkyAlt = SkyAlt - NoiseRange;

    // 5. Thickness
    float IS = FMath::Lerp(SC.BaseIslandSize * SC.ShardMinScale, SC.BaseIslandSize + SC.HeightSizeBonus, CST);
    const float ET = FMath::Lerp(SC.ShardThicknessRatio, SC.ThicknessRatio, CST);
    const float HT = IS * ET;

    // 6. Terrain intersection clamps
    float LocalMinAlt = SC.MinAltitudeAboveTerrain;
    if (SurfaceHeight < 0.f) 
    {
        LocalMinAlt = FMath::Lerp(2500.f, LocalMinAlt, FMath::Clamp(1.f + SurfaceHeight / 15000.f, 0.f, 1.f));
    }
    const float FloorSkyAlt = AnchorH + LocalMinAlt + HT;

    MinSkyAlt = FMath::Max(MinSkyAlt, FloorSkyAlt);
    MaxSkyAlt = FMath::Max(MaxSkyAlt, FloorSkyAlt);

    OutMinAlt = MinSkyAlt - HT - 1000.f; // Conservative buffer
    OutMaxAlt = MaxSkyAlt + HT + 1000.f; // Conservative buffer
}
