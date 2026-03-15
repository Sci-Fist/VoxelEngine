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
			"ProceduralMeshComponent"   // ← Required for UProceduralMeshComponent
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
			System.IO.Path.Combine(ModuleDirectory, "Variant_Platforming"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_Platforming/Animation"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_Combat"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_Combat/AI"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_Combat/Interfaces"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_Combat/Gameplay"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_Combat/UI"),
			System.IO.Path.Combine(ModuleDirectory, "UI"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_SideScrolling"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_SideScrolling/AI"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_SideScrolling/Interfaces"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_SideScrolling/Gameplay"),
			System.IO.Path.Combine(ModuleDirectory, "Variant_SideScrolling/UI")
		});
	}
}
