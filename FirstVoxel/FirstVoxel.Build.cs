// =============================================================================
// FirstVoxel.Build.cs
// =============================================================================
//
// Build configuration for the FirstVoxel procedural voxel engine module.
// This module implements a complete voxel-based terrain system with generation,
// rendering, physics, and water simulation capabilities.
//
// ARCHITECTURE OVERVIEW:
// - Core Voxel System: Chunk management, density generation, mesh building
// - Biome System: Multi-layer procedural generation with material overrides
// - Water System: Flow simulation, ocean rendering, swimming physics
// - Input System: Enhanced Input with terrain modification tools
// - UI System: World map, pause menu, tool selection interface
//
// DEPENDENCIES:
// - Core Unreal Engine modules for fundamental functionality
// - ProceduralMeshComponent for dynamic mesh generation
// - EnhancedInput for modern input handling
// - Json utilities for save/load functionality
// - Editor modules for in-editor world generation tools
//
// MODULE STRUCTURE:
// - Voxel/ - Core voxel engine implementation
// - UI/ - User interface components
// - Config/ - Generation and biome configuration
// - Biomes/ - Biome-specific generation logic
// =============================================================================

// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FirstVoxel : ModuleRules
{
	public FirstVoxel(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// =============================================================================
		// PUBLIC DEPENDENCIES
		// These modules are available to all code that includes FirstVoxel headers
		// =============================================================================
		PublicDependencyModuleNames.AddRange(new string[] {
			// Core Unreal Engine modules
			"Core",              // Fundamental engine functionality
			"CoreUObject",       // UObject system and reflection
			"Engine",            // Game engine core (actors, components, etc.)
			
			// Input and Interaction
			"InputCore",         // Basic input handling
			"EnhancedInput",     // Modern input action system with context support
			
			// AI and State Management
			"AIModule",          // AI behavior trees and navigation
			"StateTreeModule",   // State tree framework for complex behaviors
			"GameplayStateTreeModule", // Gameplay-specific state tree features
			
			// User Interface
			"UMG",               // Unreal Motion Graphics UI framework
			"Slate",             // Low-level UI framework
			"SlateCore",         // Core Slate functionality
			
			// Voxel Engine Core
			"ProceduralMeshComponent",   // Dynamic mesh generation for voxel terrain
			"Json",              // JSON serialization for save/load
			"JsonUtilities"      // Helper utilities for JSON operations
		});

		// =============================================================================
		// PRIVATE DEPENDENCIES
		// These modules are only available within FirstVoxel implementation files
		// =============================================================================
		PrivateDependencyModuleNames.AddRange(new string[] { });

		// =============================================================================
		// EDITOR-SPECIFIC DEPENDENCIES
		// Only included when building for the editor
		// =============================================================================
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { 
				"UnrealEd"         // Editor-specific functionality
			});
		}

		// =============================================================================
		// PUBLIC INCLUDE PATHS
		// Header search paths available to all modules that depend on FirstVoxel
		// =============================================================================
		PublicIncludePaths.AddRange(new string[] {
			ModuleDirectory,                                    // Root module directory
			System.IO.Path.Combine(ModuleDirectory, "Voxel"),   // Core voxel engine
			System.IO.Path.Combine(ModuleDirectory, "Voxel/Core"), // Core voxel classes
			System.IO.Path.Combine(ModuleDirectory, "Voxel/Generation"), // Mesh and density generation
			System.IO.Path.Combine(ModuleDirectory, "Voxel/Config"),     // Configuration structures
			System.IO.Path.Combine(ModuleDirectory, "Voxel/Biomes"),     // Biome system
			System.IO.Path.Combine(ModuleDirectory, "UI")               // User interface components
		});
	}
}
