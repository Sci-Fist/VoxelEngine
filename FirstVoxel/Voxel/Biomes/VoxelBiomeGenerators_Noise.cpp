// VoxelBiomeGenerators_Noise.cpp
// Noise utilities: FBM
// This is the smallest split file — just the shared FBM helper.
#include "VoxelBiomeGenerators.h"

float FVoxelBiomeGenerators::FBM(float X, float Y, float Z,
                                  int32 Octaves, float Lacunarity, float Gain,
                                  int32 MaxOctaves)
{
    const int32 N = FMath::Clamp(FMath::Min(Octaves, MaxOctaves), 1, 16);
    float V = 0.f, A = 0.5f, F = 1.f;
    for (int32 i = 0; i < N; ++i)
    {
        V += FastNoise3D(X*F, Y*F, Z*F) * A;
        F *= Lacunarity; A *= Gain;
    }
    return V;
}
