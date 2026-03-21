// VoxelLogger.cpp
// Thread-safe session log for the voxel engine.
//
// FIX #1  — Deadlock: LogVoxelEvent held LogLock, then called InitLogger()
//           which tried to acquire the same non-recursive FCriticalSection.
//           Fix: split into lock-free InitLoggerInternal() (called from
//           within the lock) and the public InitLogger() that takes the lock.
//
// FIX #15 — No flush after write: crash during a session lost the last
//           log entries. Fix: flush after every write.

#include "VoxelLogger.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

FString          UVoxelLogger::LogFilePath   = TEXT("");
IFileHandle*     UVoxelLogger::FileHandle    = nullptr;
FCriticalSection UVoxelLogger::LogLock;
bool             UVoxelLogger::bLogInitFailed = false;

// ── Private lock-free init ────────────────────────────────────────────────
// Called from inside the lock only. Never acquires LogLock.
void UVoxelLogger::InitLoggerInternal()
{
    if (UVoxelLogger::FileHandle)
    {
        delete UVoxelLogger::FileHandle;
        UVoxelLogger::FileHandle = nullptr;
    }

    const FString LogDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("Log"));
    IPlatformFile& PF    = FPlatformFileManager::Get().GetPlatformFile();
    if (!PF.DirectoryExists(*LogDir))
        PF.CreateDirectoryTree(*LogDir);

    const FDateTime Now  = FDateTime::Now();
    const FString   Stamp = Now.ToString(TEXT("%Y-%m-%d_%H-%M-%S"));
    UVoxelLogger::LogFilePath = FPaths::Combine(LogDir,
        FString::Printf(TEXT("FirstVoxel_%s.log"), *Stamp));

    const FString Header = FString::Printf(
        TEXT("=== FIRSTVOXEL SESSION [%s] ===\n"), *Now.ToString());
    FFileHelper::SaveStringToFile(Header, *UVoxelLogger::LogFilePath,
        FFileHelper::EEncodingOptions::AutoDetect,
        &IFileManager::Get(), FILEWRITE_EvenIfReadOnly);

    UVoxelLogger::FileHandle = PF.OpenWrite(*UVoxelLogger::LogFilePath, /*bAppend=*/true);
    if (!UVoxelLogger::FileHandle)
        UVoxelLogger::bLogInitFailed = true;
}

// ── Public interface ──────────────────────────────────────────────────────
void UVoxelLogger::InitLogger()
{
    if (bLogInitFailed) return;
    FScopeLock Lock(&LogLock);
    // Reset failure flag so a new session can try again
    bLogInitFailed = false;
    InitLoggerInternal();
}

void UVoxelLogger::LogVoxelEvent(FString Message)
{
    if (bLogInitFailed) return;

    FScopeLock Lock(&LogLock);

    // Lazy init — called inside the lock so InitLoggerInternal is safe
    if (LogFilePath.IsEmpty() || !FileHandle)
        InitLoggerInternal();

    if (!FileHandle) return;

    const FString Entry = FString::Printf(
        TEXT("[%s] %s\n"), *FDateTime::Now().GetTimeOfDay().ToString(), *Message);
    const FTCHARToUTF8 Conv(*Entry);
    FileHandle->Write(reinterpret_cast<const uint8*>(Conv.Get()), Conv.Length());
    FileHandle->Flush(false); // FIX #15: flush so crash doesn't lose entries
}
