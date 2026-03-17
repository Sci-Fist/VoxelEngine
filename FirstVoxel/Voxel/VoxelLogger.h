// =============================================================================
// VoxelLogger.h
// =============================================================================
//
// Thread-safe session log writer for the voxel engine.
// Writes timestamped events to a per-session file in Source/Log/ so that
// detailed generation diagnostics are available without opening the UE
// output log or attaching a debugger.
//
// -- LOG FILE LOCATION --------------------------------------------------------
//
//   Source/Log/FirstVoxel_YYYYMMDD_HHMMSS.log
//   A new file is created per PIE session (InitLogger is called in the
//   module startup). Each session therefore has an isolated history.
//
// -- THREAD SAFETY ------------------------------------------------------------
//
//   LogVoxelEvent() acquires LogLock (FCriticalSection) before writing.
//   Safe to call from background generation threads (FVoxelGeneratorTask).
//
// -- USAGE --------------------------------------------------------------------
//
//   // From any thread:
//   UVoxelLogger::LogVoxelEvent(TEXT("MyEvent: some detail"));
//
//   // From Blueprint:
//   // Call the BlueprintCallable LogVoxelEvent node.
//
// -- LOG CATEGORIES (UE output log) -------------------------------------------
//
//   LogVoxelWorld   AVoxelWorld, streaming, generation, spawn
//   LogVoxelChunk   AVoxelChunk, mesh upload, LOD transitions
//   LogVoxelBiome   FVoxelBiomeManager, weight calculation
//
//   Use UE_LOG(LogVoxelWorld, Verbose, ...) for high-frequency events that
//   should not appear in normal runs. Session log always receives all events
//   regardless of log verbosity settings.
// =============================================================================
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
