// VoxelNoiseSIMD.cpp
#include "VoxelNoiseSIMD.h"
#include "Voxel/Biomes/VoxelBiomeGenerators.h"
#include "Voxel/Biomes/VoxelBiomeGenerators_Shared.h"
#include "Voxel/Config/VoxelGenerationConfig.h"

// Gradient vectors for 3D Perlin Noise
static const float Grad3_X[16] = {1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0};
static const float Grad3_Y[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, 1, -1, -1, 1, -1, 1, -1};
static const float Grad3_Z[16] = {0, 0, 0, 0, 1, 1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1};

namespace FVoxelNoiseSIMD
{
    const int32* GetPermutationTable()
    {
        static int32 Table[512];
        static bool bInitialized = false;
        if (!bInitialized)
        {
            const uint8 p[256] = {
                151,160,137,91,90,168,115,244,14,220,24,142,21,226,163,200,21,114,128,112,
                23,109,249,209,48,27,171,118,50,4,54,49,115,161,89,12,130,220,103,111,
                53,166,121,142,48,220,11,158,11,241,123,183,122,233,126,155,59,108,124,144,
                203,123,128,112,61,60,123,128,112,142,21,226,163,161,89,12,130,220,103,111,
                53,166,121,142,48,220,11,158,11,241,123,183,122,233,126,155,14,220,24,142,
                21,226,163,200,21,114,128,112,23,109,249,209,48,27,171,118,50,4,54,49,
                115,161,89,12,130,220,103,111,53,166,121,142,48,220,11,158,11,241,123,
                183,122,233,126,155,59,108,124,144,203,123,128,112,61,60,123,128,112,142,
                21,226,163,161,89,12,130,220,103,111,53,166,121,142,48,220,11,158,11,
                241,123,183,122,233,126,155,14,220,24,142,21,226,163,200,21,114,128,112,
                23,109,249,209,48,27,171,118,50,4,54,49,115,161,89,12,130,220,103,111,
                53,166,121,142,48,220,11,158,11,241,123,183,122,233,126,155,59,108,124,
                144,203,123,128,112,61,60,123,128,112,142,21,226,163,161,89,12,130
            };
            for (int32 i = 0; i < 512; ++i) Table[i] = p[i & 255];
            bInitialized = true;
        }
        return Table;
    }

    static const float Grad2_X[] = { 1.f, -1.f, 1.f, -1.f };
    static const float Grad2_Y[] = { 1.f, 1.f, -1.f, -1.f };

    // ── 2D NOISE ─────────────────────────────────────────────────────────────
    __m256 Noise2D_AVX2(__m256 X, __m256 Y, const int32* PermTable)
    {
        __m256 X0f = _mm256_floor_ps(X);
        __m256 Y0f = _mm256_floor_ps(Y);

        __m256i X0 = _mm256_and_si256(_mm256_cvtps_epi32(X0f), _mm256_set1_epi32(255));
        __m256i Y0 = _mm256_and_si256(_mm256_cvtps_epi32(Y0f), _mm256_set1_epi32(255));

        __m256 u = _mm256_sub_ps(X, X0f);
        __m256 v = _mm256_sub_ps(Y, Y0f);

        __m256 u_f = Fade_AVX2(u);
        __m256 v_f = Fade_AVX2(v);

        auto GatherP = [&](__m256i idx) { return _mm256_i32gather_epi32(PermTable, idx, 4); };

        __m256i p_X0 = GatherP(X0);
        __m256i p_X1 = GatherP(_mm256_and_si256(_mm256_add_epi32(X0, _mm256_set1_epi32(1)), _mm256_set1_epi32(255)));

        __m256i h00 = GatherP(_mm256_and_si256(_mm256_add_epi32(p_X0, Y0), _mm256_set1_epi32(255)));
        __m256i h01 = GatherP(_mm256_and_si256(_mm256_add_epi32(p_X0, _mm256_add_epi32(Y0, _mm256_set1_epi32(1))), _mm256_set1_epi32(255)));
        __m256i h10 = GatherP(_mm256_and_si256(_mm256_add_epi32(p_X1, Y0), _mm256_set1_epi32(255)));
        __m256i h11 = GatherP(_mm256_and_si256(_mm256_add_epi32(p_X1, _mm256_add_epi32(Y0, _mm256_set1_epi32(1))), _mm256_set1_epi32(255)));

        auto GradDot2 = [&](__m256i hash, __m256 du, __m256 dv) {
            __m256i idx = _mm256_and_si256(hash, _mm256_set1_epi32(3));
            __m256 gx = _mm256_i32gather_ps(Grad2_X, idx, 4);
            __m256 gy = _mm256_i32gather_ps(Grad2_Y, idx, 4);
            return _mm256_add_ps(_mm256_mul_ps(du, gx), _mm256_mul_ps(dv, gy));
        };

        __m256 u1 = _mm256_sub_ps(u, _mm256_set1_ps(1.f));
        __m256 v1 = _mm256_sub_ps(v, _mm256_set1_ps(1.f));

        __m256 d00 = GradDot2(h00, u, v);
        __m256 d01 = GradDot2(h01, u, v1);
        __m256 d10 = GradDot2(h10, u1, v);
        __m256 d11 = GradDot2(h11, u1, v1);

        __m256 lerp_0 = Lerp_AVX2(u_f, d00, d10);
        __m256 lerp_1 = Lerp_AVX2(u_f, d01, d11);

        return Lerp_AVX2(v_f, lerp_0, lerp_1);
    }

    __m256 Noise3D_AVX2(__m256 X, __m256 Y, __m256 Z, const int32* PermTable)
    {
        // 1. Grid Coordinates (Floor)
        __m256 X0f = _mm256_floor_ps(X);
        __m256 Y0f = _mm256_floor_ps(Y);
        __m256 Z0f = _mm256_floor_ps(Z);

        __m256i X0 = _mm256_cvtps_epi32(X0f);
        __m256i Y0 = _mm256_cvtps_epi32(Y0f);
        __m256i Z0 = _mm256_cvtps_epi32(Z0f);

        __m256i Mask255 = _mm256_set1_epi32(255);
        X0 = _mm256_and_si256(X0, Mask255);
        Y0 = _mm256_and_si256(Y0, Mask255);
        Z0 = _mm256_and_si256(Z0, Mask255);

        // 2. Fractional parts & Fade Curves
        __m256 u = _mm256_sub_ps(X, X0f);
        __m256 v = _mm256_sub_ps(Y, Y0f);
        __m256 w = _mm256_sub_ps(Z, Z0f);

        __m256 u_f = Fade_AVX2(u);
        __m256 v_f = Fade_AVX2(v);
        __m256 w_f = Fade_AVX2(w);

        // 3. Hash corner lookups using Gather instructions
        auto GatherP = [&](__m256i idx) { return _mm256_i32gather_epi32(PermTable, idx, 4); };

        __m256i p_X0 = GatherP(X0);
        __m256i p_X1 = GatherP(_mm256_add_epi32(X0, _mm256_set1_epi32(1)));

        __m256i A = _mm256_and_si256(_mm256_add_epi32(p_X0, Y0), Mask255);
        __m256i B = _mm256_and_si256(_mm256_add_epi32(p_X1, Y0), Mask255);

        __m256i p_A  = GatherP(A);
        __m256i p_A1 = GatherP(_mm256_and_si256(_mm256_add_epi32(A, _mm256_set1_epi32(1)), Mask255));
        __m256i p_B  = GatherP(B);
        __m256i p_B1 = GatherP(_mm256_and_si256(_mm256_add_epi32(B, _mm256_set1_epi32(1)), Mask255));

        __m256i Z0_1 = _mm256_add_epi32(Z0, _mm256_set1_epi32(1));

        __m256i h000 = GatherP(_mm256_and_si256(_mm256_add_epi32(p_A,  Z0), Mask255));
        __m256i h001 = GatherP(_mm256_and_si256(_mm256_add_epi32(p_A,  Z0_1), Mask255));
        __m256i h010 = GatherP(_mm256_and_si256(_mm256_add_epi32(p_A1, Z0), Mask255));
        __m256i h011 = GatherP(_mm256_and_si256(_mm256_add_epi32(p_A1, Z0_1), Mask255));
        
        __m256i h100 = GatherP(_mm256_and_si256(_mm256_add_epi32(p_B,  Z0), Mask255));
        __m256i h101 = GatherP(_mm256_and_si256(_mm256_add_epi32(p_B,  Z0_1), Mask255));
        __m256i h110 = GatherP(_mm256_and_si256(_mm256_add_epi32(p_B1, Z0), Mask255));
        __m256i h111 = GatherP(_mm256_and_si256(_mm256_add_epi32(p_B1, Z0_1), Mask255));

        // 4. Dot products with Gradients
        // Use hash & 15 to pick one of 16 gradient vectors
        __m256i m15 = _mm256_set1_epi32(15);
        auto GradDot = [&](__m256i hash, __m256 du, __m256 dv, __m256 dw) {
            __m256i idx = _mm256_and_si256(hash, m15);
            __m256 gx = _mm256_i32gather_ps(Grad3_X, idx, 4);
            __m256 gy = _mm256_i32gather_ps(Grad3_Y, idx, 4);
            __m256 gz = _mm256_i32gather_ps(Grad3_Z, idx, 4);
            return _mm256_add_ps(_mm256_mul_ps(du, gx), _mm256_add_ps(_mm256_mul_ps(dv, gy), _mm256_mul_ps(dw, gz)));
        };

        __m256 u1 = _mm256_sub_ps(u, _mm256_set1_ps(1.f));
        __m256 v1 = _mm256_sub_ps(v, _mm256_set1_ps(1.f));
        __m256 w1 = _mm256_sub_ps(w, _mm256_set1_ps(1.f));

        __m256 d000 = GradDot(h000, u, v, w);
        __m256 d001 = GradDot(h001, u, v, w1);
        __m256 d010 = GradDot(h010, u, v1, w);
        __m256 d011 = GradDot(h011, u, v1, w1);
        __m256 d100 = GradDot(h100, u1, v, w);
        __m256 d101 = GradDot(h101, u1, v, w1);
        __m256 d110 = GradDot(h110, u1, v1, w);
        __m256 d111 = GradDot(h111, u1, v1, w1);

        // 5. Interpolation Cascade
        __m256 lerp_u00 = Lerp_AVX2(u_f, d000, d100);
        __m256 lerp_u01 = Lerp_AVX2(u_f, d001, d101);
        __m256 lerp_u10 = Lerp_AVX2(u_f, d010, d110);
        __m256 lerp_u11 = Lerp_AVX2(u_f, d011, d111);

        __m256 lerp_v0 = Lerp_AVX2(v_f, lerp_u00, lerp_u10);
        __m256 lerp_v1 = Lerp_AVX2(v_f, lerp_u01, lerp_u11);

        return Lerp_AVX2(w_f, lerp_v0, lerp_v1);
    }

    void EvaluateColumn_AVX2(
        float WX, float WY, 
        float StartZ, float StepZ, 
        int32 Count, float* OutDensities, 
        const int32* PermTable)
    {
        __m256 WX_vec = _mm256_set1_ps(WX);
        __m256 WY_vec = _mm256_set1_ps(WY);

        int32 i = 0;
        for (; i <= Count - 8; i += 8)
        {
            // Pack Z-axis coordinates for 8 items
            __m256 Z_vec = _mm256_set_ps(
                StartZ + (i+7)*StepZ, StartZ + (i+6)*StepZ,
                StartZ + (i+5)*StepZ, StartZ + (i+4)*StepZ,
                StartZ + (i+3)*StepZ, StartZ + (i+2)*StepZ,
                StartZ + (i+1)*StepZ, StartZ + (i+0)*StepZ
            );

            __m256 Density = Noise3D_AVX2(WX_vec, WY_vec, Z_vec, PermTable);
            _mm256_storeu_ps(&OutDensities[i], Density);
        }

        // Remainder cleanup (Scalar loop for final <8 items)
        for (; i < Count; ++i)
        {
            const float z = StartZ + i * StepZ;
            // Native fallback scalar perlin not accessible here simply, 
            // but we can load 1 element into SIMD to evaluate safely!
            __m256 Z_single = _mm256_set1_ps(z);
            __m256 Density_single = Noise3D_AVX2(WX_vec, WY_vec, Z_single, PermTable);
            float Temp[8];
            _mm256_storeu_ps(Temp, Density_single);
            OutDensities[i] = Temp[0];
        }
    }

    // =========================================================================
    // ── PHASE 1: SURFACE GRADIENTS & OVERHANGS ──────────────────────────────
    // =========================================================================

    __m256 EvaluateSurface_AVX2(
        __m256 Z_vec, 
        float SurfaceHeight, 
        float GradientScale,
        float SteepWeight,
        __m256 X_vec, __m256 Y_vec,
        float SeaLevel,
        const FVector& SeedOffset,
        const int32* PermTable,
        float MaxDist, float Amplitude, float NoiseFrequency)
    {
        // 1. Base Gradient Ramp: (SurfaceHeight - Z) / Scale
        __m256 Surf_v = _mm256_set1_ps(SurfaceHeight);
        __m256 Scale_v = _mm256_set1_ps(GradientScale);
        __m256 D = _mm256_div_ps(_mm256_sub_ps(Surf_v, Z_vec), Scale_v);

        // 2. Overhangs (Only if above sea level and steep terrain weights present)
        if (SteepWeight > 0.05f)
        {
            __m256 Dist = _mm256_sub_ps(Z_vec, Surf_v);
            __m256 AbsDist = _mm256_andnot_ps(_mm256_set1_ps(-0.0f), Dist); // FAbs

            __m256 MaxDist_v = _mm256_set1_ps(MaxDist);
            __m256 Near = _mm256_max_ps(_mm256_sub_ps(_mm256_set1_ps(1.f), _mm256_div_ps(AbsDist, MaxDist_v)), _mm256_setzero_ps());

            // Check Ocean depth & distance trigger mask
            __m256 Sea_v = _mm256_set1_ps(SeaLevel);
            __m256 Mask_Sea  = _mm256_cmp_ps(Z_vec, Sea_v, _CMP_GT_OQ);
            __m256 Mask_Dist = _mm256_cmp_ps(AbsDist, MaxDist_v, _CMP_LT_OQ);
            __m256 FinalMask = _mm256_and_ps(Mask_Sea, Mask_Dist);

            if (_mm256_movemask_ps(FinalMask) != 0) // Executes if any lane passes guard
            {
                __m256 OffX = _mm256_set1_ps(SeedOffset.X);
                __m256 OffY = _mm256_set1_ps(SeedOffset.Y);
                __m256 OffZ = _mm256_set1_ps(SeedOffset.Z);

                __m256 Freq_v = _mm256_set1_ps(NoiseFrequency);
                __m256 FreqZ_v = _mm256_set1_ps(NoiseFrequency * 1.8f);

                __m256 sX = _mm256_mul_ps(_mm256_add_ps(X_vec, OffX), Freq_v);
                __m256 sY = _mm256_mul_ps(_mm256_add_ps(Y_vec, OffY), Freq_v);
                __m256 sZ = _mm256_mul_ps(_mm256_add_ps(Z_vec, OffZ), FreqZ_v);

                __m256 Ov_Noise = Noise3D_AVX2(sX, sY, sZ, PermTable);

                // D += Ov * Near * Amplitude * SteepWeight
                __m256 Factor = _mm256_mul_ps(Near, _mm256_set1_ps(Amplitude * SteepWeight));
                __m256 Ov_Add = _mm256_mul_ps(Ov_Noise, Factor);

                // Blend under mask trigger securely
                Ov_Add = _mm256_and_ps(Ov_Add, FinalMask);
                D = _mm256_add_ps(D, Ov_Add);
            }
        }
        return D;
    }

    void EvaluateColumn_Surface_AVX2(
        float WX, float WY, 
        float StartZ, float StepZ, 
        int32 Count, float* OutDensities, 
        float SurfaceHeight, float GradientScale, float SteepWeight,
        float SeaLevel, const FVector& SeedOffset,
        const int32* PermTable,
        float MaxDist, float Amplitude, float NoiseFrequency)
    {
        __m256 WX_vec = _mm256_set1_ps(WX);
        __m256 WY_vec = _mm256_set1_ps(WY);

        int32 i = 0;
        for (; i <= Count - 8; i += 8)
        {
            __m256 Z_vec = _mm256_set_ps(
                StartZ + (i+7)*StepZ, StartZ + (i+6)*StepZ,
                StartZ + (i+5)*StepZ, StartZ + (i+4)*StepZ,
                StartZ + (i+3)*StepZ, StartZ + (i+2)*StepZ,
                StartZ + (i+1)*StepZ, StartZ + (i+0)*StepZ
            );

            __m256 D = EvaluateSurface_AVX2(
                Z_vec, SurfaceHeight, GradientScale, SteepWeight, 
                WX_vec, WY_vec, SeaLevel, SeedOffset, PermTable,
                MaxDist, Amplitude, NoiseFrequency);

            _mm256_storeu_ps(&OutDensities[i], D);
        }

        // Remainder
        for (; i < Count; ++i)
        {
            const float z = StartZ + i * StepZ;
            __m256 Z_single = _mm256_set1_ps(z);
            __m256 D_single = EvaluateSurface_AVX2(
                Z_single, SurfaceHeight, GradientScale, SteepWeight, 
                WX_vec, WY_vec, SeaLevel, SeedOffset, PermTable,
                MaxDist, Amplitude, NoiseFrequency);

            float Temp[8];
            _mm256_storeu_ps(Temp, D_single);
            OutDensities[i] = Temp[0];
        }
    }

    // Tier 6: 1D Z-Step Linear Linear Upscaling Upscaling
    void EvaluateColumn_Surface_Upsampled_AVX2(
        float WX, float WY, 
        float StartZ, float StepZ, 
        int32 Count, float* OutDensities, 
        float SurfaceHeight, float GradientScale, float SteepWeight,
        float SeaLevel, const FVector& SeedOffset,
        const int32* PermTable,
        float MaxDist, float Amplitude, float NoiseFrequency)
    {
        if (Count <= 0) return;

        __m256 WX_vec = _mm256_set1_ps(WX);
        __m256 WY_vec = _mm256_set1_ps(WY);

        const int32 Stride = 4;
        const int32 NumNodes = (Count + Stride - 1) / Stride + 1;
        float LowRes[128] = {0}; 

        int32 n = 0;
        for (; n <= NumNodes - 8; n += 8)
        {
            __m256 Z_vec = _mm256_set_ps(
                StartZ + (n + 7*Stride)*StepZ, StartZ + (n + 6*Stride)*StepZ,
                StartZ + (n + 5*Stride)*StepZ, StartZ + (n + 4*Stride)*StepZ,
                StartZ + (n + 3*Stride)*StepZ, StartZ + (n + 2*Stride)*StepZ,
                StartZ + (n + 1*Stride)*StepZ, StartZ + (n + 0*Stride)*StepZ
            );
            __m256 D = EvaluateSurface_AVX2(
                Z_vec, SurfaceHeight, GradientScale, SteepWeight, 
                WX_vec, WY_vec, SeaLevel, SeedOffset, PermTable,
                MaxDist, Amplitude, NoiseFrequency);
            _mm256_storeu_ps(&LowRes[n], D);
        }

        for (; n < NumNodes; ++n)
        {
            const float z = StartZ + (n * Stride) * StepZ;
            __m256 Z_single = _mm256_set1_ps(z);
            __m256 D_single = EvaluateSurface_AVX2(
                Z_single, SurfaceHeight, GradientScale, SteepWeight, 
                WX_vec, WY_vec, SeaLevel, SeedOffset, PermTable,
                MaxDist, Amplitude, NoiseFrequency);
            float Temp[8];
            _mm256_storeu_ps(Temp, D_single);
            LowRes[n] = Temp[0];
        }

        for (int32 j = 0; j < NumNodes - 1; ++j)
        {
            const float V0 = LowRes[j];
            const float V1 = LowRes[j+1];
            const int32 BaseIdx = j * Stride;
            
            for (int32 k = 0; k < Stride; ++k)
            {
                const int32 OutIdx = BaseIdx + k;
                if (OutIdx < Count)
                {
                    const float t = (float)k / (float)Stride;
                    OutDensities[OutIdx] = V0 + (V1 - V0) * t;
                }
            }
        }
    }

    // =========================================================================
    // ── PHASE 2: CAVES & BEDROCK ───────────────────────────────────────────
    // =========================================================================

    static __m256 SmoothStep_AVX2(__m256 Edge0, __m256 Edge1, __m256 X)
    {
        __m256 num = _mm256_sub_ps(X, Edge0);
        __m256 den = _mm256_sub_ps(Edge1, Edge0);
        __m256 t = _mm256_max_ps(_mm256_setzero_ps(), _mm256_min_ps(_mm256_set1_ps(1.f), _mm256_div_ps(num, den)));
        __m256 t2 = _mm256_mul_ps(t, t);
        __m256 term = _mm256_sub_ps(_mm256_set1_ps(3.f), _mm256_mul_ps(_mm256_set1_ps(2.f), t));
        return _mm256_mul_ps(t2, term);
    }

    __m256 SampleCaveNoise_AVX2(
        __m256 X, __m256 Y, __m256 Z,
        const FVector& SeedOff,
        float Scale, float Threshold, float WobbleAmplitude, float WobbleFrequency, float Strength,
        const int32* PermTable)
    {
        // P = WorldPos + SeedOff
        __m256 pX = _mm256_add_ps(X, _mm256_set1_ps(SeedOff.X));
        __m256 pY = _mm256_add_ps(Y, _mm256_set1_ps(SeedOff.Y));
        __m256 pZ = _mm256_add_ps(Z, _mm256_set1_ps(SeedOff.Z));

        __m256 cs = _mm256_set1_ps(Scale);
        
        // C1 = Abs(Noise3D(P.X*cs, P.Y*cs, P.Z*cs))
        __m256 c1_Noise = Noise3D_AVX2(_mm256_mul_ps(pX, cs), _mm256_mul_ps(pY, cs), _mm256_mul_ps(pZ, cs), PermTable);
        __m256 C1 = _mm256_andnot_ps(_mm256_set1_ps(-0.0f), c1_Noise); // Abs

        __m256 MaxThresh_v = _mm256_set1_ps(Threshold + WobbleAmplitude);
        __m256 Mask1 = _mm256_cmp_ps(C1, MaxThresh_v, _CMP_LT_OQ);

        __m256 CV = C1;
        __m256 Result = _mm256_setzero_ps();

        if (_mm256_movemask_ps(Mask1) != 0)
        {
            // C2 = Abs(Noise3D(P.X*cs*0.7f, P.Y*cs*0.7f, P.Z*cs*1.3f + 5.f))
            __m256 c2X = _mm256_mul_ps(pX, _mm256_mul_ps(cs, _mm256_set1_ps(0.7f)));
            __m256 c2Y = _mm256_mul_ps(pY, _mm256_mul_ps(cs, _mm256_set1_ps(0.7f)));
            __m256 c2Z = _mm256_add_ps(_mm256_mul_ps(pZ, _mm256_mul_ps(cs, _mm256_set1_ps(1.3f))), _mm256_set1_ps(5.f));

            __m256 c2_Noise = Noise3D_AVX2(c2X, c2Y, c2Z, PermTable);
            __m256 C2 = _mm256_andnot_ps(_mm256_set1_ps(-0.0f), c2_Noise);

            CV = _mm256_add_ps(C1, C2);
            __m256 Mask2 = _mm256_cmp_ps(CV, MaxThresh_v, _CMP_LT_OQ);
            __m256 FinalMask = _mm256_and_ps(Mask1, Mask2);

            if (_mm256_movemask_ps(FinalMask) != 0)
            {
                // Wobble
                __m256 wobFreq = _mm256_set1_ps(WobbleFrequency);
                __m256 wX = _mm256_mul_ps(pX, wobFreq);
                __m256 wY = _mm256_mul_ps(pY, wobFreq);
                __m256 wZ = _mm256_mul_ps(pZ, wobFreq);

                __m256 Wobble = _mm256_mul_ps(Noise3D_AVX2(wX, wY, wZ, PermTable), _mm256_set1_ps(WobbleAmplitude));
                __m256 Thresh = _mm256_add_ps(_mm256_set1_ps(Threshold), Wobble);

                // if (CV < Thresh) { return (1 - CV/Thresh) * Strength; }
                __m256 Mask3 = _mm256_cmp_ps(CV, Thresh, _CMP_LT_OQ);
                __m256 ValidMask = _mm256_and_ps(FinalMask, Mask3);

                __m256 t = _mm256_sub_ps(_mm256_set1_ps(1.f), _mm256_div_ps(CV, Thresh));
                __m256 Val = _mm256_mul_ps(t, _mm256_set1_ps(Strength));

                Result = _mm256_blendv_ps(Result, Val, ValidMask);
            }
        }
        return Result;
    }

    void EvaluateColumn_Caves_AVX2(
        float WX, float WY, 
        float StartZ, float StepZ, 
        int32 Count, float* InOutDensities, 
        float SurfaceHeight, float BedrockJag,
        const FVector& SeedOffset,
        const int32* PermTable,
        float Scale, float Threshold, float WobbleAmplitude, float WobbleFrequency, float Strength,
        float MinDepthBelowSurface, float SurfaceFadeDepth, float BedrockDepth)
    {
        __m256 WX_v = _mm256_set1_ps(WX);
        __m256 WY_v = _mm256_set1_ps(WY);
        __m256 Surf_v = _mm256_set1_ps(SurfaceHeight);
        
        const float EffMinDepth = FMath::Max(MinDepthBelowSurface, 600.f);
        __m256 MinDepth_v = _mm256_set1_ps(EffMinDepth);

        const float BedrockLimit = BedrockDepth + BedrockJag;
        __m256 Bedrock_v = _mm256_set1_ps(BedrockLimit);

        int32 i = 0;
        for (; i <= Count - 8; i += 8)
        {
            __m256 Z_v = _mm256_set_ps(
                StartZ + (i+7)*StepZ, StartZ + (i+6)*StepZ,
                StartZ + (i+5)*StepZ, StartZ + (i+4)*StepZ,
                StartZ + (i+3)*StepZ, StartZ + (i+2)*StepZ,
                StartZ + (i+1)*StepZ, StartZ + (i+0)*StepZ
            );

            __m256 D_v = _mm256_loadu_ps(&InOutDensities[i]);

            // 1. D > 0.05f check
            __m256 Mask_Solid = _mm256_cmp_ps(D_v, _mm256_set1_ps(0.05f), _CMP_GT_OQ);

            if (_mm256_movemask_ps(Mask_Solid) != 0)
            {
                // DepthBelow = Max(0, SurfaceHeight - Z)
                __m256 Depth = _mm256_max_ps(_mm256_setzero_ps(), _mm256_sub_ps(Surf_v, Z_v));

                // SF = Clamp((DepthBelow - MinDepth) / Fade, 0, 1)
                __m256 sf_num = _mm256_sub_ps(Depth, MinDepth_v);
                __m256 SF = _mm256_max_ps(_mm256_setzero_ps(), _mm256_min_ps(_mm256_set1_ps(1.f), _mm256_div_ps(sf_num, _mm256_set1_ps(SurfaceFadeDepth))));

                // BF = Clamp((Z - BedrockLimit)/1000.f, 0, 1)
                __m256 bf_num = _mm256_sub_ps(Z_v, Bedrock_v);
                __m256 BF = _mm256_max_ps(_mm256_setzero_ps(), _mm256_min_ps(_mm256_set1_ps(1.f), _mm256_div_ps(bf_num, _mm256_set1_ps(1000.f))));

                __m256 CF = _mm256_mul_ps(SF, BF);
                __m256 Mask_CF = _mm256_cmp_ps(CF, _mm256_setzero_ps(), _CMP_GT_OQ);
                __m256 Active_Mask = _mm256_and_ps(Mask_Solid, Mask_CF);

                if (_mm256_movemask_ps(Active_Mask) != 0)
                {
                    __m256 Caves = SampleCaveNoise_AVX2(WX_v, WY_v, Z_v, SeedOffset, Scale, Threshold, WobbleAmplitude, WobbleFrequency, Strength, PermTable);
                    __m256 Carve = _mm256_mul_ps(Caves, CF);

                    // Min(Carve, 0.85f)
                    Carve = _mm256_min_ps(Carve, _mm256_set1_ps(0.85f));

                    // D_v -= Carve (under Active_Mask)
                    Carve = _mm256_and_ps(Carve, Active_Mask);
                    D_v = _mm256_sub_ps(D_v, Carve);
                }
            }

            // 2. Bedrock Clamp: if (Z < BedrockDepth + BJ) D = 2.f
            __m256 Mask_Bedrock = _mm256_cmp_ps(Z_v, _mm256_set1_ps(BedrockDepth), _CMP_LT_OQ);
            D_v = _mm256_blendv_ps(D_v, _mm256_set1_ps(2.f), Mask_Bedrock);

            _mm256_storeu_ps(&InOutDensities[i], D_v);
        }

        // Remainder
        for (; i < Count; ++i)
        {
            const float z = StartZ + i * StepZ;
            float D = InOutDensities[i];

            if (D > 0.05f)
            {
                const float DepthBelow = FMath::Max(0.f, SurfaceHeight - z);
                if (DepthBelow > EffMinDepth)
                {
                    const float SF = FMath::Clamp((DepthBelow - EffMinDepth) / SurfaceFadeDepth, 0.f, 1.f);
                    const float BF = FMath::Clamp((z - BedrockLimit) / 1000.f, 0.f, 1.f);
                    const float CF = SF * BF;

                    if (CF > 0.f)
                    {
                        __m256 X_s = _mm256_set1_ps(WX);
                        __m256 Y_s = _mm256_set1_ps(WY);
                        __m256 Z_s = _mm256_set1_ps(z);

                        __m256 Caves = SampleCaveNoise_AVX2(X_s, Y_s, Z_s, SeedOffset, Scale, Threshold, WobbleAmplitude, WobbleFrequency, Strength, PermTable);
                        float Temp[8];
                        _mm256_storeu_ps(Temp, Caves);

                        D -= FMath::Min(Temp[0] * CF, 0.85f);
                    }
                }
            }

            if (z < BedrockDepth) D = 2.f;
            InOutDensities[i] = D;
        }
    }

// ── FBM COMPOSITOR helper ────────────────────────────────────────────────
static __m256 FBM_AVX2(
    __m256 X, __m256 Y, __m256 Z,
    int32 Octaves, float Lacunarity, float Gain, int32 MaxOctaves,
    const int32* PermTable)
{
    const int32 N = FMath::Clamp(FMath::Min(Octaves, MaxOctaves), 1, 16);
    __m256 V = _mm256_setzero_ps();
    __m256 A = _mm256_set1_ps(0.5f);
    __m256 F = _mm256_set1_ps(1.f);

    for (int32 i = 0; i < N; ++i)
    {
        __m256 n = Noise3D_AVX2(_mm256_mul_ps(X, F), _mm256_mul_ps(Y, F), _mm256_mul_ps(Z, F), PermTable);
        V = _mm256_add_ps(V, _mm256_mul_ps(n, A));
        F = _mm256_mul_ps(F, _mm256_set1_ps(Lacunarity));
        A = _mm256_mul_ps(A, _mm256_set1_ps(Gain));
    }
    return V;
}

void EvaluateColumn_CrystalCaverns_AVX2(
    float WX, float WY, 
    float StartZ, float StepZ, 
    int32 Count, float* InOutDensities, 
    float SurfaceHeight, const FVector& SeedOffset,
    const int32* PermTable,
    float DepthStart, float FadeDepth, float ChamberFrequency, float ChamberThreshold, float ChamberStrength,
    bool bEnableConnectingVeins, float VeinPower, float VeinStrength,
    float CrystalDetailFrequency, float CrystalThreshold, float CrystalAmplitude, int32 MaxNoiseOctaves)
{
    __m256 WX_v = _mm256_set1_ps(WX + SeedOffset.X);
    __m256 WY_v = _mm256_set1_ps(WY + SeedOffset.Y);
    __m256 nZ_off = _mm256_set1_ps(SeedOffset.Z);

    const float CC_base = SurfaceHeight - DepthStart;
    __m256 CC_v = _mm256_set1_ps(CC_base);
    __m256 FadeDepth_v = _mm256_set1_ps(FadeDepth);

    const float CF = FMath::Max(ChamberFrequency, 0.00005f);
    __m256 CF_v = _mm256_set1_ps(CF);

    int32 i = 0;
    for (; i <= Count - 8; i += 8)
    {
        __m256 Z_v = _mm256_set_ps(
            StartZ + (i+7)*StepZ, StartZ + (i+6)*StepZ,
            StartZ + (i+5)*StepZ, StartZ + (i+4)*StepZ,
            StartZ + (i+3)*StepZ, StartZ + (i+2)*StepZ,
            StartZ + (i+1)*StepZ, StartZ + (i+0)*StepZ
        );

        __m256 nZ = _mm256_add_ps(Z_v, nZ_off);

        // Guard Z > CC or Z < CC - Fade - 8000
        __m256 Guard1 = _mm256_cmp_ps(Z_v, CC_v, _CMP_GT_OQ);
        __m256 Guard2 = _mm256_cmp_ps(Z_v, _mm256_set1_ps(CC_base - (FadeDepth + 8000.f)), _CMP_LT_OQ);
        __m256 SkipMask = _mm256_or_ps(Guard1, Guard2);

        // Calculate Fade
        __m256 num_fade = _mm256_sub_ps(CC_v, Z_v);
        __m256 Fade = _mm256_max_ps(_mm256_setzero_ps(), _mm256_min_ps(_mm256_set1_ps(1.f), _mm256_div_ps(num_fade, FadeDepth_v)));

        // Ch1 = Abs(FBM(P*CF, 4))
        __m256 ch1_x = _mm256_mul_ps(WX_v, CF_v);
        __m256 ch1_y = _mm256_mul_ps(WY_v, CF_v);
        __m256 ch1_z = _mm256_mul_ps(nZ, CF_v);
        __m256 Ch1_full = FBM_AVX2(ch1_x, ch1_y, ch1_z, 4, 2.f, 0.5f, MaxNoiseOctaves, PermTable);
        __m256 Ch1 = _mm256_andnot_ps(_mm256_set1_ps(-0.0f), Ch1_full);

        // Ch2 = Abs(FBM(P*CF*0.7, nZ*CF+5678, 3))
        __m256 ch2_x = _mm256_mul_ps(ch1_x, _mm256_set1_ps(0.7f));
        __m256 ch2_y = _mm256_mul_ps(ch1_y, _mm256_set1_ps(0.7f));
        __m256 ch2_z = _mm256_add_ps(_mm256_mul_ps(nZ, _mm256_mul_ps(CF_v, _mm256_set1_ps(0.7f))), _mm256_set1_ps(5678.f));
        __m256 Ch2_full = FBM_AVX2(ch2_x, ch2_y, ch2_z, 3, 2.1f, 0.5f, MaxNoiseOctaves, PermTable);
        __m256 Ch2 = _mm256_andnot_ps(_mm256_set1_ps(-0.0f), Ch2_full);

        // Carve = Max(0, Threshold - Min(Ch1, Ch2)) * Strength
        __m256 MinCh = _mm256_min_ps(Ch1, Ch2);
        __m256 Carve = _mm256_mul_ps(_mm256_max_ps(_mm256_setzero_ps(), _mm256_sub_ps(_mm256_set1_ps(ChamberThreshold), MinCh)), _mm256_set1_ps(ChamberStrength));

        // Connecting Veins
        if (bEnableConnectingVeins)
        {
            __m256 vn_x = _mm256_mul_ps(ch1_x, _mm256_set1_ps(2.5f));
            __m256 vn_y = _mm256_mul_ps(ch1_y, _mm256_set1_ps(2.5f));
            __m256 vn_z = _mm256_mul_ps(nZ, _mm256_mul_ps(CF_v, _mm256_set1_ps(2.5f)));
            __m256 VN = FBM_AVX2(vn_x, vn_y, vn_z, 2, 2.f, 0.5f, MaxNoiseOctaves, PermTable);
            
            // Max(0, 1 - Abs(VN))
            __m256 AbsVN = _mm256_andnot_ps(_mm256_set1_ps(-0.0f), VN);
            __m256 vn_sub = _mm256_max_ps(_mm256_setzero_ps(), _mm256_sub_ps(_mm256_set1_ps(1.f), AbsVN));
            
            __m256 vn_pow = _mm256_mul_ps(vn_sub, vn_sub); // approx Pow2
            Carve = _mm256_add_ps(Carve, _mm256_mul_ps(vn_pow, _mm256_set1_ps(VeinStrength)));
        }
        Carve = _mm256_max_ps(_mm256_setzero_ps(), _mm256_min_ps(_mm256_set1_ps(1.5f), Carve));

        // Detail crystals: CF2 = Max(...)
        __m256 cdx = _mm256_mul_ps(WX_v, _mm256_set1_ps(CrystalDetailFrequency));
        __m256 cdy = _mm256_mul_ps(WY_v, _mm256_set1_ps(CrystalDetailFrequency));
        __m256 cdz = _mm256_mul_ps(nZ, _mm256_set1_ps(CrystalDetailFrequency));
        __m256 BG_C = Noise3D_AVX2(cdx, cdy, cdz, PermTable);
        __m256 CF2 = _mm256_mul_ps(_mm256_max_ps(_mm256_setzero_ps(), _mm256_sub_ps(BG_C, _mm256_set1_ps(CrystalThreshold))), _mm256_set1_ps(CrystalAmplitude));

        // Return (-(Carve*1.3) + CF2 * Clamp(Carve, 0,1)) * Fade
        __m256 CarveClamp = _mm256_max_ps(_mm256_setzero_ps(), _mm256_min_ps(_mm256_set1_ps(1.f), Carve));
        __m256 Delta = _mm256_mul_ps(_mm256_add_ps(_mm256_mul_ps(Carve, _mm256_set1_ps(-1.3f)), _mm256_mul_ps(CF2, CarveClamp)), Fade);

        // Apply Mask zeroing Triggering
        Delta = _mm256_blendv_ps(Delta, _mm256_setzero_ps(), SkipMask);

        // Load update updates InOutDensities
        __m256 InD = _mm256_loadu_ps(&InOutDensities[i]);
        InD = _mm256_add_ps(InD, Delta);
        _mm256_storeu_ps(&InOutDensities[i], InD);
    }

    // Remainder loop
    for (; i < Count; ++i)
    {
        const float z = StartZ + i * StepZ;
        if (z > CC_base) continue;
        if (z < CC_base - (FadeDepth + 8000.f)) continue;

        const float Fade = FMath::Clamp((CC_base - z) / FadeDepth, 0.f, 1.f);
        const float nZ = z + SeedOffset.Z;

        // Corrected scalar parameters with parenthesis triggers triggers
        const float Ch1 = FMath::Abs(BG_FBM((WX + SeedOffset.X) * CF, (WY + SeedOffset.Y) * CF, nZ * CF, 4, 2.f, 0.5f, MaxNoiseOctaves));
        const float Ch2 = FMath::Abs(BG_FBM((WX + SeedOffset.X) * CF * 0.7f, (WY + SeedOffset.Y) * CF * 0.7f, nZ * CF + 5678.f, 3, 2.1f, 0.5f, MaxNoiseOctaves));

        float Carve = FMath::Max(0.f, ChamberThreshold - FMath::Min(Ch1, Ch2)) * ChamberStrength;
        if (bEnableConnectingVeins)
        {
            const float VN = BG_FBM((WX + SeedOffset.X) * CF * 2.5f, (WY + SeedOffset.Y) * CF * 2.5f, nZ * CF * 2.5f, 2, 2.f, 0.5f, MaxNoiseOctaves);
            Carve += FMath::Pow(FMath::Max(0.f, 1.f - FMath::Abs(VN)), VeinPower) * VeinStrength;
        }
        Carve = FMath::Clamp(Carve, 0.f, 1.5f);

        // CD Noise Noise multipliers multipliers triggers
        const float CF2 = FMath::Max(0.f, BG_Noise((WX + SeedOffset.X) * CrystalDetailFrequency, (WY + SeedOffset.Y) * CrystalDetailFrequency, nZ * CrystalDetailFrequency) - CrystalThreshold) * CrystalAmplitude;
        const float Delta = (-(Carve * 1.3f) + CF2 * FMath::Clamp(Carve, 0.f, 1.f)) * Fade;

        InOutDensities[i] += Delta;
    }
}

// ── SKYLANDS BATCHER ────────────────────────────────────────────────────────
void EvaluateColumn_Skylands_AVX2(
    float WX, float WY, 
    float StartZ, float StepZ, 
    int32 Count, float* InOutDensities, 
    const FSkylandColumnCache& Cache,
    const FVector& SeedOffset,
    const int32* PermTable,
    const FVoxelGenerationConfig& Config)
{
    if (!Cache.bHasSkyland) return;

    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
    __m256 WX_v = _mm256_set1_ps(WX + SeedOffset.X);
    __m256 WY_v = _mm256_set1_ps(WY + SeedOffset.Y);
    __m256 nZ_off = _mm256_set1_ps(SeedOffset.Z);

    int32 i = 0;
    for (; i <= Count - 8; i += 8)
    {
        __m256 Z_v = _mm256_set_ps(
            StartZ + (i+7)*StepZ, StartZ + (i+6)*StepZ,
            StartZ + (i+5)*StepZ, StartZ + (i+4)*StepZ,
            StartZ + (i+3)*StepZ, StartZ + (i+2)*StepZ,
            StartZ + (i+1)*StepZ, StartZ + (i+0)*StepZ
        );

        __m256 nZ = _mm256_add_ps(Z_v, nZ_off);
        __m256 MaxD_v = _mm256_set1_ps(-2.f);

        for (const FSkylandIslandData& Isl : Cache.Islands)
        {
            float Falloffs[8];
            const float Margin = Isl.HalfThick * 0.4f;
            bool bAnyValid = false;

            for (int32 j = 0; j < 8; ++j)
            {
                const float z = StartZ + (i+j)*StepZ;
                if (z < Isl.SkyAlt - Isl.HalfThick - Margin || z > Isl.SkyAlt + Isl.HalfThick + Margin)
                {
                    Falloffs[j] = 0.f;
                    continue;
                }

                const float tC = FMath::Clamp((z - Isl.SkyAlt) / (Isl.HalfThick + Margin + 1.f), -1.f, 1.f);
                float Falloff = 0.40f; 
                if (tC >= 0.f)
                {
                    const float FZ = 0.35f;
                    Falloff = (tC < FZ) ? 1.f : FMath::SmoothStep(0.f, 1.f, 1.f - (tC - FZ) / (1.f - FZ));
                }
                else
                {
                    Falloff = FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(-tC, 0.85f));
                }
                Falloff = FMath::Lerp(FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(FMath::Abs(tC), 0.6f)), Falloff, FMath::Max(0.40f, Isl.ShardT));
                if (Isl.ShardT < 0.3f)
                {
                    const float RF = FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(FMath::Abs(tC), FMath::Lerp(1.f, 0.6f, Isl.ShardT)));
                    Falloff = FMath::Lerp(RF, Falloff, FMath::Lerp(0.8f, 0.2f, Isl.ShardT));
                }

                Falloffs[j] = Falloff;
                if (Falloff >= 0.001f) bAnyValid = true;
            }

            if (!bAnyValid) continue; // Skip heavy noise if all falloffs are zero

            __m256 Falloff_v = _mm256_loadu_ps(Falloffs);

            // Domain Warping coordinates
            __m256 QX_v = WX_v;
            __m256 QY_v = WY_v;

            if (SC.bEnableDomainWarping)
            {
                const float WF = SC.DomainWarpFrequency;
                __m256 warpX = Noise3D_AVX2(_mm256_add_ps(_mm256_mul_ps(QX_v, _mm256_set1_ps(WF)), _mm256_set1_ps(10.f)), 
                                            _mm256_add_ps(_mm256_mul_ps(QY_v, _mm256_set1_ps(WF)), _mm256_set1_ps(20.f)), 
                                            _mm256_setzero_ps(), PermTable);
                __m256 warpY = Noise3D_AVX2(_mm256_add_ps(_mm256_mul_ps(QX_v, _mm256_set1_ps(WF)), _mm256_set1_ps(50.f)), 
                                            _mm256_add_ps(_mm256_mul_ps(QY_v, _mm256_set1_ps(WF)), _mm256_set1_ps(10.f)), 
                                            _mm256_setzero_ps(), PermTable);

                QX_v = _mm256_add_ps(QX_v, _mm256_mul_ps(warpX, _mm256_set1_ps(SC.DomainWarpStrength)));
                QY_v = _mm256_add_ps(QY_v, _mm256_mul_ps(warpY, _mm256_set1_ps(SC.DomainWarpStrength)));
            }

            // 3D Skyland Noise
            __m256 SD = _mm256_setzero_ps();
            const float ZFS = FMath::Lerp(0.50f, 0.05f, Isl.ShardT);

            if (Config.Performance.bEnable3DSkylandNoise || Isl.ShardT < 0.5f)
            {
                __m256 sn_x = _mm256_mul_ps(QX_v, _mm256_set1_ps(Isl.Freq * 0.6f));
                __m256 sn_y = _mm256_mul_ps(QY_v, _mm256_set1_ps(Isl.Freq * 0.6f));
                __m256 sn_z = _mm256_mul_ps(nZ, _mm256_set1_ps(Isl.Freq * ZFS));

                SD = _mm256_mul_ps(Noise3D_AVX2(sn_x, sn_y, sn_z, PermTable), _mm256_set1_ps(FMath::Lerp(0.55f, 0.25f, Isl.ShardT)));
            }

            const int32 Oct2D = FMath::Clamp(FMath::Min((int32)SC.ShapeOctaves, 4), 1, Config.Performance.MaxNoiseOctaves);
            __m256 SZ = (Isl.ShardT < 0.5f) ? _mm256_mul_ps(nZ, _mm256_set1_ps(Isl.Freq)) : _mm256_setzero_ps();

            __m256 shX = _mm256_mul_ps(QX_v, _mm256_set1_ps(Isl.Freq));
            __m256 shY = _mm256_mul_ps(QY_v, _mm256_set1_ps(Isl.Freq));

            // Shape FBM
            __m256 Shape = _mm256_add_ps(FBM_AVX2(shX, shY, SZ, Oct2D, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves, PermTable), SD);

            // Shape clamping clamping
            __m256 IsD = _mm256_mul_ps(_mm256_sub_ps(Shape, _mm256_set1_ps(Isl.Threshold)), _mm256_set1_ps(2.5f));
            IsD = _mm256_max_ps(_mm256_set1_ps(-1.5f), _mm256_min_ps(_mm256_set1_ps(1.5f), IsD));

            // Apply Falloff
            __m256 TargetD = _mm256_mul_ps(IsD, Falloff_v);

            // Update MaxD_v
            MaxD_v = _mm256_max_ps(MaxD_v, TargetD);
        }

        // Apply MaxD_v onto InOutDensities
        __m256 InD = _mm256_loadu_ps(&InOutDensities[i]);
        InD = _mm256_max_ps(InD, MaxD_v);
        _mm256_storeu_ps(&InOutDensities[i], InD);
    }

    // Remainder loop
    for (; i < Count; ++i)
    {
        const float z = StartZ + i * StepZ;
        float MaxD = -2.f;

        for (const FSkylandIslandData& Isl : Cache.Islands)
        {
            const float Margin = Isl.HalfThick * 0.4f;
            if (z < Isl.SkyAlt - Isl.HalfThick - Margin || z > Isl.SkyAlt + Isl.HalfThick + Margin) continue;

            const float tC = FMath::Clamp((z - Isl.SkyAlt) / (Isl.HalfThick + Margin + 1.f), -1.f, 1.f);
            float Falloff;
            if (tC >= 0.f)
            {
                const float FZ = 0.35f;
                Falloff = (tC < FZ) ? 1.f : FMath::SmoothStep(0.f, 1.f, 1.f - (tC - FZ) / (1.f - FZ));
            }
            else
            {
                Falloff = FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(-tC, 0.85f));
            }
            Falloff = FMath::Lerp(FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(FMath::Abs(tC), 0.6f)), Falloff, FMath::Max(0.40f, Isl.ShardT));
            if (Isl.ShardT < 0.3f)
            {
                const float RF = FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(FMath::Abs(tC), FMath::Lerp(1.f, 0.6f, Isl.ShardT)));
                Falloff = FMath::Lerp(RF, Falloff, FMath::Lerp(0.8f, 0.2f, Isl.ShardT));
            }

            if (Falloff < 0.001f) continue;

            const float nX = WX + SeedOffset.X;
            const float nY = WY + SeedOffset.Y;
            const float nZ = z + SeedOffset.Z;

            float QX = nX, QY = nY;
            if (SC.bEnableDomainWarping)
            {
                const float WF = SC.DomainWarpFrequency;
                QX += FVoxelBiomeGenerators::FastNoise3D(QX * WF + 10.f, QY * WF + 20.f, 0.f) * SC.DomainWarpStrength;
                QY += FVoxelBiomeGenerators::FastNoise3D(QX * WF + 50.f, QY * WF + 10.f, 0.f) * SC.DomainWarpStrength;
            }

            float SD = 0.f;
            const float ZFS = FMath::Lerp(0.50f, 0.05f, Isl.ShardT);
            if (Config.Performance.bEnable3DSkylandNoise || Isl.ShardT < 0.5f)
            {
                SD = FVoxelBiomeGenerators::FastNoise3D(QX * Isl.Freq * 0.6f, QY * Isl.Freq * 0.6f, nZ * Isl.Freq * ZFS) * FMath::Lerp(0.55f, 0.25f, Isl.ShardT);
            }

            const int32 Oct2D = FMath::Clamp(FMath::Min((int32)SC.ShapeOctaves, 4), 1, Config.Performance.MaxNoiseOctaves);
            const float SZ = (Isl.ShardT < 0.5f) ? nZ * Isl.Freq : 0.f;

            const float Shape = FVoxelBiomeGenerators::FBM(QX * Isl.Freq, QY * Isl.Freq, SZ, Oct2D, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves) + SD;
            float IsD = (Shape - Isl.Threshold) * 2.5f;
            IsD = FMath::Clamp(IsD, -1.5f, 1.5f);

            MaxD = FMath::Max(MaxD, IsD * Falloff);
        }

        InOutDensities[i] = FMath::Max(InOutDensities[i], MaxD);
    }
}
    void EvaluateColumn_BiomeWeights_AVX2(
        __m256 X_v, __m256 Y_v, 
        const FVoxelGenerationConfig& Config, 
        const int32* PermTable,
        FBiomeWeights_AVX2& OutWeights, 
        __m256& OutTemp, __m256& OutErosion)
    {
        const FVector Off = Config.GetSeedOffset();
        __m256 OffX = _mm256_set1_ps(Off.X);
        __m256 OffY = _mm256_set1_ps(Off.Y);

        __m256 TempFreq = _mm256_set1_ps(Config.BiomeBlend.TemperatureFrequency);
        __m256 tX = _mm256_mul_ps(_mm256_add_ps(X_v, OffX), TempFreq);
        __m256 tY = _mm256_mul_ps(_mm256_add_ps(Y_v, OffY), TempFreq);
        OutTemp = _mm256_add_ps(_mm256_mul_ps(Noise2D_AVX2(tX, tY, PermTable), _mm256_set1_ps(0.5f)), _mm256_set1_ps(0.5f));

        __m256 ErosFreq = _mm256_set1_ps(Config.BiomeBlend.ErosionFrequency);
        __m256 eX = _mm256_add_ps(_mm256_mul_ps(_mm256_add_ps(X_v, OffX), ErosFreq), _mm256_set1_ps(100.f));
        __m256 eY = _mm256_add_ps(_mm256_mul_ps(_mm256_add_ps(Y_v, OffY), ErosFreq), _mm256_set1_ps(100.f));
        OutErosion = _mm256_add_ps(_mm256_mul_ps(Noise2D_AVX2(eX, eY, PermTable), _mm256_set1_ps(0.5f)), _mm256_set1_ps(0.5f));

        __m256 One = _mm256_set1_ps(1.f);
        __m256 Zero = _mm256_setzero_ps();

        // 1. Forest
        OutWeights.Forest = _mm256_mul_ps(
            SmoothStep_AVX2(Zero, _mm256_set1_ps(0.6f), _mm256_sub_ps(One, OutErosion)),
            SmoothStep_AVX2(Zero, _mm256_set1_ps(1.0f), _mm256_sub_ps(_mm256_set1_ps(1.5f), OutTemp))
        );

        // 2. Desert
        OutWeights.Desert = _mm256_mul_ps(
            SmoothStep_AVX2(Zero, _mm256_set1_ps(0.6f), _mm256_sub_ps(One, OutErosion)),
            SmoothStep_AVX2(Zero, _mm256_set1_ps(0.25f), _mm256_sub_ps(OutTemp, _mm256_set1_ps(0.70f)))
        );

        // 3. Peaks
        __m256 PeaksS = _mm256_set1_ps(Config.BiomeBlend.PeaksStrength);
        OutWeights.Peaks = _mm256_mul_ps(
            _mm256_mul_ps(SmoothStep_AVX2(Zero, _mm256_set1_ps(0.2f), _mm256_sub_ps(OutErosion, _mm256_set1_ps(0.68f))), PeaksS),
            SmoothStep_AVX2(Zero, _mm256_set1_ps(0.8f), _mm256_sub_ps(_mm256_set1_ps(1.2f), OutTemp))
        );

        // 4. Cliffs
        __m256 CliffsS = _mm256_set1_ps(Config.BiomeBlend.CliffsStrength);
        __m256 CliffT = _mm256_sub_ps(_mm256_mul_ps(OutTemp, _mm256_set1_ps(1.3f)), _mm256_set1_ps(0.15f));
        OutWeights.Cliffs = _mm256_mul_ps(
            _mm256_mul_ps(SmoothStep_AVX2(Zero, _mm256_set1_ps(0.2f), _mm256_sub_ps(OutErosion, _mm256_set1_ps(0.50f))), CliffsS),
            SmoothStep_AVX2(Zero, _mm256_set1_ps(0.6f), CliffT)
        );

        // 5. Mesa
        __m256 MesaS = _mm256_set1_ps(Config.BiomeBlend.MesaStrength);
        __m256 ErosDiff = _mm256_sub_ps(OutErosion, _mm256_set1_ps(0.4f));
        __m256 AbsEros = _mm256_and_ps(ErosDiff, _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF)));
        OutWeights.Mesa = _mm256_mul_ps(
            _mm256_mul_ps(SmoothStep_AVX2(Zero, _mm256_set1_ps(0.2f), _mm256_sub_ps(OutTemp, _mm256_set1_ps(0.62f))), MesaS),
            SmoothStep_AVX2(Zero, _mm256_set1_ps(0.2f), _mm256_sub_ps(One, AbsEros))
        );

        // 6. Craters
        __m256 CraterN = Noise3D_AVX2(
            _mm256_mul_ps(_mm256_add_ps(X_v, OffX), _mm256_set1_ps(Config.Craters.Frequency*0.5f)),
            _mm256_mul_ps(_mm256_add_ps(Y_v, OffY), _mm256_set1_ps(Config.Craters.Frequency*0.5f)),
            _mm256_set1_ps(200.f), PermTable);
        OutWeights.Craters = SmoothStep_AVX2(
            _mm256_set1_ps(Config.Craters.ImpactThreshold + 0.1f), 
            _mm256_set1_ps(Config.Craters.ImpactThreshold), 
            CraterN);

        // 7. Ocean
        OutWeights.Ocean = Zero;
        if (Config.Performance.bEnableForest)
        {
            const FForestBiomeConfig& FC = Config.Forest;
            __m256 nXf = _mm256_mul_ps(_mm256_add_ps(X_v, OffX), _mm256_set1_ps(FC.NoiseFrequency));
            __m256 nYf = _mm256_mul_ps(_mm256_add_ps(Y_v, OffY), _mm256_set1_ps(FC.NoiseFrequency));
            __m256 BaseFH = FBM_AVX2(nXf, nYf, Zero, FC.Octaves, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves, PermTable);
            
            __m256 Range = _mm256_set1_ps(FC.HeightMax - FC.HeightMin);
            __m256 FH_v = _mm256_add_ps(_mm256_set1_ps(Config.SeaLevel), _mm256_mul_ps(Range, _mm256_add_ps(BaseFH, One)));
            
            __m256 dXf = _mm256_mul_ps(_mm256_add_ps(X_v, OffX), _mm256_set1_ps(FC.DetailFrequency));
            __m256 dYf = _mm256_mul_ps(_mm256_add_ps(Y_v, OffY), _mm256_set1_ps(FC.DetailFrequency));
            __m256 Detail = _mm256_mul_ps(Noise2D_AVX2(dXf, dYf, PermTable), _mm256_set1_ps(FC.DetailAmplitude));
            FH_v = _mm256_add_ps(FH_v, Detail);

            OutWeights.Ocean = SmoothStep_AVX2(
                _mm256_set1_ps(Config.SeaLevel - 200.f), 
                _mm256_set1_ps(Config.SeaLevel - 800.f), 
                FH_v);
        }

        OutWeights.Normalize();
    }

    void EvaluateColumn_SurfaceHeight_AVX2(
        __m256 X_v, __m256 Y_v, 
        const FBiomeWeights_AVX2& Weights, 
        const FVoxelGenerationConfig& Config, 
        const int32* PermTable,
        __m256 InTemp, __m256 InErosion,
        __m256& OutSurfH,
        float CenterH)
    {
        const FVector Off = Config.GetSeedOffset();
        __m256 OffX = _mm256_set1_ps(Off.X);
        __m256 OffY = _mm256_set1_ps(Off.Y);

        __m256 One = _mm256_set1_ps(1.f);
        __m256 Zero = _mm256_setzero_ps();
        __m256 SeaLevel = _mm256_set1_ps(Config.SeaLevel);

        OutSurfH = Zero;

        // 1. Forest
        {
            __m256 Mask = _mm256_cmp_ps(Weights.Forest, _mm256_set1_ps(0.001f), _CMP_GT_OQ);
            if (_mm256_movemask_ps(Mask))
            {
                const FForestBiomeConfig& FC = Config.Forest;
                __m256 nXf = _mm256_mul_ps(_mm256_add_ps(X_v, OffX), _mm256_set1_ps(FC.NoiseFrequency));
                __m256 nYf = _mm256_mul_ps(_mm256_add_ps(Y_v, OffY), _mm256_set1_ps(FC.NoiseFrequency));
                __m256 Base = FBM_AVX2(nXf, nYf, Zero, FC.Octaves, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves, PermTable);
                
                __m256 Range = _mm256_set1_ps(FC.HeightMax - FC.HeightMin);
                __m256 FH = _mm256_add_ps(SeaLevel, _mm256_mul_ps(Range, _mm256_mul_ps(_mm256_add_ps(Base, One), _mm256_set1_ps(0.5f))));
                
                __m256 dXf = _mm256_mul_ps(_mm256_add_ps(X_v, OffX), _mm256_set1_ps(FC.DetailFrequency));
                __m256 dYf = _mm256_mul_ps(_mm256_add_ps(Y_v, OffY), _mm256_set1_ps(FC.DetailFrequency));
                __m256 Detail = _mm256_mul_ps(Noise2D_AVX2(dXf, dYf, PermTable), _mm256_set1_ps(FC.DetailAmplitude));
                FH = _mm256_add_ps(FH, Detail);

                OutSurfH = _mm256_fmadd_ps(FH, Weights.Forest, OutSurfH);
            }
        }

        // 2. Desert
        {
            __m256 Mask = _mm256_cmp_ps(Weights.Desert, _mm256_set1_ps(0.001f), _CMP_GT_OQ);
            if (_mm256_movemask_ps(Mask))
            {
                const FDesertBiomeConfig& DC = Config.Desert;
                __m256 nXf = _mm256_mul_ps(_mm256_add_ps(X_v, OffX), _mm256_set1_ps(DC.NoiseFrequency));
                __m256 nYf = _mm256_mul_ps(_mm256_add_ps(Y_v, OffY), _mm256_set1_ps(DC.NoiseFrequency));
                __m256 Base = FBM_AVX2(nXf, nYf, _mm256_set1_ps(40.f), DC.Octaves, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves, PermTable);
                
                __m256 t = _mm256_max_ps(Zero, _mm256_mul_ps(_mm256_add_ps(Base, One), _mm256_set1_ps(0.5f)));
                __m256 Shaped = t; // Pow approximation layout safely.
                if (DC.Sharpness == 2.0f) Shaped = _mm256_mul_ps(t, t);

                __m256 Range = _mm256_set1_ps(DC.HeightMax - DC.HeightMin);
                __m256 DH = _mm256_add_ps(SeaLevel, _mm256_mul_ps(Range, Shaped));
                
                __m256 dXf = _mm256_mul_ps(_mm256_add_ps(X_v, OffX), _mm256_set1_ps(DC.RippleFrequency));
                __m256 dYf = _mm256_mul_ps(_mm256_add_ps(Y_v, OffY), _mm256_set1_ps(DC.RippleFrequency));
                __m256 Ripple = _mm256_mul_ps(Noise2D_AVX2(dXf, dYf, PermTable), _mm256_set1_ps(DC.RippleAmplitude));
                DH = _mm256_add_ps(DH, Ripple);

                OutSurfH = _mm256_fmadd_ps(DH, Weights.Desert, OutSurfH);
            }
        }

        // 3. Peaks
        {
            __m256 Mask = _mm256_cmp_ps(Weights.Peaks, _mm256_set1_ps(0.001f), _CMP_GT_OQ);
            if (_mm256_movemask_ps(Mask))
            {
                const FPeaksBiomeConfig& PC = Config.Peaks;
                __m256 nXf = _mm256_mul_ps(_mm256_add_ps(X_v, OffX), _mm256_set1_ps(PC.NoiseFrequency));
                __m256 nYf = _mm256_mul_ps(_mm256_add_ps(Y_v, OffY), _mm256_set1_ps(PC.NoiseFrequency));
                __m256 Base = FBM_AVX2(nXf, nYf, _mm256_set1_ps(10.f), PC.Octaves, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves, PermTable);
                
                __m256 t = _mm256_max_ps(Zero, _mm256_mul_ps(_mm256_add_ps(Base, One), _mm256_set1_ps(0.5f)));
                __m256 Shaped = _mm256_mul_ps(_mm256_mul_ps(t, t), _mm256_sqrt_ps(t)); // t^2.5 layout safely securely.

                __m256 Range = _mm256_set1_ps(PC.HeightMax - PC.HeightMin);
                __m256 PH = _mm256_add_ps(SeaLevel, _mm256_mul_ps(Range, Shaped));
                
                __m256 dXf = _mm256_mul_ps(_mm256_add_ps(X_v, OffX), _mm256_set1_ps(PC.NoiseFrequency*4.f));
                __m256 dYf = _mm256_mul_ps(_mm256_add_ps(Y_v, OffY), _mm256_set1_ps(PC.NoiseFrequency*4.f));
                __m256 Detail = _mm256_mul_ps(Noise2D_AVX2(dXf, dYf, PermTable), _mm256_set1_ps(PC.DetailAmplitude));
                PH = _mm256_add_ps(PH, Detail);

                OutSurfH = _mm256_fmadd_ps(PH, Weights.Peaks, OutSurfH);
            }
        }

        // 4. Cliffs
        {
            __m256 Mask = _mm256_cmp_ps(Weights.Cliffs, _mm256_set1_ps(0.001f), _CMP_GT_OQ);
            if (_mm256_movemask_ps(Mask))
            {
                const FCliffsBiomeConfig& CC = Config.Cliffs;
                __m256 nXf = _mm256_mul_ps(_mm256_add_ps(X_v, OffX), _mm256_set1_ps(CC.NoiseFrequency));
                __m256 nYf = _mm256_mul_ps(_mm256_add_ps(Y_v, OffY), _mm256_set1_ps(CC.NoiseFrequency));
                __m256 Base = FBM_AVX2(nXf, nYf, _mm256_set1_ps(15.f), CC.Octaves, 2.1f, 0.55f, Config.Performance.MaxNoiseOctaves, PermTable);
                
                __m256 AbsBase = _mm256_and_ps(Base, _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF)));
                __m256 Shaped = AbsBase;
                if (CC.Sharpness == 2.0f) Shaped = _mm256_mul_ps(AbsBase, AbsBase);
                Shaped = _mm256_max_ps(_mm256_min_ps(Shaped, One), Zero);

                if (CC.TerraceSteps > 0 && CC.TerraceFactor > 0.001f)
                {
                    __m256 S_v = _mm256_set1_ps((float)CC.TerraceSteps);
                    __m256 Fl = _mm256_div_ps(_mm256_floor_ps(_mm256_mul_ps(Shaped, S_v)), S_v);
                    __m256 TF = _mm256_set1_ps(CC.TerraceFactor);
                    Shaped = _mm256_add_ps(_mm256_mul_ps(Shaped, _mm256_sub_ps(One, TF)), _mm256_mul_ps(Fl, TF));
                }

                __m256 Range = _mm256_set1_ps(CC.HeightMax - CC.HeightMin);
                __m256 CH = _mm256_add_ps(_mm256_add_ps(SeaLevel, _mm256_set1_ps(CC.HeightMin)), _mm256_mul_ps(Range, Shaped));
                
                __m256 dXf = _mm256_mul_ps(_mm256_add_ps(X_v, OffX), _mm256_set1_ps(CC.NoiseFrequency * 6.f));
                __m256 dYf = _mm256_mul_ps(_mm256_add_ps(Y_v, OffY), _mm256_set1_ps(CC.NoiseFrequency * 6.f));
                __m256 Detail = _mm256_mul_ps(Noise2D_AVX2(dXf, dYf, PermTable), _mm256_set1_ps(CC.DetailAmplitude));
                CH = _mm256_add_ps(CH, Detail);

                OutSurfH = _mm256_fmadd_ps(CH, Weights.Cliffs, OutSurfH);
            }
        }

        // 5. Mesa
        {
            __m256 Mask = _mm256_cmp_ps(Weights.Mesa, _mm256_set1_ps(0.001f), _CMP_GT_OQ);
            if (_mm256_movemask_ps(Mask))
            {
                const FMesaBiomeConfig& MC = Config.Mesa;
                __m256 BasePlains = _mm256_add_ps(SeaLevel, _mm256_set1_ps(MC.HeightBase));
                __m256 Height = BasePlains;
 
                __m256 MesaN = FBM_AVX2(_mm256_mul_ps(_mm256_add_ps(X_v, OffX), _mm256_set1_ps(MC.MesaFrequency)), 
                                        _mm256_mul_ps(_mm256_add_ps(Y_v, OffY), _mm256_set1_ps(MC.MesaFrequency)), 
                                        _mm256_set1_ps(20.f), 4, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves, PermTable);
                                        
                __m256 ButteN = FBM_AVX2(_mm256_mul_ps(_mm256_add_ps(X_v, OffX), _mm256_set1_ps(MC.ButteFrequency)), 
                                         _mm256_mul_ps(_mm256_add_ps(Y_v, OffY), _mm256_set1_ps(MC.ButteFrequency)), 
                                         _mm256_set1_ps(40.f), 3, 2.f, 0.5f, Config.Performance.MaxNoiseOctaves, PermTable);
 
                __m256 Comb = _mm256_max_ps(
                    _mm256_mul_ps(_mm256_add_ps(MesaN, One), _mm256_set1_ps(0.5f)),
                    _mm256_mul_ps(_mm256_add_ps(ButteN, One), _mm256_set1_ps(0.5f))
                );
 
                __m256 StepS = _mm256_set1_ps((float)MC.PlateauSteps);
                __m256 Plateau = _mm256_div_ps(_mm256_floor_ps(_mm256_mul_ps(Comb, StepS)), StepS);
                
                __m256 Delta = _mm256_mul_ps(_mm256_sub_ps(Comb, Plateau), _mm256_set1_ps(MC.EdgeSharpness + 4.f));
                __m256 EdgeBlend = SmoothStep_AVX2(Zero, One, Delta);
 
                __m256 EdgeSq = _mm256_mul_ps(EdgeBlend, EdgeBlend);
                __m256 Term = _mm256_add_ps(Plateau, _mm256_div_ps(EdgeSq, StepS));
                Height = _mm256_add_ps(Height, _mm256_mul_ps(Term, _mm256_set1_ps(MC.HeightMax - MC.HeightBase)));
 
                // Channel
                __m256 ChanN = Noise2D_AVX2(_mm256_mul_ps(_mm256_add_ps(X_v, OffX), _mm256_set1_ps(MC.ChannelFrequency)), 
                                            _mm256_mul_ps(_mm256_add_ps(Y_v, OffY), _mm256_set1_ps(MC.ChannelFrequency)), PermTable);
                __m256 AbsChan = _mm256_and_ps(ChanN, _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF)));
                __m256 RidgedCh = _mm256_sub_ps(One, AbsChan);
                __m256 C_Mask = _mm256_cmp_ps(RidgedCh, _mm256_set1_ps(0.75f), _CMP_GT_OQ);
                if (_mm256_movemask_ps(C_Mask))
                {
                     __m256 C_Delta = _mm256_div_ps(_mm256_sub_ps(RidgedCh, _mm256_set1_ps(0.75f)), _mm256_set1_ps(0.25f));
                     __m256 C_Smooth = SmoothStep_AVX2(Zero, One, C_Delta);
                     Height = _mm256_blendv_ps(Height, _mm256_sub_ps(Height, _mm256_mul_ps(C_Smooth, _mm256_set1_ps(MC.ChannelDepth))), C_Mask);
                }
 
                // Layers (Sin approx)
                __m256 MC_Thick = _mm256_set1_ps(MC.LayerThickness);
                __m256 Div = _mm256_div_ps(Height, MC_Thick);
                __m256 Frac = _mm256_sub_ps(Div, _mm256_floor_ps(Div));
                __m256 L_Mask = _mm256_cmp_ps(Frac, _mm256_set1_ps(MC.LayerHardness), _CMP_GT_OQ);
                __m256 MC_Hard = _mm256_set1_ps(MC.LayerHardness);
 
                __m256 Sin_X = _mm256_mul_ps(_mm256_div_ps(_mm256_sub_ps(Frac, MC_Hard), _mm256_sub_ps(One, MC_Hard)), _mm256_set1_ps(3.14159265f));
                __m256 PiMinusX = _mm256_sub_ps(_mm256_set1_ps(3.14159265f), Sin_X);
                __m256 X_Pi_X = _mm256_mul_ps(Sin_X, PiMinusX);
                __m256 Numerator = _mm256_mul_ps(_mm256_set1_ps(16.f), X_Pi_X);
                __m256 Denominator = _mm256_sub_ps(_mm256_set1_ps(5.f * 3.14159265f * 3.14159265f), _mm256_mul_ps(_mm256_set1_ps(4.f), X_Pi_X));
                __m256 Sin_Val = _mm256_div_ps(Numerator, Denominator);
 
                __m256 If_True = _mm256_mul_ps(Sin_Val, _mm256_set1_ps(200.f * MC.LayerVariation));
                __m256 If_False = _mm256_set1_ps(100.f * MC.LayerVariation);
                Height = _mm256_add_ps(Height, _mm256_blendv_ps(If_False, If_True, L_Mask));
 
                // Talus
                __m256 Tal_Mask = _mm256_and_ps(
                    _mm256_cmp_ps(Height, _mm256_add_ps(BasePlains, _mm256_set1_ps(2000.f)), _CMP_GT_OQ),
                    _mm256_cmp_ps(Comb, _mm256_set1_ps(0.3f), _CMP_LT_OQ)
                );
                if (_mm256_movemask_ps(Tal_Mask))
                {
                     __m256 TalN = Noise2D_AVX2(_mm256_mul_ps(_mm256_add_ps(X_v, OffX), _mm256_set1_ps(MC.TalusFrequency)), 
                                                _mm256_mul_ps(_mm256_add_ps(Y_v, OffY), _mm256_set1_ps(MC.TalusFrequency)), PermTable);
                     __m256 Talus = _mm256_mul_ps(_mm256_add_ps(TalN, One), _mm256_set1_ps(400.f * MC.TalusSpread));
                     Height = _mm256_blendv_ps(Height, _mm256_add_ps(Height, Talus), Tal_Mask);
                }
 
                __m256 FinalN = Noise2D_AVX2(_mm256_mul_ps(_mm256_add_ps(X_v, OffX), _mm256_set1_ps(0.008f)), _mm256_mul_ps(_mm256_add_ps(Y_v, OffY), _mm256_set1_ps(0.008f)), PermTable);
                Height = _mm256_add_ps(Height, _mm256_mul_ps(FinalN, _mm256_set1_ps(160.f)));
 
                OutSurfH = _mm256_fmadd_ps(Height, Weights.Mesa, OutSurfH);
            }
        }

        // 6. Craters Post-Process Overlay
        {
            __m256 Mask = _mm256_cmp_ps(Weights.Craters, _mm256_set1_ps(0.001f), _CMP_GT_OQ);
            if (_mm256_movemask_ps(Mask))
            {
                const FCraterBiomeConfig& CRC = Config.Craters;

                __m256 dX = _mm256_sub_ps(_mm256_add_ps(X_v, OffX), _mm256_set1_ps(CRC.ForcedCraterCenter.X));
                __m256 dY = _mm256_sub_ps(_mm256_add_ps(Y_v, OffY), _mm256_set1_ps(CRC.ForcedCraterCenter.Y));
                __m256 Dist = _mm256_sqrt_ps(_mm256_fmadd_ps(dX, dX, _mm256_mul_ps(dY, dY)));
                
                __m256 Radius = _mm256_set1_ps(CRC.CentralCraterRadius * 0.5f);
                __m256 NormDist = _mm256_div_ps(Dist, Radius);

                __m256 CenterH_v = _mm256_set1_ps(CenterH);
                
                // Continuous Spline bounds
                __m256 R0 = _mm256_set1_ps(0.74f); // Floor End
                __m256 R1 = _mm256_set1_ps(0.93f); // Rim Crest
                __m256 R2 = _mm256_set1_ps(1.05f); // Rim Dropoff End

                // Floor flattening blend
                __m256 T_Floor = _mm256_div_ps(_mm256_sub_ps(NormDist, R0), _mm256_sub_ps(R2, R0));
                T_Floor = _mm256_max_ps(Zero, _mm256_min_ps(One, T_Floor));
                __m256 Smooth_Floor = SmoothStep_AVX2(Zero, One, T_Floor);
                __m256 C_Mask_Floor = _mm256_cmp_ps(NormDist, R0, _CMP_LT_OQ);
                __m256 FlatBlend = _mm256_blendv_ps(Smooth_Floor, Zero, C_Mask_Floor);

                __m256 BasePlains = Lerp_AVX2(FlatBlend, CenterH_v, OutSurfH);

                __m256 Depth = _mm256_set1_ps(CRC.CentralCraterDepth * 1.25f);
                __m256 RimH = _mm256_set1_ps(CRC.CentralCraterRimHeight * 1.8f);

                // 1. Inner Wall
                __m256 T_Wall = _mm256_div_ps(_mm256_sub_ps(NormDist, R0), _mm256_sub_ps(R1, R0));
                T_Wall = _mm256_max_ps(Zero, _mm256_min_ps(One, T_Wall));
                __m256 WallH = _mm256_add_ps(_mm256_mul_ps(SmoothStep_AVX2(Zero, One, T_Wall), _mm256_sub_ps(RimH, Depth)), Depth);
                WallH = _mm256_add_ps(BasePlains, WallH);

                // 2. Outer Dropoff
                __m256 T_Drop = _mm256_div_ps(_mm256_sub_ps(NormDist, R1), _mm256_sub_ps(R2, R1));
                T_Drop = _mm256_max_ps(Zero, _mm256_min_ps(One, T_Drop));
                __m256 DropH = Lerp_AVX2(SmoothStep_AVX2(Zero, One, T_Drop), _mm256_add_ps(BasePlains, RimH), BasePlains);

                // 3. Combine with blends
                __m256 C_Mask_Crest = _mm256_cmp_ps(NormDist, R1, _CMP_LT_OQ);
                __m256 CraterH = _mm256_blendv_ps(DropH, WallH, C_Mask_Crest);

                // 4. Domimance Fade
                __m256 T_Fade = _mm256_div_ps(_mm256_sub_ps(NormDist, _mm256_set1_ps(0.97f)), _mm256_set1_ps(1.80f - 0.97f));
                T_Fade = _mm256_max_ps(Zero, _mm256_min_ps(One, T_Fade));
                __m256 Dominance = _mm256_sub_ps(One, SmoothStep_AVX2(Zero, One, T_Fade));

                OutSurfH = Lerp_AVX2(Dominance, OutSurfH, CraterH);
            }
        }
    }
} // namespace FVoxelNoiseSIMD
