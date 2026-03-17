// =============================================================================
// CaveLayerConfig.h
// =============================================================================
//
// Configuration for the CAVE LAYER of the voxel world generator.
//
// ── TYPES DEFINED HERE ───────────────────────────────────────────────────────
//   FCaveTunnelsConfig     — worm-noise tunnel parameters
//   FCrystalCavernsConfig  — large carved chamber parameters
//
// ── OVERVIEW ─────────────────────────────────────────────────────────────────
//  Cave generation runs in two sub-passes, both only carving into already-solid
//  terrain (density > 0), so they can never break through the surface crust.
//
//  WORM TUNNELS
//    Two offset Perlin tunnel fields are combined (Cave1 + Cave2). Where both
//    fall below FCaveTunnelsConfig.Threshold a smooth ramp carves a passage.
//    Tunnels fade in below MinDepthBelowSurface and fade out near BedrockDepth.
//    WobbleFrequency / WobbleAmplitude make the diameter vary organically.
//
//  CRYSTAL CAVERNS
//    Two FBM fields produce large hollow chambers at depths below DepthStart.
//    ChamberStrength controls how aggressively each chamber is carved.
//    ConnectingVeins links chambers with narrow passages.
//    Crystals are intentionally disabled (NetDelta = -CarveFactor only) to
//    prevent solid crystal pillars from re-sealing the carved space.
//
//  BEDROCK
//    Always forced solid below FCaveTunnelsConfig.BedrockDepth regardless of
//    what the tunnel or cavern noise evaluates to.
// =============================================================================

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
//  FIXED: Deeper caverns to prevent stalagmites near surface
// ============================================================
USTRUCT(BlueprintType)
struct FCrystalCavernsConfig
{
    GENERATED_BODY()

    // FIXED: Start caverns much deeper underground to prevent surface stalagmites
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Caverns",
        meta=(ToolTip="Minimum depth below surface where crystal caverns can start (cm). Increased to prevent stalagmites near surface."))
    float DepthStart = 8000.f; // Changed from 3000f to 8000f (80m deep)

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
