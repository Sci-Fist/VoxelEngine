// =============================================================================
// VoxelWaterSimTask.h
// =============================================================================
#pragma once

#include "CoreMinimal.h"
#include "Async/AsyncWork.h"
#include "Voxel/Water/VoxelWaterSimulator.h"

/**
 * Background task that executes one step of the water simulation.
 * Does not block the Game Thread.
 */
class FWaterSimAsyncTask : public FNonAbandonableTask
{
public:
    FWaterSimAsyncTask(FVoxelWaterSimulator* InSimulator)
        : Simulator(InSimulator)
    {}

    /** Required by FAsyncTask */
    void DoWork()
    {
        if (Simulator)
        {
            // Step() is thread-safe via its internal FCriticalSection (MapLock).
            // On Windows FCriticalSection wraps CRITICAL_SECTION which is re-entrant,
            // so nested acquisition inside SimCell helpers does not deadlock.
            Simulator->Step();
        }
    }

    FORCEINLINE TStatId GetStatId() const
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(FWaterSimAsyncTask, STATGROUP_ThreadPoolAsyncTasks);
    }

private:
    FVoxelWaterSimulator* Simulator;
};
