// VoxelNoiseSIMD_Tester.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelNoiseSIMD_Tester.generated.h"

UCLASS()
class FIRSTVOXEL_API AVoxelNoiseTester : public AActor
{
    GENERATED_BODY()

public:
    AVoxelNoiseTester();

    UPROPERTY(EditAnywhere, Category = "Voxel|Benchmark")
    int32 Iterations = 1000000;

    UPROPERTY(EditAnywhere, Category = "Voxel|Benchmark")
    float Frequency = 0.01f;

    UFUNCTION(CallInEditor, Category = "Voxel|Benchmark")
    void RunBenchmark();

protected:
    // Pure AVX2 implementation helper
    void RunAVX2Test(int32 TotalPoints);
    void RunScalarTest(int32 TotalPoints);
};
