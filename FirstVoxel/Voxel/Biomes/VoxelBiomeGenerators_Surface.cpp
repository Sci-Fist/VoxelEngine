// VoxelBiomeGenerators_Surface.cpp
// Surface height functions: Forest, Desert, Peaks, Cliffs, Mesa.
// Crystal cavern delta also lives here (purely sub-surface, no crater).
#include "VoxelBiomeGenerators_Shared.h"
#include "VoxelBiomeGenerators.h"
#include "Voxel/Config/VoxelGenerationConfig.h"

// =============================================================================
//  FOREST
// =============================================================================
float FVoxelBiomeGenerators::GetForestHeight(float X, float Y, const FVoxelGenerationConfig& C)
{
    const FForestBiomeConfig& FC = C.Forest;
    const FVector Off = C.GetSeedOffset();
    const float nX = X+Off.X, nY = Y+Off.Y;
    float Base = BG_FBM(nX*FC.NoiseFrequency, nY*FC.NoiseFrequency, 0.f,
                        FC.Octaves, 2.f, 0.5f, C.Performance.MaxNoiseOctaves);
    return C.SeaLevel + FMath::Lerp(FC.HeightMin, FC.HeightMax, (Base+1.f)*0.5f)
           + BG_Noise(nX*FC.DetailFrequency, nY*FC.DetailFrequency, 0.f)*FC.DetailAmplitude;
}

// =============================================================================
//  DESERT
// =============================================================================
float FVoxelBiomeGenerators::GetDesertHeight(float X, float Y, const FVoxelGenerationConfig& C)
{
    const FDesertBiomeConfig& DC = C.Desert;
    const FVector Off = C.GetSeedOffset();
    const float nX = X+Off.X, nY = Y+Off.Y;
    float Base = BG_FBM(nX*DC.NoiseFrequency, nY*DC.NoiseFrequency, 40.f,
                        DC.Octaves, 2.f, 0.5f, C.Performance.MaxNoiseOctaves);
    float Shaped = FMath::Pow(FMath::Max(0.f, (Base+1.f)*0.5f), DC.Sharpness);
    return C.SeaLevel + FMath::Lerp(DC.HeightMin, DC.HeightMax, Shaped)
           + BG_Noise(nX*DC.RippleFrequency, nY*DC.RippleFrequency, 0.f)*DC.RippleAmplitude;
}

// =============================================================================
//  PEAKS
// =============================================================================
float FVoxelBiomeGenerators::GetPeaksHeight(float X, float Y, const FVoxelGenerationConfig& C)
{
    const FPeaksBiomeConfig& PC = C.Peaks;
    const FVector Off = C.GetSeedOffset();
    const float nX = X+Off.X, nY = Y+Off.Y;
    float Base = BG_FBM(nX*PC.NoiseFrequency, nY*PC.NoiseFrequency, 10.f,
                        PC.Octaves, 2.f, 0.5f, C.Performance.MaxNoiseOctaves);
    float Shaped = FMath::Clamp(FMath::Pow(FMath::Clamp((Base+1.f)*0.5f,0.f,1.f), PC.Sharpness),0.f,1.f);
    const float MaxDetail = (PC.HeightMax-PC.HeightMin)*0.02f;
    return C.SeaLevel + FMath::Lerp(PC.HeightMin, PC.HeightMax, Shaped)
           + BG_Noise(nX*PC.NoiseFrequency*4.f, nY*PC.NoiseFrequency*4.f, 0.f)
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
    float Base = BG_FBM(nX*CC.NoiseFrequency, nY*CC.NoiseFrequency, 15.f,
                        CC.Octaves, 2.1f, 0.55f, C.Performance.MaxNoiseOctaves);
    float Shaped = FMath::Clamp(FMath::Pow(FMath::Abs(Base), CC.Sharpness), 0.f, 1.f);
    if (CC.TerraceSteps>0 && CC.TerraceFactor>0.f)
    {
        const float S = (float)CC.TerraceSteps;
        Shaped = FMath::Lerp(Shaped, FMath::Floor(Shaped*S)/S, CC.TerraceFactor);
    }
    return C.SeaLevel + FMath::Lerp(CC.HeightMin, CC.HeightMax, Shaped)
           + BG_Noise(nX*CC.NoiseFrequency*6.f, nY*CC.NoiseFrequency*6.f, 0.f)*CC.DetailAmplitude;
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
        (BG_FBM(nX*MC.MesaFrequency,  nY*MC.MesaFrequency,  20.f, 4, 2.f, 0.5f, C.Performance.MaxNoiseOctaves)+1.f)*0.5f,
        (BG_FBM(nX*MC.ButteFrequency, nY*MC.ButteFrequency, 40.f, 3, 2.f, 0.5f, C.Performance.MaxNoiseOctaves)+1.f)*0.5f);
    const float StepS = (float)MC.PlateauSteps;
    const float Plateau = FMath::Floor(CombinedProfile*StepS)/StepS;
    const float EdgeBlend = FMath::SmoothStep(0.f, 1.f, (CombinedProfile-Plateau)*(MC.EdgeSharpness+4.f));
    Height += (Plateau + FMath::Pow(EdgeBlend,2.f)/StepS) * (MC.HeightMax-MC.HeightBase);

    const float PillarN = BG_Noise(nX*MC.PillarFrequency, nY*MC.PillarFrequency, 60.f);
    if (PillarN > 0.65f)
    {
        const float I = (PillarN-0.65f)/0.35f;
        float PH = FMath::Lerp(MC.PillarHeightMin, MC.PillarHeightMax, I);
        PH *= FMath::Pow(1.f-FMath::Clamp(1.f-I,0.f,1.f)*MC.PillarConicalFactor, 1.5f);
        if (I > 0.85f) PH += 500.f;
        Height = FMath::Max(Height, BasePlains+PH);
    }

    const float RidgedCh = 1.f - FMath::Abs(BG_Noise(nX*MC.ChannelFrequency, nY*MC.ChannelFrequency, 80.f));
    if (RidgedCh > 0.75f)
        Height -= FMath::SmoothStep(0.f,1.f,(RidgedCh-0.75f)/0.25f)*MC.ChannelDepth;

    const float Frac = (Height/MC.LayerThickness) - FMath::Floor(Height/MC.LayerThickness);
    if (Frac > MC.LayerHardness)
        Height += FMath::Sin((Frac-MC.LayerHardness)/(1.f-MC.LayerHardness)*3.14159f)*200.f*MC.LayerVariation;
    else Height += 100.f*MC.LayerVariation;

    if (Height > BasePlains+2000.f && CombinedProfile < 0.3f)
        Height += (BG_Noise(nX*MC.TalusFrequency, nY*MC.TalusFrequency, 100.f)+1.f)*400.f*MC.TalusSpread;

    return Height + BG_Noise(nX*0.008f, nY*0.008f, 0.f)*160.f;
}

// =============================================================================
//  CRYSTAL CAVERN DELTA (sub-surface modifier, no crater dependency)
// =============================================================================
float FVoxelBiomeGenerators::GetCrystalCavernDelta(float X, float Y, float Z,
    float SurfaceHeight, const FVoxelGenerationConfig& C)
{
    const FCrystalCavernsConfig& CVC = C.CaveCrystals;
    const FVector Off = C.GetSeedOffset();
    const float nX=X+Off.X, nY=Y+Off.Y, nZ=Z+Off.Z;
    const float CC = SurfaceHeight - CVC.DepthStart;
    if (Z > CC) return 0.f;
    if (Z < CC-(CVC.FadeDepth+8000.f)) return 0.f;
    const float Fade = FMath::Clamp((CC-Z)/CVC.FadeDepth, 0.f, 1.f);
    const float CF   = FMath::Max(CVC.ChamberFrequency, 0.00005f);
    const float Ch1  = FMath::Abs(BG_FBM(nX*CF, nY*CF, nZ*CF, 4, 2.f, 0.5f, C.Performance.MaxNoiseOctaves));
    const float Ch2  = FMath::Abs(BG_FBM(nX*CF*0.7f, nY*CF*0.7f, nZ*CF+5678.f, 3, 2.1f, 0.5f, C.Performance.MaxNoiseOctaves));
    float Carve = FMath::Max(0.f, CVC.ChamberThreshold-FMath::Min(Ch1,Ch2))*CVC.ChamberStrength;
    if (CVC.bEnableConnectingVeins)
    {
        const float VN = BG_FBM(nX*CF*2.5f, nY*CF*2.5f, nZ*CF*2.5f, 2, 2.f, 0.5f, C.Performance.MaxNoiseOctaves);
        Carve += FMath::Pow(FMath::Max(0.f, 1.f-FMath::Abs(VN)), CVC.VeinPower)*CVC.VeinStrength;
    }
    Carve = FMath::Clamp(Carve, 0.f, 1.5f);
    const float CF2 = FMath::Max(0.f,
        BG_Noise(nX*CVC.CrystalDetailFrequency, nY*CVC.CrystalDetailFrequency, nZ*CVC.CrystalDetailFrequency)
        - CVC.CrystalThreshold) * CVC.CrystalAmplitude;
    return (-(Carve*1.3f) + CF2*FMath::Clamp(Carve,0.f,1.f)) * Fade;
}
