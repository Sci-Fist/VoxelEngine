// =============================================================================
// FirstVoxel.cpp
// =============================================================================
//
// Module entry point for the FirstVoxel procedural voxel engine.
// This file implements the module lifecycle and initializes core systems.
//
// ARCHITECTURE OVERVIEW:
// - Module Lifecycle: StartupModule() initializes the voxel logging system
// - Session Logging: Creates timestamped log files for debugging generation
// - Engine Integration: Registers the module with Unreal Engine's module system
//
// DEPENDENCIES:
// - VoxelLogger: Session logging system for generation diagnostics
// - Core Unreal Engine modules for module management
//
// INITIALIZATION SEQUENCE:
// 1. FDefaultGameModuleImpl::StartupModule() - Base module initialization
// 2. UVoxelLogger::InitLogger() - Creates session log file
// 3. UE_LOG output - Confirms successful module startup
// 4. UVoxelLogger::LogVoxelEvent() - Records startup in session log
//
// LOGGING SYSTEM:
// - Creates log files in Source/Log/FirstVoxel_YYYYMMDD_HHMMSS.log
// - Thread-safe logging from background generation threads
// - Separate log categories for different voxel subsystems
// =============================================================================

// Copyright Epic Games, Inc. All Rights Reserved.

#include "FirstVoxel.h"
#include "Modules/ModuleManager.h"
#include "Voxel/VoxelLogger.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "Voxel/VoxelLogger.h"

// Define the main log category for the FirstVoxel module
// This is used throughout the codebase for UE_LOG output
DEFINE_LOG_CATEGORY(LogFirstVoxel);

/**
 * @class FFirstVoxelGameModule
 * @brief Module implementation for the FirstVoxel procedural voxel engine
 * 
 * This class handles the module lifecycle including startup and shutdown.
 * It ensures proper initialization of core voxel systems before gameplay begins.
 */
class FFirstVoxelGameModule : public FDefaultGameModuleImpl
{
public:
	/**
	 * Called when the module is loaded and should perform its initialization.
	 * This is the entry point for all voxel engine initialization.
	 */
	virtual void StartupModule() override
	{
		// Call parent implementation to ensure base module functionality
		FDefaultGameModuleImpl::StartupModule();
		
		// Initialize the voxel session logging system
		// This creates a timestamped log file for debugging generation issues
		UVoxelLogger::InitLogger();

		// Map the shader directory so the engine can find our custom USF files
		FString ShaderDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/FirstVoxel/Voxel/Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Voxel"), ShaderDir);
		
		// Log successful module startup to the UE output log
		UE_LOG(LogFirstVoxel, Log, TEXT("FirstVoxel module started; log file initialised."));
		
		// Record module startup in the session log file
		UVoxelLogger::LogVoxelEvent(TEXT("FirstVoxel module started."));
	}
	
	/**
	 * Called when the module is being unloaded.
	 * Currently uses the default implementation.
	 */
	virtual void ShutdownModule() override
	{
		FDefaultGameModuleImpl::ShutdownModule();
	}
};

// Register this module with Unreal Engine's module system
// This macro tells the engine how to instantiate and manage the module
IMPLEMENT_PRIMARY_GAME_MODULE(FFirstVoxelGameModule, FirstVoxel, "FirstVoxel");
