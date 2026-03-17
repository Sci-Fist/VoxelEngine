// SurfaceBiomesConfig.h
// Configuration for 2D height-field blended surface biomes.

#pragma once

#include "CoreMinimal.h"
#include "SurfaceBiomesConfig.generated.h"

// ============================================================
//  BIOME BLEND CONTROL
// ============================================================
USTRUCT(BlueprintType)
struct FBiomeBlendConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend")
    float TemperatureFrequency = 0.00015f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend")
    float ErosionFrequency = 0.00010f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend")
    float PeaksStrength = 1.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend")
    float CliffsStrength = 1.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Blend")
    float MesaStrength = 1.0f;
};

// ============================================================
//  SURFACE — FOREST BIOME
// ============================================================
USTRUCT(BlueprintType)
struct FForestBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest")
    float NoiseFrequency = 0.00007f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest")
    float HeightMin = 500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest")
    float HeightMax = 6000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest")
    int32 Octaves = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest")
    float DetailFrequency = 0.0003f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Forest")
    float DetailAmplitude = 200.f;
};

// ============================================================
//  SURFACE — DESERT BIOME
// ============================================================
USTRUCT(BlueprintType)
struct FDesertBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert")
    float NoiseFrequency = 0.00012f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert")
    float HeightMin = 800.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert")
    float HeightMax = 8000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert")
    int32 Octaves = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert")
    float Sharpness = 1.6f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert")
    float RippleFrequency = 0.002f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Desert")
    float RippleAmplitude = 40.f;
};

// ============================================================
//  SURFACE — PEAKS BIOME
// ============================================================
USTRUCT(BlueprintType)
struct FPeaksBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks")
    float NoiseFrequency = 0.00015f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks")
    float HeightMin = 3000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks")
    float HeightMax = 25000.f;  // FIX: was 80000 (800m) — caused extreme stalagmite spikes

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks")
    int32 Octaves = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks")
    float Sharpness = 1.8f;  // FIX: was 2.5 — high sharpness on FBM creates razor peaks

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Peaks")
    float DetailAmplitude = 300.f;  // FIX: was 500 — reduced detail noise to soften peaks
};

// ============================================================
//  SURFACE — CLIFFS BIOME
// ============================================================
USTRUCT(BlueprintType)
struct FCliffsBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs")
    float NoiseFrequency = 0.00035f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs")
    float HeightMin = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs")
    float HeightMax = 15000.f;  // FIX: was 40000 — too tall combined with ridge sharpness

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs")
    int32 Octaves = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs")
    float Sharpness = 1.8f;  // FIX: was 3.6 — ridge noise ^ 3.6 = pure stalagmites

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs")
    float TerraceFactor = 0.35f;  // FIX: was 0.6 — less aggressive terracing

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs")
    int32 TerraceSteps = 5;  // FIX: was 12 — fewer steps = smoother cliff faces

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cliffs")
    float DetailAmplitude = 400.f;  // FIX: was 800 — reduced detail noise amplitude
};

// ============================================================
//  SURFACE — MESA BIOME
// ============================================================
USTRUCT(BlueprintType)
struct FMesaBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa")
    float HeightBase = 12000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa")
    float HeightMax = 30000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa")
    int32 PlateauSteps = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mesa")
    float EdgeSharpness = 10.f;
};

// ============================================================
//  SURFACE — CRATER BIOME
// ============================================================
USTRUCT(BlueprintType)
struct FCraterBiomeConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
    float Frequency = 0.00002f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
    float Depth = -1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
    float RimHeight = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
    float ImpactThreshold = -0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
    float RimNoiseAmplitude = 800.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Craters")
    float FloorNoiseAmplitude = 300.f;
};

// ============================================================
//  SURFACE — OVERHANG CONFIG
// ============================================================
USTRUCT(BlueprintType)
struct FOverhangConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overhangs")
    float MaxDistFromSurface = 1500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overhangs")
    float NoiseFrequency = 0.0007f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Overhangs")
    float Amplitude = 0.12f;  // FIX: was 0.35 — was amplifying spikes on steep terrain
};
