// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FirstVoxel : ModuleRules
{
	public FirstVoxel(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"ProceduralMeshComponent",   // ← Required for UProceduralMeshComponent
			"Json",
			"JsonUtilities"
		});


		PrivateDependencyModuleNames.AddRange(new string[] { });

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd" });
		}

		PublicIncludePaths.AddRange(new string[] {
			ModuleDirectory,
			System.IO.Path.Combine(ModuleDirectory, "Voxel"),
			System.IO.Path.Combine(ModuleDirectory, "Voxel/Core"),
			System.IO.Path.Combine(ModuleDirectory, "Voxel/Generation"),
			System.IO.Path.Combine(ModuleDirectory, "Voxel/Config"),
			System.IO.Path.Combine(ModuleDirectory, "Voxel/Biomes"),
			System.IO.Path.Combine(ModuleDirectory, "UI")
		});
	}
}
