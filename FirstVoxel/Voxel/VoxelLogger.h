// VoxelLogger.h
#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HAL/FileManager.h"  // IFileHandle
#include "VoxelLogger.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVoxelWorld, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogVoxelChunk, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogVoxelBiome, Log, All);

UCLASS()
class FIRSTVOXEL_API UVoxelLogger : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Log a message to Source/Log/FirstVoxel_<date>_<time>.log */
	UFUNCTION(BlueprintCallable, Category = "Voxel|Logging")
	static void LogVoxelEvent(FString Message);

	/** Initialize the log file for the current session. */
	static void InitLogger();

private:
	/** Critical section for thread-safe logging from background tasks. */
	static FCriticalSection LogLock;

	/** Absolute path of the current session's log file. */
	static FString LogFilePath;

	/**
	 * Persistent write handle for the log file.
	 * Opened once in InitLogger() and kept alive for the whole session to avoid
	 * per-event open/close overhead. Closed and re-opened by the next InitLogger()
	 * call so each PIE session gets its own fresh file.
	 */
	static IFileHandle* FileHandle;
	static bool bLogInitFailed;
};
