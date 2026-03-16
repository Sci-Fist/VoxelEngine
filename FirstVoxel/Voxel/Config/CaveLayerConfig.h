// CaveLayerConfig.h
// Configuration for underground worm tunnels and crystal caverns.

#pragma once

#include "CoreMinimal.h"
#include "CaveLayerConfig.generated.h"

// ============================================================
//  CAVE LAYER — WORM TUNNELS
// ============================================================
USTRUCT(BlueprintType)
struct FCaveTunnelsConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tunnels")
    float Threshold = 0.55f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tunnels")
    float Scale = 0.009f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tunnels")
    float Strength = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tunnels")
    float MinDepthBelowSurface = 2000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tunnels")
    float SurfaceFadeDepth = 400.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tunnels")
    float WobbleFrequency = 0.0002f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tunnels")
    float WobbleAmplitude = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bedrock")
    float BedrockDepth = -5000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bedrock")
    float BedrockJagFrequency = 0.001f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bedrock")
    float BedrockJagAmplitude = 800.f;
};

// ============================================================
//  CAVE LAYER — CRYSTAL CAVERNS
// ============================================================
USTRUCT(BlueprintType)
struct FCrystalCavernsConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Caverns")
    float DepthStart = 3000.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Caverns")
    float FadeDepth = 500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Caverns")
    float ChamberFrequency = 0.0012f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Caverns")
    float ChamberThreshold = 0.34f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Caverns")
    float ChamberStrength = 6.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Veins")
    bool bEnableConnectingVeins = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Veins",
        meta=(EditCondition="bEnableConnectingVeins", ClampMin="2.0", ClampMax="32.0"))
    float VeinPower = 16.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Veins",
        meta=(EditCondition="bEnableConnectingVeins", ClampMin="0.0"))
    float VeinStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crystals")
    float CrystalDetailFrequency = 0.002f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crystals",
        meta=(ClampMin="0.0", ClampMax="1.0"))
    float CrystalThreshold = 0.28f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crystals",
        meta=(ClampMin="0.0"))
    float CrystalAmplitude = 2.5f;
};
