// VoxelLogger.cpp
#include "VoxelLogger.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

FString       UVoxelLogger::LogFilePath = TEXT("");
IFileHandle*  UVoxelLogger::FileHandle  = nullptr;
FCriticalSection UVoxelLogger::LogLock;
bool UVoxelLogger::bLogInitFailed = false;

void UVoxelLogger::InitLogger()
{
	if (bLogInitFailed) return;
	FScopeLock ScopeLock(&LogLock);

	if (FileHandle)
	{
		delete FileHandle;
		FileHandle = nullptr;
	}

	// Log folder under project Source: <ProjectDir>/Source/Log/
	const FString LogDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("Log"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*LogDir))
	{
		PlatformFile.CreateDirectoryTree(*LogDir);
	}

	// Filename: date and time so each session gets a unique file
	const FDateTime Now = FDateTime::Now();
	const FString Timestamp = Now.ToString(TEXT("%Y-%m-%d_%H-%M-%S"));
	LogFilePath = FPaths::Combine(LogDir, FString::Printf(TEXT("FirstVoxel_%s.log"), *Timestamp));

	const FString Header = FString::Printf(
		TEXT("=== FIRSTVOXEL SESSION [%s] ===\n"), *Now.ToString());
	FFileHelper::SaveStringToFile(
		Header, *LogFilePath,
		FFileHelper::EEncodingOptions::AutoDetect,
		&IFileManager::Get(), FILEWRITE_EvenIfReadOnly);

	FileHandle = PlatformFile.OpenWrite(*LogFilePath, /*bAppend=*/true);
	if (!FileHandle)
	{
		bLogInitFailed = true;
	}
}

void UVoxelLogger::LogVoxelEvent(FString Message)
{
	if (bLogInitFailed) return;

	FScopeLock ScopeLock(&LogLock);

	if (LogFilePath.IsEmpty() || !FileHandle)
		InitLogger();

	if (!FileHandle) return; 

	const FString Timestamped = FString::Printf(
		TEXT("[%s] %s\n"), *FDateTime::Now().GetTimeOfDay().ToString(), *Message);

	const FTCHARToUTF8 Converter(*Timestamped);
	FileHandle->Write(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
}
