// VoxelNoiseSIMD.h
#pragma once

#include "CoreMinimal.h"
#include <immintrin.h>

struct FSkylandColumnCache;
struct FVoxelGenerationConfig;

namespace FVoxelNoiseSIMD
{
    struct FBiomeWeights_AVX2
    {
        __m256 Forest;
        __m256 Peaks;
        __m256 Cliffs;
        __m256 Mesa;
        __m256 Craters;
        __m256 Desert;
        __m256 Ocean;

        void Normalize()
        {
            __m256 Sum = _mm256_add_ps(Forest, _mm256_add_ps(Peaks, _mm256_add_ps(Cliffs, 
                         _mm256_add_ps(Mesa, _mm256_add_ps(Craters, _mm256_add_ps(Desert, Ocean))))));
            __m256 Mask = _mm256_cmp_ps(Sum, _mm256_set1_ps(0.0001f), _CMP_GT_OQ);
            __m256 InvSum = _mm256_div_ps(_mm256_set1_ps(1.f), Sum);
            InvSum = _mm256_blendv_ps(_mm256_set1_ps(1.f), InvSum, Mask);

            Forest  = _mm256_mul_ps(Forest, InvSum);
            Peaks   = _mm256_mul_ps(Peaks, InvSum);
            Cliffs  = _mm256_mul_ps(Cliffs, InvSum);
            Mesa    = _mm256_mul_ps(Mesa, InvSum);
            Craters = _mm256_mul_ps(Craters, InvSum);
            Desert  = _mm256_mul_ps(Desert, InvSum);
            Ocean   = _mm256_mul_ps(Ocean, InvSum);
        }
    };

    __m256 Noise2D_AVX2(__m256 X, __m256 Y, const int32* PermTable);

    // Returns 8 absolute float values in parallel 
    // for 3D Perlin gradient hash noise equations.
    __m256 Noise3D_AVX2(__m256 X, __m256 Y, __m256 Z, const int32* PermTable);

    // Fade polynomial: x*x*x*(x*(x*6 - 15) + 10)
    static FORCEINLINE __m256 Fade_AVX2(__m256 T)
    {
        __m256 Poly = _mm256_fmadd_ps(T, _mm256_set1_ps(6.f), _mm256_set1_ps(-15.f));
        Poly = _mm256_fmadd_ps(T, Poly, _mm256_set1_ps(10.f));
        return _mm256_mul_ps(_mm256_mul_ps(T, _mm256_mul_ps(T, T)), Poly);
    }

    // Linear Interpolation: A + T*(B - A)
    static FORCEINLINE __m256 Lerp_AVX2(__m256 T, __m256 A, __m256 B)
    {
        return _mm256_fmadd_ps(T, _mm256_sub_ps(B, A), A);
    }

    void EvaluateColumn_BiomeWeights_AVX2(
        __m256 X_v, __m256 Y_v, 
        const FVoxelGenerationConfig& Config, 
        const int32* PermTable,
        FBiomeWeights_AVX2& OutWeights, 
        __m256& OutTemp, __m256& OutErosion);

    void EvaluateColumn_SurfaceHeight_AVX2(
        __m256 X_v, __m256 Y_v, 
        const FBiomeWeights_AVX2& Weights, 
        const FVoxelGenerationConfig& Config, 
        const int32* PermTable,
        __m256 InTemp, __m256 InErosion,
        __m256& OutSurfH,
        float CenterH);

    // Accessor for static hash lookup
    const int32* GetPermutationTable();

    // High level Column evaluator
    void EvaluateColumn_AVX2(
        float WX, float WY, 
        float StartZ, float StepZ, 
        int32 Count, float* OutDensities, 
        const int32* PermTable);

    // Phase 1: Surface density batcher
    __m256 EvaluateSurface_AVX2(
        __m256 Z_vec, 
        float SurfaceHeight, 
        float GradientScale,
        float SteepWeight, // (Cliffs + Peaks) For overhangs
        __m256 X_vec, __m256 Y_vec, // For overhang noise
        float SeaLevel,
        const FVector& SeedOffset,
        const int32* PermTable,
        float MaxDist, float Amplitude, float NoiseFrequency);

    /**
     * Phase 1: High Level Surface-Pass Batcher
     * Evaluates heights, linear gradient ramps, and conditional overhangs with 256-bit noise.
     * Updates OutDensities array directly in bulk batches.
     */
    void EvaluateColumn_Surface_AVX2(
        float WX, float WY, 
        float StartZ, float StepZ, 
        int32 Count, float* OutDensities, 
        float SurfaceHeight, float GradientScale, float SteepWeight,
        float SeaLevel, const FVector& SeedOffset,
        const int32* PermTable,
        float MaxDist=1200.f, float Amplitude=300.f, float NoiseFrequency=0.004f);

    // Tier 6: 1D Z-Step Linear Linear Upscaling Upscaling
    void EvaluateColumn_Surface_Upsampled_AVX2(
        float WX, float WY, 
        float StartZ, float StepZ, 
        int32 Count, float* OutDensities, 
        float SurfaceHeight, float GradientScale, float SteepWeight,
        float SeaLevel, const FVector& SeedOffset,
        const int32* PermTable,
        float MaxDist=1200.f, float Amplitude=300.f, float NoiseFrequency=0.004f);

    /**
     * Phase 2: Core rigid worm-tunnel solver
     * Combines 3 noiseoctaves over absolute ABS masks vectors parallel lanes.
     */
    __m256 SampleCaveNoise_AVX2(
        __m256 X, __m256 Y, __m256 Z,
        const FVector& SeedOff,
        float Scale, float Threshold, float WobbleAmplitude, float WobbleFrequency, float Strength,
        const int32* PermTable);

    /**
     * Phase 2: In-place Caves dispatcher
     * Reads existing density buffers, calculates Depth-Clamp fades in parallel, 
     * and subtracts Cave Carve weights before doing Bedrock flooring limits clamping.
     */
    void EvaluateColumn_Caves_AVX2(
        float WX, float WY, 
        float StartZ, float StepZ, 
        int32 Count, float* InOutDensities, 
        float SurfaceHeight, float BedrockJag,
        const FVector& SeedOffset,
        const int32* PermTable,
        float Scale, float Threshold, float WobbleAmplitude, float WobbleFrequency, float Strength,
        float MinDepthBelowSurface, float SurfaceFadeDepth, float BedrockDepth);

    /**
     * Phase 3: Crystal Caverns batcher
     * Subtracts crystal cavity deltas directly from density array in place layout safely.
     */
    void EvaluateColumn_CrystalCaverns_AVX2(
        float WX, float WY, 
        float StartZ, float StepZ, 
        int32 Count, float* InOutDensities, 
        float SurfaceHeight, const FVector& SeedOffset,
        const int32* PermTable,
        float DepthStart, float FadeDepth, float ChamberFrequency, float ChamberThreshold, float ChamberStrength,
        bool bEnableConnectingVeins, float VeinPower, float VeinStrength,
        float CrystalDetailFrequency, float CrystalThreshold, float CrystalAmplitude, int32 MaxNoiseOctaves);

    /**
     * Phase 4: Skylands batcher
     * Maxes skyland density directly onto density array in place layout safely.
     */
    void EvaluateColumn_Skylands_AVX2(
        float WX, float WY, 
        float StartZ, float StepZ, 
        int32 Count, float* InOutDensities, 
        const FSkylandColumnCache& Cache,
        const FVector& SeedOffset,
        const int32* PermTable,
        const FVoxelGenerationConfig& Config);
}
