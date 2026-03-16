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
	ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralMesh"));
	RootComponent = ProceduralMesh;

	ProceduralMesh->SetCollisionProfileName(TEXT("BlockAll"));
	ProceduralMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ProceduralMesh->bUseComplexAsSimpleCollision = true;
	ProceduralMesh->bUseAsyncCooking = true;
	// FIX: Hide terrain mesh until geometry is actually ready to prevent invisible-mesh pop.
	// SetVisibility(true) is called at the end of ApplyMesh() once all sections are uploaded.
	ProceduralMesh->SetVisibility(false);
	
	// Initialize mesh state
	MeshState = EChunkMeshState::Empty;
	TransitionProgress = 0.0f;
	TransitionStartTime = 0.0f;

	// Water mesh component — translucent, no collision, visible by default for proper rendering.
	WaterMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaterMesh"));
	WaterMesh->SetupAttachment(RootComponent);
	WaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WaterMesh->bUseComplexAsSimpleCollision = false;
	WaterMesh->bUseAsyncCooking = false;
	WaterMesh->SetCastShadow(false);
	WaterMesh->SetVisibility(true); // Changed from false to true

	TreeHISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TreeHISM"));
	TreeHISM->SetupAttachment(RootComponent);

	GrassHISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GrassHISM"));
	GrassHISM->SetupAttachment(RootComponent);
}

void AVoxelChunk::BeginPlay()
{
	Super::BeginPlay();
}
void AVoxelChunk::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelGeneration();
	Super::EndPlay(EndPlayReason);
}

void AVoxelChunk::CancelGeneration()
{
	if (!bGenerating) return;

	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelChunk: CancelGeneration (%d,%d,%d)"), ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z));

	if (CurrentTask.IsValid())
	{
		CurrentTask->Cancel();
	}

	// Invalidate any queued completion callbacks for this generation.
	++GenerationId;
	
	// Ensure we don't report as generating once cancellation is requested.
	bGenerating = false;

	// Notify the world once so it can decrement ActiveGenerations even on cancel.
	if (OnGenerationComplete)
	{
		OnGenerationComplete();
		OnGenerationComplete = nullptr;
	}
}

void AVoxelChunk::GenerateAsync()
{
	if (bGenerating) return;
	bGenerating = true;
	bMeshApplied = false;

	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelChunk: GenerateAsync (%d,%d,%d)"), ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z));

	uint32 TaskId = ++GenerationId;
	FVector Origin = GetActorLocation();

	// Use injected FVoxelDensityGenerator so caves, skylands, and overhangs are all active.
	IVoxelDensityProvider* DensityProvider = DensityGenerator;
	if (!DensityProvider)
	{
		static FVoxelDensityGenerator FallbackDensityGenerator;
		DensityProvider = &FallbackDensityGenerator;
	}

	TMap<int32, float> LocalMap;
	if (DataMap)
	{
		DataMap->GetChunkData(ChunkCoord, LocalMap);
	}

	CurrentTask = MakeShared<FVoxelGeneratorTask>(
		ChunkCoord,
		Origin,
		ChunkSize,
		VoxelSize,
		GetStepSize(),
		GenerationConfig,
		DensityProvider,
		FoliageDensity,
		MaxFoliageSlope,
		LocalMap
	);

	TSharedPtr<FVoxelGeneratorTask> LocalTask = CurrentTask;
	TWeakObjectPtr<AVoxelChunk> SafeThis(this);

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [SafeThis, LocalTask, TaskId]()
	{
		if (!SafeThis.IsValid() || LocalTask->IsCancelled()) return;

		LocalTask->Execute();

		AsyncTask(ENamedThreads::GameThread, [SafeThis, LocalTask, TaskId]()
		{
			if (SafeThis.IsValid() && TaskId == SafeThis->GenerationId)
			{
				SafeThis->ApplyMesh(LocalTask);
			}
		});
	});
}

void AVoxelChunk::GenerateSync()
{
	static FVoxelDensityGenerator GlobalDensityGeneratorSync;
	TMap<int32, float> LocalMap;
	if (DataMap)
	{
		DataMap->GetChunkData(ChunkCoord, LocalMap);
	}

	CurrentTask = MakeShared<FVoxelGeneratorTask>(
		ChunkCoord,
		GetActorLocation(),
		ChunkSize,
		VoxelSize,
		GetStepSize(),
		GenerationConfig,
		&GlobalDensityGeneratorSync,
		FoliageDensity,
		MaxFoliageSlope,
		LocalMap
	);
	CurrentTask->Execute();
	ApplyMesh(CurrentTask);
}

void AVoxelChunk::ApplyMesh(TSharedPtr<FVoxelGeneratorTask> CompletedTask)
{
	if (!CompletedTask.IsValid()) return;

	const FVoxelMeshOutput& Out = CompletedTask->GetMeshOutput();

	// Store mesh output for state management and transitions
	MeshOutput = Out;

	// ── Determine per-biome material overrides ────────────────────────────
	// Sample the dominant biome at the chunk geometric centre (XY and Z).
	const float HalfChunk = ChunkSize * VoxelSize * 0.5f;
	const FVector ChunkCentre = GetActorLocation() + FVector(HalfChunk, HalfChunk, HalfChunk);
	const FVoxelBiomeWeightMap CentreWeights =
		FVoxelBiomeManager::GetBiomeWeightsStatic(ChunkCentre.X, ChunkCentre.Y, GenerationConfig);
	const EVoxelBiome DominantBiome = CentreWeights.GetDominantBiome();
	const FVoxelBiomeRenderConfig& BiomeRender = GenerationConfig.GetBiomeRender(DominantBiome);

	UMaterialInterface* FlatMat  = MasterFlatMaterial;
	UMaterialInterface* SlopeMat = MasterSlopeMaterial;
	if (BiomeRender.bEnableMaterialOverride &&
		CentreWeights.GetWeight(DominantBiome) >= BiomeRender.MaterialOverrideThreshold)
	{
		if (BiomeRender.FlatMaterialOverride)  FlatMat  = BiomeRender.FlatMaterialOverride.Get();
		if (BiomeRender.SlopeMaterialOverride) SlopeMat = BiomeRender.SlopeMaterialOverride.Get();
	}

	// ── Upload terrain mesh sections ──────────────────────────────────────
	ProceduralMesh->ClearAllMeshSections();
	UploadSection(0, Out.FlatMesh,  FlatMat);
	UploadSection(1, Out.SlopeMesh, SlopeMat);

	// ── Per-biome foliage system (Recycled / Pooled) ──────────────────────
	const TArray<TArray<FTransform>>& PerFoliage = CompletedTask->GetPerFoliageTransforms();
	const TArray<UStaticMesh*>&       FoliageMeshes = CompletedTask->GetPerFoliageMeshes();

	const bool bHasPerBiomeFoliage = PerFoliage.Num() > 0;

	if (bHasPerBiomeFoliage)
	{
		for (int32 Slot = 0; Slot < PerFoliage.Num(); ++Slot)
		{
			UStaticMesh* SlotMesh = (Slot < FoliageMeshes.Num()) ? FoliageMeshes[Slot] : nullptr;
			if (!SlotMesh || PerFoliage[Slot].Num() == 0) continue;

			UInstancedStaticMeshComponent* HISM = nullptr;
			if (Slot < BiomeFoliageHISMs.Num())
			{
				HISM = BiomeFoliageHISMs[Slot];
				if (HISM) HISM->ClearInstances();
			}
			else
			{
				float NextIdx = BiomeFoliageHISMs.Num();
				// Use %d so component names are clean integers, not "BiomeFoliage_0.000000".
				HISM = NewObject<UInstancedStaticMeshComponent>(this,
				*FString::Printf(TEXT("BiomeFoliage_%d"), (int32)NextIdx));
				HISM->SetupAttachment(RootComponent);
				HISM->RegisterComponent();
				BiomeFoliageHISMs.Add(HISM);
			}

			if (HISM)
			{
				// FIX: Set mobility to Movable BEFORE SetStaticMesh to avoid Static mobility error
				// Components default to Static, which prevents runtime mesh assignment
				HISM->SetMobility(EComponentMobility::Movable);
				HISM->SetStaticMesh(SlotMesh);
				// TODO: Consider SetMobility(Static) after initial setup for Lumen baking
				// but this breaks runtime foliage updates
				HISM->SetCullDistances(0, 12000);
				HISM->AddInstances(PerFoliage[Slot], false);
			}
		}

		// Clear unused instances for trailing pooled slots
		for (int32 i = PerFoliage.Num(); i < BiomeFoliageHISMs.Num(); ++i)
		{
			if (BiomeFoliageHISMs[i]) BiomeFoliageHISMs[i]->ClearInstances();
		}

	}
	else
	{
		// ── Legacy fallback: global tree/grass meshes ─────────────────────
		if (IsValid(TreeHISM))  TreeHISM->ClearInstances();
		if (IsValid(GrassHISM)) GrassHISM->ClearInstances();

		// Local variables — these are only needed during this call, so they live on the stack.
		const TArray<FTransform>& TreeTransforms  = CompletedTask->GetTreeTransforms();
		const TArray<FTransform>& GrassTransforms = CompletedTask->GetGrassTransforms();

		if (IsValid(TreeHISM) && TreeMesh)
		{
			// FIX: Set mobility to Movable before SetStaticMesh (components default to Static)
			TreeHISM->SetMobility(EComponentMobility::Movable);
			TreeHISM->SetStaticMesh(TreeMesh);
			TreeHISM->AddInstances(TreeTransforms, false);
		}
		if (IsValid(GrassHISM) && GrassMesh)
		{
			// FIX: Set mobility to Movable before SetStaticMesh (components default to Static)
			GrassHISM->SetMobility(EComponentMobility::Movable);
			GrassHISM->SetStaticMesh(GrassMesh);
			GrassHISM->AddInstances(GrassTransforms, false);
		}
	}

	// ── Populate water solid-cell map from the task's density array ──────────
	// This is used by FVoxelWaterSimulator to know which cells block water flow.
	{
		WaterData.Init(ChunkSize);
		const TArray<float>& Dens = CompletedTask->GetDensities();
		const int32 S = (ChunkSize / GetStepSize()) + 3; // density array stride
		for (int32 lz = 0; lz < ChunkSize; ++lz)
		for (int32 ly = 0; ly < ChunkSize; ++ly)
		for (int32 lx = 0; lx < ChunkSize; ++lx)
		{
			// Density array: local voxel (lx,ly,lz) is at padded index (lx+1, ly+1, lz+1)
			const int32 DIdx = (lx + 1) + (ly + 1) * S + (lz + 1) * S * S;
			const int32 WIdx = lx + ly * ChunkSize + lz * ChunkSize * ChunkSize;
			if (Dens.IsValidIndex(DIdx))
				WaterData.SolidCells[WIdx] = (Dens[DIdx] > 0.f);
		}
	}

	// ── Register water source positions detected during generation ────────────
	// OnChunkWaterReady is bound by AVoxelWorld so it can call SetSource() on the simulator.
	if (OnChunkWaterReady)
	{
		OnChunkWaterReady(CompletedTask->GetWaterSources());
	}

	// FIX: Reveal terrain mesh NOW — geometry + material are both fully ready.
	// Previously the mesh component was visible from spawn with no sections,
	// causing the invisible-hull pop visible before the player.
	ProceduralMesh->SetVisibility(true);

	// --- 💊 DEFER VISIBILITY FIX ---
	// Reveal the entire actor and trigger physics collision ONLY after 
	// all mesh buffers (procedural + foliage) are fully uploaded and safe.
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);

	bMeshApplied = true;
	bGenerating  = false;

	// Notify AVoxelWorld that this chunk finished so it can decrement ActiveGenerations.
	if (OnGenerationComplete)
	{
		OnGenerationComplete();
		OnGenerationComplete = nullptr;
	}
}

void AVoxelChunk::UploadSection(int32 SectionIndex, const FVoxelMeshData& Data, UMaterialInterface* Mat)
{
	if (Data.Vertices.Num() == 0 || !IsValid(ProceduralMesh)) return;

	// CreateMeshSection (FColor overload) correctly uploads the biome vertex colors
	// baked by FVoxelMeshGenerator. Using the LinearColor overload with an empty array
	// was silently discarding all vertex color data, making every biome look identical.
	ProceduralMesh->CreateMeshSection(
		SectionIndex,
		Data.Vertices,
		Data.Triangles,
		Data.Normals,
		Data.UVs,
		Data.VertexColors,
		Data.Tangents,
		LOD == 0  // OPTIMIZATION: Only build heavy collision mesh for highest-detail center chunks
	);


	if (Mat) ProceduralMesh->SetMaterial(SectionIndex, Mat);
}

void AVoxelChunk::DestroyAndRebuildMesh()
{
	GenerateAsync();
}

// ---------------------------------------------------------------------------
// Water mesh
// ---------------------------------------------------------------------------
void AVoxelChunk::RebuildWaterMesh()
{
	if (!IsValid(WaterMesh)) return;
	BuildWaterMeshInternal();
	WaterData.bMeshDirty = false;
}

void AVoxelChunk::BuildWaterMeshInternal()
{
	WaterMesh->ClearAllMeshSections();

	if (!WaterData.HasAnyWater())
	{
		WaterMesh->SetVisibility(false);
		return;
	}

	// We generate a simple flat-quad mesh for every water surface cell:
	// top face (air above), and side faces where the neighbour is air.
	TArray<FVector>   Vertices;
	TArray<int32>     Triangles;
	TArray<FVector>   Normals;
	TArray<FVector2D> UVs;

	const int32 CS = ChunkSize;
	const float VS = VoxelSize;
	const float SeaLevel = GenerationConfig.SeaLevel;
	const bool bEnableOcean = GenerationConfig.Water.bEnableOcean;

	// Helper: get water level at a local coord (clamped, 0 outside range)
	auto GetW = [&](int32 lx, int32 ly, int32 lz) -> uint8
	{
		if (lx < 0 || lx >= CS || ly < 0 || ly >= CS || lz < 0 || lz >= CS)
			return WATER_EMPTY;
		const int32 i = lx + ly * CS + lz * CS * CS;
		return WaterData.Cells[i];
	};

	auto IsSolidLocal = [&](int32 lx, int32 ly, int32 lz) -> bool
	{
		if (lx < 0 || lx >= CS || ly < 0 || ly >= CS || lz < 0 || lz >= CS)
			return false; // outside chunk = treat as air (neighbour chunk handles it)
		const int32 i = lx + ly * CS + lz * CS * CS;
		return WaterData.SolidCells.IsValidIndex(i) && WaterData.SolidCells[i];
	};

	auto EmitQuad = [&](FVector V0, FVector V1, FVector V2, FVector V3, FVector Normal)
	{
		const int32 Base = Vertices.Num();
		Vertices.Add(V0); Vertices.Add(V1); Vertices.Add(V2); Vertices.Add(V3);
		Normals.Add(Normal); Normals.Add(Normal); Normals.Add(Normal); Normals.Add(Normal);
		UVs.Add({0,0}); UVs.Add({1,0}); UVs.Add({1,1}); UVs.Add({0,1});
		Triangles.Add(Base+0); Triangles.Add(Base+1); Triangles.Add(Base+2);
		Triangles.Add(Base+0); Triangles.Add(Base+2); Triangles.Add(Base+3);
	};

	for (int32 lz = 0; lz < CS; ++lz)
	for (int32 ly = 0; ly < CS; ++ly)
	for (int32 lx = 0; lx < CS; ++lx)
	{
		uint8 Level = GetW(lx, ly, lz);

		// --- 🌊 INJECT OCEAN MASKING ---
		if (Level == WATER_EMPTY && bEnableOcean)
		{
			const float VoxelWorldZ = GetActorLocation().Z + lz * VS;
			const float AboveWorldZ = VoxelWorldZ + VS;

			// Only render ocean surface for the strata directly intersecting SeaLevel
			if (VoxelWorldZ <= SeaLevel && AboveWorldZ > SeaLevel)
			{
				Level = WATER_FULL; // Treat air below SeaLevel as surface point fluid
			}
		}

		if (Level == WATER_EMPTY) continue;

		const float WLevel  = (Level == WATER_SOURCE ? 1.f : (float)Level / (float)WATER_FULL);
		const float x0 = lx * VS;
		const float y0 = ly * VS;
		const float z0 = lz * VS;
		const float x1 = x0 + VS;
		const float y1 = y0 + VS;
		const float zTop = z0 + VS * WLevel; // water surface at fill fraction

		// TOP face — only if cell above is air/empty
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

	if (Vertices.Num() == 0)
	{
		WaterMesh->SetVisibility(false);
		return;
	}

	TArray<FVector>          EmptyNorms;
	TArray<FColor>           EmptyColors;
	TArray<FProcMeshTangent> EmptyTangents;

	WaterMesh->CreateMeshSection(
		0, Vertices, Triangles, Normals, UVs, EmptyColors, EmptyTangents, false);

	if (WaterMaterial)
		WaterMesh->SetMaterial(0, WaterMaterial);

	WaterMesh->SetVisibility(true);

	// --- 📜 LOGGER ---
	// Log procedural water volume metrics so we can verify generation amounts in Log files
	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelWater: Chunk %s emitted %d procedural water vertices (Ocean/Pools)"), 
		*ChunkCoord.ToString(), Vertices.Num()));
}

void AVoxelChunk::ClearMesh()
{
	++GenerationId;
	ProceduralMesh->ClearAllMeshSections();
	ProceduralMesh->SetVisibility(false); // hide until next ApplyMesh reveals it
	WaterMesh->ClearAllMeshSections();
	WaterMesh->SetVisibility(false);
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
	bMeshApplied = false;
}

#if WITH_EDITOR
void AVoxelChunk::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property)
	{
		GenerateSync();
	}
}
#endif

// OnMeshGenerated intentionally removed — the mesh ready callback is handled
// by OnGenerationComplete (the TFunction<void()> delegate) which AVoxelWorld binds.
// Keeping a dead stub here caused confusion about the correct notification path.

// ---------------------------------------------------------------------------
// Smooth LOD Transition System
// ---------------------------------------------------------------------------

void AVoxelChunk::TransitionToLOD(int32 NewLOD)
{
	if (NewLOD == LOD) return;
	
	// Store current mesh as previous for blending
	PreviousMesh = MeshOutput;
	TargetLOD = NewLOD;
	MeshState = EChunkMeshState::Transitioning;
	TransitionProgress = 0.0f;
	TransitionStartTime = GetWorld()->GetTimeSeconds();
	
	// Start generating new LOD mesh
	LOD = NewLOD;
	GenerateAsync();
}

void AVoxelChunk::UpdateMeshState()
{
	if (MeshState == EChunkMeshState::Transitioning)
	{
		float CurrentTime = GetWorld()->GetTimeSeconds();
		float Elapsed = CurrentTime - TransitionStartTime;
		TransitionProgress = FMath::Clamp(Elapsed / TransitionDuration, 0.0f, 1.0f);
		
		if (TransitionProgress >= 1.0f)
		{
			MeshState = EChunkMeshState::Ready;
			PreviousMesh.Reset();
		}
		else
		{
			// TODO: Implement mesh blending here when both meshes are available
			// For now, just ensure visibility is maintained
			SetMeshVisibility(true);
		}
	}
}

void AVoxelChunk::BlendMeshes(const FVoxelMeshOutput& From, const FVoxelMeshOutput& To, float Alpha)
{
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
	if (ProceduralMesh)
	{
		// Only change visibility if state allows it
		if (MeshState != EChunkMeshState::Empty && MeshState != EChunkMeshState::Error)
		{
			ProceduralMesh->SetVisibility(bVisible);
		}
	}
	
	if (WaterMesh)
	{
		WaterMesh->SetVisibility(bVisible);
	}
	
	// Update foliage visibility
	for (UInstancedStaticMeshComponent* HISM : BiomeFoliageHISMs)
	{
		if (HISM)
		{
			HISM->SetVisibility(bVisible);
		}
	}
	
	if (TreeHISM)
	{
		TreeHISM->SetVisibility(bVisible);
	}
	
	if (GrassHISM)
	{
		GrassHISM->SetVisibility(bVisible);
	}
}
