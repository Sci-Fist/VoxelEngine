// VoxelLogger.cpp
#include "VoxelLogger.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

FString       UVoxelLogger::LogFilePath = TEXT("");
IFileHandle*  UVoxelLogger::FileHandle  = nullptr;

void UVoxelLogger::InitLogger()
{
	// Close any handle left open from a previous PIE session so it doesn't
	// keep pointing at a stale file after the path changes below.
	if (FileHandle)
	{
		delete FileHandle;
		FileHandle = nullptr;
	}

	const FString LogDir   = FPaths::ProjectLogDir();
	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S"));
	LogFilePath = LogDir / FString::Printf(TEXT("FirstVoxel_%s.log"), *Timestamp);

	// Write a session header so the start of each PIE run is easy to find in the log.
	const FString Header = FString::Printf(
		TEXT("=== VOXEL PLAYTEST SESSION [%s] ===\n"), *FDateTime::Now().ToString());
	FFileHelper::SaveStringToFile(
		Header, *LogFilePath,
		FFileHelper::EEncodingOptions::AutoDetect,
		&IFileManager::Get(), FILEWRITE_EvenIfReadOnly);

	// Open the append handle now so LogVoxelEvent never has to re-open it mid-session.
	FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*LogFilePath, /*bAppend=*/true);
}

void UVoxelLogger::LogVoxelEvent(FString Message)
{
	// Lazily initialise on first use (e.g. when called before BeginPlay).
	if (LogFilePath.IsEmpty() || !FileHandle)
		InitLogger();

	if (!FileHandle) return; // filesystem unavailable (e.g. packaged with no write access)

	const FString Timestamped = FString::Printf(
		TEXT("[%s] %s\n"), *FDateTime::Now().GetTimeOfDay().ToString(), *Message);

	const FTCHARToUTF8 Converter(*Timestamped);
	FileHandle->Write(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
	FileHandle->Flush();
}
