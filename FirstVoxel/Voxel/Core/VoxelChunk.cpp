#include "Core/VoxelChunk.h"
#include "FirstVoxel.h"
#include "CoreMinimal.h"
#include "Generation/VoxelMeshGenerator.h"
#include "Generation/VoxelDensityGenerator.h"
#include "Core/VoxelWorld.h"
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

	TreeHISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TreeHISM"));
	TreeHISM->SetupAttachment(RootComponent);

	GrassHISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GrassHISM"));
	GrassHISM->SetupAttachment(RootComponent);
}

void AVoxelChunk::BeginPlay()
{
	Super::BeginPlay();
}

void AVoxelChunk::CancelGeneration()
{
	// Cancel any in-flight task so shutdown/streaming doesn't hang waiting on completion.
	if (!bGenerating)
	{
		return;
	}

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

	// ── Determine per-biome material overrides ────────────────────────────
	// Sample the dominant biome at the chunk centre (cheap 2D noise).
	const FVector ChunkCentre = GetActorLocation()
		+ FVector(ChunkSize * VoxelSize * 0.5f, ChunkSize * VoxelSize * 0.5f, 0.f);
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
				HISM->SetStaticMesh(SlotMesh);
				// Static mobility is required for correct Lumen / static lighting baking.
				// Movable foliage disables static GI contribution and increases rendering cost.
				HISM->SetMobility(EComponentMobility::Static);
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
			TreeHISM->SetStaticMesh(TreeMesh);
			TreeHISM->AddInstances(TreeTransforms, false);
		}
		if (IsValid(GrassHISM) && GrassMesh)
		{
			GrassHISM->SetStaticMesh(GrassMesh);
			GrassHISM->AddInstances(GrassTransforms, false);
		}
	}

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
	ClearMesh();
	GenerateAsync();
}

void AVoxelChunk::ClearMesh()
{
	++GenerationId;
	ProceduralMesh->ClearAllMeshSections();
	// Destroy dynamic per-biome foliage components
	for (UInstancedStaticMeshComponent* HISM : BiomeFoliageHISMs)
		if (IsValid(HISM)) HISM->DestroyComponent();
	BiomeFoliageHISMs.Reset();
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
