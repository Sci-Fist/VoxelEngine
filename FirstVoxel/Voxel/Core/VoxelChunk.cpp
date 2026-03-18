// VoxelChunk.cpp
// 
// Core chunk implementation for the voxel engine, handling terrain generation,
// mesh creation, foliage placement, and water simulation integration.
//
// ARCHITECTURE OVERVIEW:
// This class represents a single chunk of the voxel world, responsible for
// generating terrain geometry, applying materials, placing foliage, and
// managing water simulation data. Chunks are the fundamental building blocks
// of the streaming voxel world system.
//
// KEY RESPONSIBILITIES:
// - Asynchronous terrain generation and mesh creation
// - Biome-based material assignment and foliage placement
// - Water simulation data management and mesh generation
// - LOD transition handling and mesh state management
// - Integration with the world streaming system
//
// PERFORMANCE CHARACTERISTICS:
// - Background thread generation to prevent frame drops
// - Procedural mesh components for dynamic geometry
// - Instanced static mesh components for efficient foliage rendering
// - Water mesh generation for realistic water surfaces
// - LOD system for distance-based detail management

#include "Core/VoxelChunk.h"
#include "FirstVoxel.h"
#include "CoreMinimal.h"
#include "Generation/VoxelMeshGenerator.h"
#include "Generation/VoxelDensityGenerator.h"
#include "Voxel/Core/World/VoxelWorld.h"
#include "Core/VoxelDataMap.h"
#include "Biomes/VoxelBiomeManager.h"
#include "VoxelLogger.h"
#include "ProceduralMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformProcess.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"

DEFINE_LOG_CATEGORY(LogVoxelChunk);

AVoxelChunk::AVoxelChunk()
{
	// Enable Tick so UpdateMeshState() can drive LOD transitions each frame.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // Starts disabled to save CPU
	PrimaryActorTick.TickGroup     = TG_PrePhysics;

	// Initialize procedural mesh component for terrain geometry
	// This component handles dynamic mesh generation and rendering
	ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralMesh"));
	RootComponent = ProceduralMesh;

	// Configure terrain mesh collision properties
	ProceduralMesh->SetCollisionProfileName(TEXT("BlockAll"));
	ProceduralMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ProceduralMesh->bUseComplexAsSimpleCollision = true;
	
	// FIX: Disable AsyncCooking to remove the timing race where IsCollisionReady() 
	// triggers before baking actually completes on background threads. 
	ProceduralMesh->bUseAsyncCooking = false; 

	// Initialize visual-only backface mesh component (no collision)
	BackfaceMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BackfaceMesh"));
	if (BackfaceMesh)
	{
		BackfaceMesh->SetupAttachment(RootComponent);
		BackfaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BackfaceMesh->bUseComplexAsSimpleCollision = false;
		BackfaceMesh->SetCastShadow(true);
	}
	
	// FIX: Ensure proper culling and rendering settings for terrain mesh
	ProceduralMesh->SetCastShadow(true);
	ProceduralMesh->SetCanEverAffectNavigation(true);
	// Note: Two-sided lighting and reverse culling settings not available in this UE version
	// The mesh should render correctly with proper normals from the mesh generation
	// If faces are still being culled, the issue is likely in the mesh generation normals
	
	// IMPORTANT: Hide terrain mesh until geometry is actually ready
	// This prevents invisible-mesh pop-in during generation
	// SetVisibility(true) is called at the end of ApplyMesh() once all sections are uploaded
	ProceduralMesh->SetVisibility(false);
	
	// Initialize mesh state management for LOD transitions
	MeshState = EChunkMeshState::Empty;
	TransitionProgress = 0.0f;
	TransitionStartTime = 0.0f;

	// Initialize water mesh component for water surface rendering
	// Water mesh is translucent, has no collision, and is managed separately from terrain
	WaterMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaterMesh"));
	WaterMesh->SetupAttachment(RootComponent);
	WaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WaterMesh->bUseComplexAsSimpleCollision = false;
	WaterMesh->bUseAsyncCooking = false;
	WaterMesh->SetCastShadow(false);
	WaterMesh->SetVisibility(false); // Hidden until BuildWaterMeshInternal confirms actual water geometry

	// Initialize foliage components using instanced static mesh for performance
	// Trees and grass are handled separately for different rendering and culling needs
	TreeHISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TreeHISM"));
	TreeHISM->SetupAttachment(RootComponent);

	GrassHISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GrassHISM"));
	GrassHISM->SetupAttachment(RootComponent);
}

void AVoxelChunk::BeginPlay()
{
	Super::BeginPlay();
	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelChunk: BeginPlay (%d,%d,%d)"),
		ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z));
}

void AVoxelChunk::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// Drive smooth LOD transitions each frame.
	if (MeshState == EChunkMeshState::Transitioning)
	{
		UpdateMeshState();
	}
}

void AVoxelChunk::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up any ongoing generation tasks before destruction
	CancelGeneration();
	
	// Log chunk cleanup for debugging purposes
	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelChunk: EndPlay (%d,%d,%d) - Reason: %d"), 
		ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, (int32)EndPlayReason));
	
	Super::EndPlay(EndPlayReason);
}

void AVoxelChunk::CancelGeneration()
{
	// Early exit if no generation is currently in progress
	if (!bGenerating) return;

	// Log generation cancellation for debugging
	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelChunk: CancelGeneration (%d,%d,%d)"), 
		ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z));

	// Cancel the current generation task if it exists
	if (CurrentTask.IsValid())
	{
		CurrentTask->Cancel();
	}

	// Invalidate any queued completion callbacks BEFORE calling the callback
	// so a re-entrant SpawnChunk call (bMeshDirty path) doesn't see bGenerating=true.
	++GenerationId;
	bGenerating = false;

	// Notify the world so it can decrement ActiveGenerations.
	// Must happen AFTER bGenerating is cleared so the world's dirty-rebuild
	// guard (`!Chunk->IsGenerating()`) works correctly on re-entry.
	if (OnGenerationComplete)
	{
		auto Callback = MoveTemp(OnGenerationComplete);
		OnGenerationComplete = nullptr; // clear before calling to prevent double-fire
		Callback();
	}
}

void AVoxelChunk::GenerateAsync()
{
	// Early exit if generation is already in progress
	if (bGenerating) return;
	
	// Set generation state flags
	bGenerating = true;
	bMeshApplied = false;

	// Log generation start for debugging and performance tracking
	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelChunk: GenerateAsync (%d,%d,%d)"), 
		ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z));

	// Increment generation ID to track this specific generation cycle
	uint32 TaskId = ++GenerationId;
	FVector Origin = GetActorLocation();

	// Determine density provider for terrain generation
	// Use injected provider if available, otherwise fall back to global generator
	IVoxelDensityProvider* DensityProvider = DensityGenerator;
	if (!DensityProvider)
	{
		static FVoxelDensityGenerator FallbackDensityGenerator;
		DensityProvider = &FallbackDensityGenerator;
	}

	// Retrieve local terrain data if data map is available
	TMap<int32, float> LocalMap;
	if (DataMap)
	{
		const FVector Pos = GetActorLocation();
		const float Size = ChunkSize * VoxelSize;
		const FIntVector GlobalChunkCoord(
			FMath::FloorToInt(Pos.X / Size),
			FMath::FloorToInt(Pos.Y / Size),
			FMath::FloorToInt(Pos.Z / Size));

		DataMap->GetChunkData(GlobalChunkCoord, LocalMap);
	}

	// Create generation task with all necessary parameters
	CurrentTask = MakeShared<FVoxelGeneratorTask>(
		ChunkCoord,                    // Chunk coordinates for world positioning
		Origin,                        // World origin for this chunk
		ChunkSize,                     // Number of voxels per dimension
		VoxelSize,                     // World units per voxel
		GetStepSize(),                 // Step size for density sampling
		GenerationConfig,              // Generation configuration settings
		DensityProvider,               // Terrain density provider
		FoliageDensity,                // Foliage placement density
		MaxFoliageSlope,               // Maximum slope for foliage placement
		LocalMap                       // Local terrain modifications
	);

	// Capture task and this pointer for async execution
	TSharedPtr<FVoxelGeneratorTask> LocalTask = CurrentTask;
	TWeakObjectPtr<AVoxelChunk> SafeThis(this);

	// Execute generation task on background thread
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [SafeThis, LocalTask, TaskId]()
	{
		// Safety check: ensure chunk still exists and task wasn't cancelled
		if (!SafeThis.IsValid() || LocalTask->IsCancelled()) return;

		// Execute the generation task
		LocalTask->Execute();

		// Return to game thread to apply the generated mesh
		AsyncTask(ENamedThreads::GameThread, [SafeThis, LocalTask, TaskId]()
		{
			// Final safety check: ensure chunk still exists and this is the correct generation
			if (SafeThis.IsValid() && TaskId == SafeThis->GenerationId)
			{
				SafeThis->ApplyMesh(LocalTask);
			}
		});
	});
}

void AVoxelChunk::GenerateSync()
{
	// Create a global density generator for synchronous generation
	// This is used primarily in editor contexts where async generation isn't suitable
	static FVoxelDensityGenerator GlobalDensityGeneratorSync;
	
	// Retrieve local terrain data if data map is available
	TMap<int32, float> LocalMap;
	if (DataMap)
	{
		const FVector Pos = GetActorLocation();
		const float Size = ChunkSize * VoxelSize;
		const FIntVector GlobalChunkCoord(
			FMath::FloorToInt(Pos.X / Size),
			FMath::FloorToInt(Pos.Y / Size),
			FMath::FloorToInt(Pos.Z / Size));

		DataMap->GetChunkData(GlobalChunkCoord, LocalMap);
	}

	// Create generation task with synchronous execution parameters
	CurrentTask = MakeShared<FVoxelGeneratorTask>(
		ChunkCoord,                    // Chunk coordinates for world positioning
		GetActorLocation(),            // World origin for this chunk
		ChunkSize,                     // Number of voxels per dimension
		VoxelSize,                     // World units per voxel
		GetStepSize(),                 // Step size for density sampling
		GenerationConfig,              // Generation configuration settings
		&GlobalDensityGeneratorSync,   // Synchronous density provider
		FoliageDensity,                // Foliage placement density
		MaxFoliageSlope,               // Maximum slope for foliage placement
		LocalMap                       // Local terrain modifications
	);
	
	// Execute generation task synchronously on current thread
	CurrentTask->Execute();
	
	// Apply the generated mesh immediately
	ApplyMesh(CurrentTask);
}

void AVoxelChunk::ApplyMesh(TSharedPtr<FVoxelGeneratorTask> CompletedTask)
{
	// Safety check: ensure task is valid
	if (!CompletedTask.IsValid()) return;

	// Get the generated mesh output from the completed task
	const FVoxelMeshOutput& Out = CompletedTask->GetMeshOutput();

	// Store mesh output for state management and LOD transitions
	MeshOutput = Out;

	// ── Determine per-biome material overrides ────────────────────────────
	// Sample the dominant biome at the chunk geometric centre (XY and Z).
	// This provides accurate biome-based material selection for the entire chunk.
	const float HalfChunk = ChunkSize * VoxelSize * 0.5f;
	const FVector ChunkCentre = GetActorLocation() + FVector(HalfChunk, HalfChunk, HalfChunk);
	const FVoxelBiomeWeightMap CentreWeights =
		FVoxelBiomeManager::GetBiomeWeightsStatic(ChunkCentre.X, ChunkCentre.Y, GenerationConfig);
	const EVoxelBiome DominantBiome = CentreWeights.GetDominantBiome();
	const FVoxelBiomeRenderConfig& BiomeRender = GenerationConfig.GetBiomeRender(DominantBiome);

	// Determine materials to use for flat and slope surfaces
	UMaterialInterface* FlatMat  = MasterFlatMaterial;
	UMaterialInterface* SlopeMat = MasterSlopeMaterial;
	
	// Apply biome-specific material overrides if enabled and threshold met
	if (BiomeRender.bEnableMaterialOverride &&
		CentreWeights.GetWeight(DominantBiome) >= BiomeRender.MaterialOverrideThreshold)
	{
		if (BiomeRender.FlatMaterialOverride)  FlatMat  = BiomeRender.FlatMaterialOverride.Get();
		if (BiomeRender.SlopeMaterialOverride) SlopeMat = BiomeRender.SlopeMaterialOverride.Get();
	}
	
	// FIX: Ensure materials are valid - fall back to master materials if biome overrides are invalid
	if (!FlatMat)  FlatMat = MasterFlatMaterial;
	if (!SlopeMat) SlopeMat = MasterSlopeMaterial;
	
	// If no material is assigned (user hasn't set MasterFlatMaterial / MasterSlopeMaterial
	// on the AVoxelWorld Details panel), fall back to the engine default surface material.
	// We log this ONCE globally rather than once per chunk (which floods the output log
	// with hundreds of identical warnings every generation).
	if (!FlatMat)
	{
		FlatMat = UMaterial::GetDefaultMaterial(MD_Surface);
		static bool bFlatWarned = false; // suppress per-chunk spam
		if (!bFlatWarned) { bFlatWarned = true;
			UE_LOG(LogVoxelChunk, Warning,
				TEXT("VoxelChunk: MasterFlatMaterial is not assigned on AVoxelWorld. "
				     "Assign a material in the Details panel under Voxel|Materials. "
				     "Using engine default for now. (This message appears once.)"));
		}
	}
	if (!SlopeMat)
	{
		SlopeMat = UMaterial::GetDefaultMaterial(MD_Surface);
		static bool bSlopeWarned = false;
		if (!bSlopeWarned) { bSlopeWarned = true;
			UE_LOG(LogVoxelChunk, Warning,
				TEXT("VoxelChunk: MasterSlopeMaterial is not assigned on AVoxelWorld. "
				     "Assign a material in the Details panel under Voxel|Materials. "
				     "Using engine default for now. (This message appears once.)"));
		}
	}
	
	// FIX: Ensure proper material assignment - use slope material for steep faces
	// This ensures the material assignment logic works correctly
	// Note: Material assignment is handled per-section in UploadSection() based on mesh data
	// The material selection above (FlatMat vs SlopeMat) is applied when uploading mesh sections
	

	// ── Generate biome-specific mesh name ─────────────────────────────────
	// Create descriptive mesh names that include biome information for debugging
	FString BiomeName = "Unknown";
	switch (DominantBiome)
	{
		case EVoxelBiome::Forest:  BiomeName = "Forest"; break;
		case EVoxelBiome::Desert:  BiomeName = "Desert"; break;
		case EVoxelBiome::Peaks:   BiomeName = "Peaks"; break;
		case EVoxelBiome::Cliffs:  BiomeName = "Cliffs"; break;
		case EVoxelBiome::Mesa:    BiomeName = "Mesa"; break;
		case EVoxelBiome::Craters: BiomeName = "Craters"; break;
		default: BiomeName = "Mixed";
	}

	// Generate chunk coordinate string for mesh naming
	FString ChunkCoordStr = FString::Printf(TEXT("%d_%d_%d"), ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z);
	
	// Set biome-specific mesh section names for debugging and profiling
	FString FlatMeshName = FString::Printf(TEXT("FlatMesh_%s_%s"), *BiomeName, *ChunkCoordStr);
	// ── Upload terrain mesh sections ──────────────────────────────────────
	// Clear existing mesh sections and upload new geometry
	ProceduralMesh->ClearAllMeshSections();
	if (BackfaceMesh) BackfaceMesh->ClearAllMeshSections();

	// Section 0: Flats
	UploadSection(0, Out.FlatMesh, FlatMat, FlatMeshName);
	
	// Section 1: Slopes (Outward with physics)
	UploadSection(1, Out.SlopeMesh, SlopeMat, FString::Printf(TEXT("SlopeMesh_%s"), *ChunkCoordStr));

	// Upload backfaces to dedicated visual-only component (no collision)
	if (BackfaceMesh)
	{
		UploadSection(0, Out.BackMesh, FlatMat, FString::Printf(TEXT("BackMesh_Flat_%s"), *ChunkCoordStr), BackfaceMesh);
		UploadSection(1, Out.SlopeBackMesh, SlopeMat, FString::Printf(TEXT("BackMesh_Slope_%s"), *ChunkCoordStr), BackfaceMesh);
	}

	// ── Per-biome foliage system (Recycled / Pooled) ──────────────────────
	// Handle biome-specific foliage placement with efficient component reuse
	const TArray<TArray<FTransform>>& PerFoliage = CompletedTask->GetPerFoliageTransforms();
	const TArray<UStaticMesh*>&       FoliageMeshes = CompletedTask->GetPerFoliageMeshes();

	// FIX: Check that at least one foliage slot has actual geometry.
	// PerFoliage.Num() > 0 is true whenever FoliageSlots were pre-cached (even
	// if every slot has zero instances), which silently bypassed the legacy
	// tree/grass path even when no per-biome foliage meshes were assigned.
	bool bHasPerBiomeFoliage = false;
	for (int32 s = 0; s < PerFoliage.Num(); ++s)
	{
		if (FoliageMeshes.IsValidIndex(s) && FoliageMeshes[s] && PerFoliage[s].Num() > 0)
		{
			bHasPerBiomeFoliage = true;
			break;
		}
	}

	if (bHasPerBiomeFoliage)
	{
		// Process each foliage type with component pooling for performance
		for (int32 Slot = 0; Slot < PerFoliage.Num(); ++Slot)
		{
			UStaticMesh* SlotMesh = (Slot < FoliageMeshes.Num()) ? FoliageMeshes[Slot] : nullptr;
			if (!SlotMesh || PerFoliage[Slot].Num() == 0) continue;

			UInstancedStaticMeshComponent* HISM = nullptr;
			
			// Reuse existing component if available, otherwise create new one
			if (Slot < BiomeFoliageHISMs.Num())
			{
				HISM = BiomeFoliageHISMs[Slot];
				if (HISM) HISM->ClearInstances();
			}
			else
			{
				const int32 NextIdx = BiomeFoliageHISMs.Num();
				HISM = NewObject<UInstancedStaticMeshComponent>(this,
					*FString::Printf(TEXT("BiomeFoliage_%d"), NextIdx));
				HISM->SetupAttachment(RootComponent);
				HISM->RegisterComponent();
				BiomeFoliageHISMs.Add(HISM);
			}

			if (HISM)
			{
				// IMPORTANT: Set mobility to Movable BEFORE SetStaticMesh
				// Components default to Static, which prevents runtime mesh assignment
				HISM->SetMobility(EComponentMobility::Movable);
				HISM->SetStaticMesh(SlotMesh);
				// Note: Consider SetMobility(Static) after initial setup for Lumen baking
				// but this breaks runtime foliage updates
				HISM->SetCullDistances(0, 12000);
				HISM->AddInstances(PerFoliage[Slot], false);
			}
		}

		// Clear unused instances for trailing pooled slots to prevent memory leaks
		for (int32 i = PerFoliage.Num(); i < BiomeFoliageHISMs.Num(); ++i)
		{
			if (BiomeFoliageHISMs[i]) BiomeFoliageHISMs[i]->ClearInstances();
		}
	}
	else
	{
		// ── Legacy fallback: global tree/grass meshes ─────────────────────
		// Handle traditional tree and grass placement for backward compatibility
		if (IsValid(TreeHISM))  TreeHISM->ClearInstances();
		if (IsValid(GrassHISM)) GrassHISM->ClearInstances();

		// Get transform arrays from completed task
		const TArray<FTransform>& TreeTransforms  = CompletedTask->GetTreeTransforms();
		const TArray<FTransform>& GrassTransforms = CompletedTask->GetGrassTransforms();

		// Apply tree instances with proper component setup
		if (IsValid(TreeHISM) && TreeMesh)
		{
			// IMPORTANT: Set mobility to Movable before SetStaticMesh
			TreeHISM->SetMobility(EComponentMobility::Movable);
			TreeHISM->SetStaticMesh(TreeMesh);
			TreeHISM->AddInstances(TreeTransforms, false);
		}
		
		// Apply grass instances with proper component setup
		if (IsValid(GrassHISM) && GrassMesh)
		{
			// IMPORTANT: Set mobility to Movable before SetStaticMesh
			GrassHISM->SetMobility(EComponentMobility::Movable);
			GrassHISM->SetStaticMesh(GrassMesh);
			GrassHISM->AddInstances(GrassTransforms, false);
		}
	}

	// ── Populate water solid-cell map from the task's density array ──────────
	// This is used by FVoxelWaterSimulator to know which cells block water flow.
	// Initialize water data structure and populate solid cell information
	{
		WaterData.Init(ChunkSize);
		const TArray<float>& Dens = CompletedTask->GetDensities();
		// FIX: Density array is indexed in effective (LOD-stepped) voxel space.
		// EffCS = voxels-per-axis after stepping, S = padded stride.
		// The water cell map is always ChunkSize^3 (full resolution), so we
		// sample the density array at the nearest stepped voxel for each cell.
		const int32 EffCS      = ChunkSize / GetStepSize();
		const int32 S          = EffCS + 3;
		const int32 StepSz     = GetStepSize();
		const FVector ChunkOrigin = GetActorLocation();
		
		for (int32 lz = 0; lz < ChunkSize; ++lz)
		for (int32 ly = 0; ly < ChunkSize; ++ly)
		for (int32 lx = 0; lx < ChunkSize; ++lx)
		{
			// Map full-res voxel to nearest stepped voxel, then into padded density array.
			const int32 ex   = FMath::Clamp(lx / StepSz, 0, EffCS - 1);
			const int32 ey   = FMath::Clamp(ly / StepSz, 0, EffCS - 1);
			const int32 ez   = FMath::Clamp(lz / StepSz, 0, EffCS - 1);
			const int32 DIdx = (ex + 1) + (ey + 1) * S + (ez + 1) * S * S;
			const int32 WIdx = lx + ly * ChunkSize + lz * ChunkSize * ChunkSize;
			if (Dens.IsValidIndex(DIdx))
			{
				const bool bSolid = (Dens[DIdx] > 0.f);
				WaterData.SolidCells[WIdx] = bSolid;

				// ---- 🌊 VOXEL OCEAN FILL ----
				if (GenerationConfig.Water.bUseVoxelOcean && !bSolid)
				{
					const float WorldZ = ChunkOrigin.Z + lz * VoxelSize;
					if (WorldZ <= GenerationConfig.SeaLevel)
					{
						WaterData.Cells[WIdx] = WATER_SOURCE;
					}
				}
			}
		}
	}

	// ── Register water source positions detected during generation ────────────
	// OnChunkWaterReady is bound by AVoxelWorld so it can call SetSource() on the simulator.
	// This enables water simulation to know where to place water sources in the world.
	if (OnChunkWaterReady)
	{
		OnChunkWaterReady(CompletedTask->GetWaterSources());
	}

	// FIX: Reveal terrain mesh NOW — geometry + material are both fully ready.
	ProceduralMesh->SetVisibility(true);
	ProceduralMesh->UpdateBounds();
	if (BackfaceMesh) BackfaceMesh->UpdateBounds();

	// --- 💊 DEFER VISIBILITY FIX ---
	// Reveal the entire actor and trigger physics collision ONLY after 
	// all mesh buffers (procedural + foliage) are fully uploaded and safe.
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);

	// Update generation state flags
	bMeshApplied = true;
	bGenerating  = false;
	MeshState    = EChunkMeshState::Ready;

	// Notify AVoxelWorld that this chunk finished so it can decrement ActiveGenerations.
	// Use MoveTemp to prevent double-fire if the callback re-enters this chunk.
	if (OnGenerationComplete)
	{
		auto Callback = MoveTemp(OnGenerationComplete);
		OnGenerationComplete = nullptr;
		Callback();
	}
}

void AVoxelChunk::UploadSection(int32 SectionIndex, const FVoxelMeshData& Data, UMaterialInterface* Mat, const FString& SectionName, UProceduralMeshComponent* TargetMesh)
{
	UProceduralMeshComponent* MeshToUse = TargetMesh ? TargetMesh : ProceduralMesh;

	// Safety check: ensure we have valid data and mesh component
	if (Data.Vertices.Num() == 0 || !IsValid(MeshToUse)) return;

	// Build collision for ALL sections on the main ProceduralMesh at LOD 0/1.
	// FIX: The old code only built collision for SectionIndex == 0 (flat faces).
	// Slope/cliff faces (section 1) had NO collision, so the player would fall
	// through any wall or cliff steeper than the SlopeThreshold. Both sections
	// on ProceduralMesh need collision; BackfaceMesh never needs it.
	const bool bBuildCollision = (MeshToUse == ProceduralMesh) && (LOD <= 1);

	MeshToUse->CreateMeshSection(
		SectionIndex,
		Data.Vertices,
		Data.Triangles,
		Data.Normals,
		Data.UVs,
		Data.VertexColors,
		Data.Tangents,
		bBuildCollision
	);

	// Apply material if provided
	if (Mat) MeshToUse->SetMaterial(SectionIndex, Mat);

	// UProceduralMeshComponent does not support per-section naming; the parameter
	// is accepted for call-site readability / future debug tooling only.
}

void AVoxelChunk::DestroyAndRebuildMesh()
{
	GenerateAsync();
}

// ---------------------------------------------------------------------------
// Water Mesh Generation System
// ---------------------------------------------------------------------------
void AVoxelChunk::RebuildWaterMesh()
{
	// Safety check: ensure water mesh component is valid
	if (!IsValid(WaterMesh)) return;
	
	// Generate water mesh geometry
	BuildWaterMeshInternal();
	
	// Mark water data as clean after mesh rebuild
	WaterData.bMeshDirty = false;
}

void AVoxelChunk::BuildWaterMeshInternal()
{
	// Clear existing water mesh sections
	WaterMesh->ClearAllMeshSections();

	// Safety check: ensure water material is assigned
	// Without a material the water mesh renders UE's default black geometry
	// which overlays and hides the terrain. Skip the build entirely.
	if (!WaterMaterial)
	{
		WaterMesh->SetVisibility(false);
		return;
	}

	// Early exit if no water exists in this chunk
	if (!WaterData.HasAnyWater())
	{
		WaterMesh->SetVisibility(false);
		return;
	}

	// Generate procedural water mesh for visualization
	// We create simple flat-quad meshes for every water surface cell:
	// - Top face (when air is above the water)
	// - Side faces (when water meets air on the sides)
	TArray<FVector>   Vertices;
	TArray<int32>     Triangles;
	TArray<FVector>   Normals;
	TArray<FVector2D> UVs;

	const int32 CS = ChunkSize;
	const float VS = VoxelSize;

	// Helper function: get water level at local coordinates with bounds checking
	auto GetW = [&](int32 lx, int32 ly, int32 lz) -> uint8
	{
		if (lx < 0 || lx >= CS || ly < 0 || ly >= CS || lz < 0 || lz >= CS)
			return WATER_EMPTY;
		const int32 i = lx + ly * CS + lz * CS * CS;
		return WaterData.Cells[i];
	};

	// Helper function: check if coordinate contains solid terrain
	auto IsSolidLocal = [&](int32 lx, int32 ly, int32 lz) -> bool
	{
		if (lx < 0 || lx >= CS || ly < 0 || ly >= CS || lz < 0 || lz >= CS)
			return false; // outside chunk = treat as air (neighbour chunk handles it)
		const int32 i = lx + ly * CS + lz * CS * CS;
		return WaterData.SolidCells.IsValidIndex(i) && WaterData.SolidCells[i];
	};

	// Helper function: emit a quad face with proper UVs and normals
	auto EmitQuad = [&](FVector V0, FVector V1, FVector V2, FVector V3, FVector Normal)
	{
		const int32 Base = Vertices.Num();
		Vertices.Add(V0); Vertices.Add(V1); Vertices.Add(V2); Vertices.Add(V3);
		Normals.Add(Normal); Normals.Add(Normal); Normals.Add(Normal); Normals.Add(Normal);
		UVs.Add({0,0}); UVs.Add({1,0}); UVs.Add({1,1}); UVs.Add({0,1});
		Triangles.Add(Base+0); Triangles.Add(Base+1); Triangles.Add(Base+2);
		Triangles.Add(Base+0); Triangles.Add(Base+2); Triangles.Add(Base+3);
	};

	// Generate water mesh geometry for each voxel in the chunk
	for (int32 lz = 0; lz < CS; ++lz)
	for (int32 ly = 0; ly < CS; ++ly)
	for (int32 lx = 0; lx < CS; ++lx)
	{
		uint8 Level = GetW(lx, ly, lz);

		// Skip empty cells
		if (Level == WATER_EMPTY) continue;

		// Calculate water surface height
		const float WLevel  = (Level == WATER_SOURCE ? 1.f : (float)Level / (float)WATER_FULL);
		const float x0 = lx * VS;
		const float y0 = ly * VS;
		const float z0 = lz * VS;
		const float x1 = x0 + VS;
		const float y1 = y0 + VS;
		const float zTop = z0 + VS * WLevel; // water surface at fill fraction

		// Generate top face if cell above is air/empty
		// This creates the visible water surface
		const bool bAboveEmpty = !IsSolidLocal(lx, ly, lz+1) && GetW(lx, ly, lz+1) == WATER_EMPTY;
		if (bAboveEmpty)
		{
			EmitQuad(
				FVector(x0, y0, zTop),
				FVector(x1, y0, zTop),
				FVector(x1, y1, zTop),
				FVector(x0, y1, zTop),
				FVector::UpVector);
		}
	}

	// Early exit if no water geometry was generated
	if (Vertices.Num() == 0)
	{
		WaterMesh->SetVisibility(false);
		return;
	}

	// Create mesh section with generated geometry
	TArray<FVector>          EmptyNorms;
	TArray<FColor>           EmptyColors;
	TArray<FProcMeshTangent> EmptyTangents;

	WaterMesh->CreateMeshSection(
		0, Vertices, Triangles, Normals, UVs, EmptyColors, EmptyTangents, false);

	// Apply water material
	if (WaterMaterial)
		WaterMesh->SetMaterial(0, WaterMaterial);

	// Make water mesh visible
	WaterMesh->SetVisibility(true);

	// --- 📜 LOGGER ---
	// Log procedural water volume metrics for debugging and performance analysis
	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelWater: Chunk %s emitted %d procedural water vertices (Ocean/Pools)"), 
		*ChunkCoord.ToString(), Vertices.Num()));
}

void AVoxelChunk::ClearMesh()
{
	// Increment generation ID to invalidate any pending operations
	++GenerationId;
	
	// Clear terrain mesh sections and hide component
	ProceduralMesh->ClearAllMeshSections();
	ProceduralMesh->SetVisibility(false); // hide until next ApplyMesh reveals it
	
	// Clear water mesh sections and hide component
	WaterMesh->ClearAllMeshSections();
	WaterMesh->SetVisibility(false);
	
	// Reset water simulation data
	WaterData.Reset();
	
	// Recycle dynamic per-biome foliage components instead of destroying them.
	// This preserves the components on the actor when returned to the pool,
	// removing thousands of synchronous Game Thread deallocations/allocations.
	for (UInstancedStaticMeshComponent* HISM : BiomeFoliageHISMs)
	{
		if (IsValid(HISM))
		{
			HISM->ClearInstances();
		}
	}
	
	// Reset async cooking to default for pooled recycling
	if (ProceduralMesh) ProceduralMesh->bUseAsyncCooking = true;
	
	// Reset mesh state flags
	bMeshApplied = false;
	MeshState    = EChunkMeshState::Empty;
}

#if WITH_EDITOR
void AVoxelChunk::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Handle property changes in editor context
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	// Regenerate chunk if any properties were modified
	if (PropertyChangedEvent.Property)
	{
		GenerateSync();
	}
}
#endif

// Note: OnMeshGenerated intentionally removed — the mesh ready callback is handled
// by OnGenerationComplete (the TFunction<void()> delegate) which AVoxelWorld binds.
// Keeping a dead stub here caused confusion about the correct notification path.

// ---------------------------------------------------------------------------
// Smooth LOD Transition System
// ---------------------------------------------------------------------------

void AVoxelChunk::TransitionToLOD(int32 NewLOD)
{
	if (NewLOD == LOD) return;
	if (bGenerating) return; // already mid-transition; let it finish

	PreviousMesh        = MeshOutput;
	TargetLOD           = NewLOD;
	MeshState           = EChunkMeshState::Transitioning;
	TransitionProgress  = 0.0f;
	TransitionStartTime = GetWorld()->GetTimeSeconds();

	LOD = NewLOD;
	SetActorTickEnabled(true); // Enable tick to drive transition progress
	GenerateAsync();
}

void AVoxelChunk::UpdateMeshState()
{
	// Handle mesh state transitions and progress tracking
	if (MeshState == EChunkMeshState::Transitioning)
	{
		// Calculate transition progress based on elapsed time
		float CurrentTime = GetWorld()->GetTimeSeconds();
		float Elapsed = CurrentTime - TransitionStartTime;
		TransitionProgress = FMath::Clamp(Elapsed / TransitionDuration, 0.0f, 1.0f);
		
		if (TransitionProgress >= 1.0f)
		{
			// Transition complete
			MeshState = EChunkMeshState::Ready;
			PreviousMesh.Reset();
			SetActorTickEnabled(false); // Disable tick when idle
		}
		else
		{
			// TODO: Implement mesh blending here when both meshes are available
			// For now, just ensure visibility is maintained during transition
			SetMeshVisibility(true);
		}
	}
}

void AVoxelChunk::BlendMeshes(const FVoxelMeshOutput& From, const FVoxelMeshOutput& To, float Alpha)
{
	// Placeholder implementation for mesh blending
	// For now, we'll use a simple approach: keep the old mesh visible until new is ready
	// In a full implementation, we would:
	// 1. Create intermediate mesh data by interpolating vertices
	// 2. Update mesh sections with blended data
	// 3. Handle material transitions
	
	// Current implementation: maintain visibility during transition
	if (Alpha < 1.0f)
	{
		// Keep current mesh visible during transition
		ProceduralMesh->SetVisibility(true);
	}
	else
	{
		// Transition complete, ensure new mesh is visible
		ProceduralMesh->SetVisibility(true);
	}
}

void AVoxelChunk::SetMeshVisibility(bool bVisible)
{
	// Drive visibility on all renderable components.
	// ProceduralMesh: only show when we actually have geometry (not Empty/Error).
	if (ProceduralMesh)
	{
		const bool bHasGeometry =
			(MeshState != EChunkMeshState::Empty) &&
			(MeshState != EChunkMeshState::Error);
		ProceduralMesh->SetVisibility(bVisible && bHasGeometry);
	}
	
	// Update water mesh visibility
	if (WaterMesh)
	{
		WaterMesh->SetVisibility(bVisible);
	}
	
	// Update per-biome foliage visibility
	for (UInstancedStaticMeshComponent* HISM : BiomeFoliageHISMs)
	{
		if (HISM)
		{
			HISM->SetVisibility(bVisible);
		}
	}
	
	// Update legacy foliage visibility
	if (TreeHISM)
	{
		TreeHISM->SetVisibility(bVisible);
	}
	
	if (GrassHISM)
	{
		GrassHISM->SetVisibility(bVisible);
	}
}
