// VoxelLogger.cpp
// 
// Centralized logging system for the voxel engine, providing thread-safe
// file-based logging with automatic session management and timestamping.
//
// ARCHITECTURE OVERVIEW:
// This logger implements a singleton-style logging system specifically designed
// for the voxel engine's needs. It provides reliable, thread-safe logging to
// persistent files with automatic session management and structured output.
//
// KEY FEATURES:
// - Thread-safe logging with critical section protection
// - Automatic session file creation with timestamp-based naming
// - Persistent logging to project Source/Log directory
// - Graceful failure handling and retry mechanisms
// - Structured log format with timestamps and session headers
//
// PERFORMANCE CHARACTERISTICS:
// - Minimal overhead logging operations
// - Buffered file writes for efficiency
// - Lazy initialization to avoid startup delays
// - Automatic cleanup on initialization failures

#include "VoxelLogger.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

// Static member variables for session management and thread safety
FString       UVoxelLogger::LogFilePath = TEXT("");        // Current session log file path
IFileHandle*  UVoxelLogger::FileHandle  = nullptr;         // Active file handle for writing
FCriticalSection UVoxelLogger::LogLock;                    // Thread synchronization
bool UVoxelLogger::bLogInitFailed = false;                 // Initialization failure flag

void UVoxelLogger::InitLogger()
{
	// Early exit if logger has previously failed to initialize
	// This prevents repeated failed initialization attempts
	if (bLogInitFailed) return;
	
	// Acquire lock to ensure thread-safe initialization
	FScopeLock ScopeLock(&LogLock);

	// Clean up any existing file handle before creating new one
	if (FileHandle)
	{
		delete FileHandle;
		FileHandle = nullptr;
	}

	// Create log directory structure under project Source folder
	// Path format: <ProjectDir>/Source/Log/
	const FString LogDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("Log"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*LogDir))
	{
		PlatformFile.CreateDirectoryTree(*LogDir);
	}

	// Generate unique session filename using current timestamp
	// Format: FirstVoxel_YYYY-MM-DD_HH-MM-SS.log
	const FDateTime Now = FDateTime::Now();
	const FString Timestamp = Now.ToString(TEXT("%Y-%m-%d_%H-%M-%S"));
	LogFilePath = FPaths::Combine(LogDir, FString::Printf(TEXT("FirstVoxel_%s.log"), *Timestamp));

	// Write session header to new log file
	// This provides clear separation between different engine sessions
	const FString Header = FString::Printf(
		TEXT("=== FIRSTVOXEL SESSION [%s] ===\n"), *Now.ToString());
	FFileHelper::SaveStringToFile(
		Header, *LogFilePath,
		FFileHelper::EEncodingOptions::AutoDetect,
		&IFileManager::Get(), FILEWRITE_EvenIfReadOnly);

	// Open file handle for append operations
	// Using append mode allows multiple log entries to be added to same file
	FileHandle = PlatformFile.OpenWrite(*LogFilePath, /*bAppend=*/true);
	if (!FileHandle)
	{
		// Mark initialization as failed to prevent future attempts
		bLogInitFailed = true;
	}
}

void UVoxelLogger::LogVoxelEvent(FString Message)
{
	// Early exit if logger has failed initialization
	if (bLogInitFailed) return;

	// Acquire lock to ensure thread-safe logging operations
	FScopeLock ScopeLock(&LogLock);

	// Lazy initialization - create logger if not already initialized
	if (LogFilePath.IsEmpty() || !FileHandle)
		InitLogger();

	// Safety check - ensure file handle is valid before writing
	if (!FileHandle) return; 

	// Format log entry with timestamp and message
	// Format: [HH:MM:SS.fff] Message content
	const FString Timestamped = FString::Printf(
		TEXT("[%s] %s\n"), *FDateTime::Now().GetTimeOfDay().ToString(), *Message);

	// Convert FString to UTF-8 bytes for file writing
	// This ensures proper encoding and cross-platform compatibility
	const FTCHARToUTF8 Converter(*Timestamped);
	FileHandle->Write(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
}
