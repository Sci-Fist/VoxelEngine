// Copyright Epic Games, Inc. All Rights Reserved.

#include "FirstVoxel.h"
#include "Modules/ModuleManager.h"
#include "Voxel/VoxelLogger.h"

DEFINE_LOG_CATEGORY(LogFirstVoxel);

class FFirstVoxelGameModule : public FDefaultGameModuleImpl
{
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();
		UVoxelLogger::InitLogger();
		UE_LOG(LogFirstVoxel, Log, TEXT("FirstVoxel module started; log file initialised."));
		UVoxelLogger::LogVoxelEvent(TEXT("FirstVoxel module started."));
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FFirstVoxelGameModule, FirstVoxel, "FirstVoxel");