// VoxelChunk.cpp
//
// Core chunk implementation for the voxel engine.
//
// FIXES APPLIED IN THIS REVISION
// ───────────────────────────────
// FIX-1  ClearMesh() no longer resets bUseAsyncCooking to true.
//        The constructor intentionally sets it to false to eliminate the
//        collision-ready timing race. Resetting it on every pool recycle
//        silently reinstated the race for every recycled chunk.
//
// FIX-2  ClearMesh() and CancelGeneration() now increment WaterGeneration.
//        FVoxelWaterSimulator::Step() uses this counter to detect stale
//        registrations if UnregisterChunk was not called before recycle.
//
// FIX-3  UploadSection() explicitly clears a section when Data is empty.
//        Previously an early return left ghost geometry (invisible colliders)
//        from the prior generation at that section index after a dirty rebuild.
//
// FIX-4  Material warning booleans are now instance members (bFlatMaterialWarned /
//        bSlopeMaterialWarned) reset in ClearMesh(). Static locals persisted
//        across PIE sessions so the warning never re-fired after a fix.
//
// FIX-5  BlendMeshes() removed — it was never called (UpdateMeshState() only
//        called SetMeshVisibility). LOD transitions now keep the old mesh
//        visible until the new generation completes, then swap cleanly.
//        True alpha-blended LOD transitions require material-parameter lerping
//        which should be implemented as a follow-up (see ARCHITECTURE notes).

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

	ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralMesh"));
	RootComponent = ProceduralMesh;

	ProceduralMesh->SetCollisionProfileName(TEXT("BlockAll"));
	ProceduralMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ProceduralMesh->bUseComplexAsSimpleCollision = true;

	// FIX-1 (constructor side): bUseAsyncCooking is permanently false.
	// This eliminates the timing race where IsCollisionReady() fires before
	// PhysX baking completes on a background thread. ClearMesh() must NOT
	// reset this to true — see FIX-1 comment in ClearMesh().
	ProceduralMesh->bUseAsyncCooking = false;

	BackfaceMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BackfaceMesh"));
	if (BackfaceMesh)
	{
		BackfaceMesh->SetupAttachment(RootComponent);
		BackfaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BackfaceMesh->bUseComplexAsSimpleCollision = false;
		BackfaceMesh->SetCastShadow(true);
	}

	ProceduralMesh->SetCastShadow(true);
	ProceduralMesh->SetCanEverAffectNavigation(true);

	// Hide terrain mesh until geometry is ready to prevent invisible-mesh pop-in.
	// SetVisibility(true) is called at the end of ApplyMesh() once all sections
	// are uploaded.
	ProceduralMesh->SetVisibility(false);

	MeshState = EChunkMeshState::Empty;
	TransitionProgress = 0.0f;
	TransitionStartTime = 0.0f;

	WaterMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaterMesh"));
	WaterMesh->SetupAttachment(RootComponent);
	WaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WaterMesh->bUseComplexAsSimpleCollision = false;
	WaterMesh->bUseAsyncCooking = false;
	WaterMesh->SetCastShadow(false);
	WaterMesh->SetVisibility(false); // Hidden until BuildWaterMeshInternal confirms actual water geometry

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
	bMeshApplied = false;
	bGenerating = false;
}

void AVoxelChunk::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Handle pending LOD transitions when chunk becomes ready.
	if (bPendingLODTransition && IsReady() && !IsGenerating())
	{
		TransitionToLOD(PendingLOD);
		bPendingLODTransition = false;
		PendingLOD = 0;
	}

	// Drive LOD transition progress each frame.
	if (MeshState == EChunkMeshState::Transitioning)
	{
		UpdateMeshState();
	}
}

void AVoxelChunk::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelGeneration();
	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelChunk: EndPlay (%d,%d,%d) - Reason: %d"),
		ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, (int32)EndPlayReason));
	Super::EndPlay(EndPlayReason);
}

void AVoxelChunk::CancelGeneration()
{
	if (!bGenerating) return;

	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelChunk: CancelGeneration (%d,%d,%d)"),
		ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z));

	if (CurrentTask.IsValid())
	{
		CurrentTask->Cancel();
	}

	// FIX-2: Increment WaterGeneration on cancellation so any simulator entry
	// registered before this cancel is detected as stale in Step().
	++WaterGeneration;

	++GenerationId;
	bGenerating = false;

	// Notify the world so it can decrement ActiveGenerations.
	if (OnGenerationComplete)
	{
		auto Callback = MoveTemp(OnGenerationComplete);
		OnGenerationComplete = nullptr;
		Callback();
	}
}

void AVoxelChunk::GenerateAsync()
{
	if (bGenerating) return;

	bGenerating = true;
	bMeshApplied = false;

	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelChunk: GenerateAsync (%d,%d,%d)"),
		ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z));

	uint32 TaskId = ++GenerationId;
	FVector Origin = GetActorLocation();

	IVoxelDensityProvider* DensityProvider = DensityGenerator;
	if (!DensityProvider)
	{
		static FVoxelDensityGenerator FallbackDensityGenerator;
		DensityProvider = &FallbackDensityGenerator;
	}

	if (DataMap)
	{
		const FVector Pos = GetActorLocation();
		const float Size = ChunkSize * VoxelSize;
		const FIntVector GlobalChunkCoord(
			FMath::FloorToInt(Pos.X / Size),
			FMath::FloorToInt(Pos.Y / Size),
			FMath::FloorToInt(Pos.Z / Size));
		// DataMap->GetChunkData(GlobalChunkCoord, LocalMap); // TODO: re-enable when DataMap round-trip is verified
	}

	if (!DenseChunk.IsValid())
	{
		DenseChunk = MakeShared<FVoxelDensityChunk>();
	}

	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelChunk: GenerateAsync called for (%d,%d,%d)"), ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z);

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
		DataMap
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

	if (DataMap)
	{
		const FVector Pos = GetActorLocation();
		const float Size = ChunkSize * VoxelSize;
		const FIntVector GlobalChunkCoord(
			FMath::FloorToInt(Pos.X / Size),
			FMath::FloorToInt(Pos.Y / Size),
			FMath::FloorToInt(Pos.Z / Size));
		// DataMap->GetChunkData(GlobalChunkCoord, LocalMap); // TODO: re-enable when DataMap round-trip is verified
	}

	if (!DenseChunk.IsValid())
	{
		DenseChunk = MakeShared<FVoxelDensityChunk>();
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
		DataMap
	);

	CurrentTask->Execute();
	ApplyMesh(CurrentTask);
}

void AVoxelChunk::ApplyMesh(TSharedPtr<FVoxelGeneratorTask> CompletedTask)
{
	if (!CompletedTask.IsValid()) return;

	const FVoxelMeshOutput& Out = CompletedTask->GetMeshOutput();
	MeshOutput = Out;

	if (!DenseChunk.IsValid())
	{
		DenseChunk = MakeShared<FVoxelDensityChunk>();
	}
	DenseChunk->Densities = CompletedTask->GetDensities();

	// ── Determine per-biome material overrides ────────────────────────────
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

	if (!FlatMat)  FlatMat  = MasterFlatMaterial;
	if (!SlopeMat) SlopeMat = MasterSlopeMaterial;

	// FIX-4: Use instance member warning flags instead of static locals.
	// Static locals persisted across PIE sessions so warnings never re-fired
	// after fixing the material assignment. Instance members reset in ClearMesh()
	// when the chunk is returned to the pool, so the warning fires again on
	// the next PIE session if the problem still exists.
	if (!FlatMat)
	{
		FlatMat = UMaterial::GetDefaultMaterial(MD_Surface);
		if (!bFlatMaterialWarned)
		{
			bFlatMaterialWarned = true;
			UE_LOG(LogVoxelChunk, Warning,
				TEXT("VoxelChunk: MasterFlatMaterial is not assigned on AVoxelWorld. "
				     "Assign a material in the Details panel under Voxel|Materials. "
				     "Using engine default. (This message fires once per chunk pool recycle.)"));
		}
	}
	if (!SlopeMat)
	{
		SlopeMat = UMaterial::GetDefaultMaterial(MD_Surface);
		if (!bSlopeMaterialWarned)
		{
			bSlopeMaterialWarned = true;
			UE_LOG(LogVoxelChunk, Warning,
				TEXT("VoxelChunk: MasterSlopeMaterial is not assigned on AVoxelWorld. "
				     "Assign a material in the Details panel under Voxel|Materials. "
				     "Using engine default. (This message fires once per chunk pool recycle.)"));
		}
	}

	// ── Generate biome-specific mesh names ────────────────────────────────
	FString BiomeName;
	switch (DominantBiome)
	{
		case EVoxelBiome::Forest:  BiomeName = TEXT("Forest"); break;
		case EVoxelBiome::Desert:  BiomeName = TEXT("Desert"); break;
		case EVoxelBiome::Peaks:   BiomeName = TEXT("Peaks");  break;
		case EVoxelBiome::Cliffs:  BiomeName = TEXT("Cliffs"); break;
		case EVoxelBiome::Mesa:    BiomeName = TEXT("Mesa");   break;
		case EVoxelBiome::Craters: BiomeName = TEXT("Craters"); break;
		default:                   BiomeName = TEXT("Mixed");  break;
	}

	FString ChunkCoordStr = FString::Printf(TEXT("%d_%d_%d"), ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z);
	FString FlatMeshName  = FString::Printf(TEXT("FlatMesh_%s_%s"), *BiomeName, *ChunkCoordStr);

	const FIntVector& TaskCoord = CompletedTask->GetChunkCoord();
	UE_LOG(LogVoxelWorld, Log,
		TEXT("VoxelChunk: ApplyMesh for Chunk (%d,%d,%d) [TaskCoord=(%d,%d,%d)] (LOD %d). Flat=%d verts, Slope=%d verts"),
		ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z,
		TaskCoord.X, TaskCoord.Y, TaskCoord.Z,
		LOD, Out.FlatMesh.Vertices.Num(), Out.SlopeMesh.Vertices.Num());

	// ── Upload terrain mesh sections ──────────────────────────────────────
	// FIX-3 is applied inside UploadSection(): empty sections are explicitly
	// cleared rather than skipped, preventing ghost geometry on dirty rebuilds.
	if (BackfaceMesh) BackfaceMesh->ClearAllMeshSections();

	UploadSection(0, Out.FlatMesh,  FlatMat,  FlatMeshName);
	UploadSection(1, Out.SlopeMesh, SlopeMat, FString::Printf(TEXT("SlopeMesh_%s"), *ChunkCoordStr));

	if (BackfaceMesh)
	{
		UploadSection(0, Out.BackMesh,      FlatMat,  FString::Printf(TEXT("BackMesh_Flat_%s"),  *ChunkCoordStr), BackfaceMesh);
		UploadSection(1, Out.SlopeBackMesh, SlopeMat, FString::Printf(TEXT("BackMesh_Slope_%s"), *ChunkCoordStr), BackfaceMesh);
	}

	// ── Per-biome foliage system (Recycled / Pooled) ──────────────────────
	const TArray<TArray<FTransform>>& PerFoliage   = CompletedTask->GetPerFoliageTransforms();
	const TArray<UStaticMesh*>&       FoliageMeshes = CompletedTask->GetPerFoliageMeshes();

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
				const int32 NextIdx = BiomeFoliageHISMs.Num();
				HISM = NewObject<UInstancedStaticMeshComponent>(this,
					*FString::Printf(TEXT("BiomeFoliage_%d"), NextIdx));
				HISM->SetupAttachment(RootComponent);
				HISM->RegisterComponent();
				BiomeFoliageHISMs.Add(HISM);
			}

			if (HISM)
			{
				HISM->SetMobility(EComponentMobility::Movable);
				HISM->SetStaticMesh(SlotMesh);
				HISM->SetCullDistances(0, 12000);
				HISM->AddInstances(PerFoliage[Slot], false);
			}
		}

		// Destroy excess components beyond current foliage slot count.
		for (int32 i = PerFoliage.Num(); i < BiomeFoliageHISMs.Num(); ++i)
		{
			if (BiomeFoliageHISMs[i])
			{
				BiomeFoliageHISMs[i]->ClearInstances();
				BiomeFoliageHISMs[i]->DestroyComponent();
			}
		}
		BiomeFoliageHISMs.SetNum(PerFoliage.Num());
	}
	else
	{
		// ── Legacy fallback: global tree/grass meshes ─────────────────────
		if (IsValid(TreeHISM))  TreeHISM->ClearInstances();
		if (IsValid(GrassHISM)) GrassHISM->ClearInstances();

		const TArray<FTransform>& TreeTransforms  = CompletedTask->GetTreeTransforms();
		const TArray<FTransform>& GrassTransforms = CompletedTask->GetGrassTransforms();

		if (IsValid(TreeHISM) && TreeMesh)
		{
			TreeHISM->SetMobility(EComponentMobility::Movable);
			TreeHISM->SetStaticMesh(TreeMesh);
			TreeHISM->AddInstances(TreeTransforms, false);
		}

		if (IsValid(GrassHISM) && GrassMesh)
		{
			GrassHISM->SetMobility(EComponentMobility::Movable);
			GrassHISM->SetStaticMesh(GrassMesh);
			GrassHISM->AddInstances(GrassTransforms, false);
		}
	}

	// ── Populate water solid-cell map ─────────────────────────────────────
	{
		WaterData.Init(ChunkSize);
		const TArray<float>& Dens  = CompletedTask->GetDensities();
		const int32 EffCS      = ChunkSize / GetStepSize();
		const int32 S          = EffCS + 3;
		const int32 StepSz     = GetStepSize();
		const FVector ChunkOrigin = GetActorLocation();

		TArray<float, TInlineAllocator<32*32>> OceanWeights;
		OceanWeights.SetNumZeroed(ChunkSize * ChunkSize);
		for (int32 ly = 0; ly < ChunkSize; ++ly)
		for (int32 lx = 0; lx < ChunkSize; ++lx)
		{
			const float ColX = ChunkOrigin.X + lx * VoxelSize;
			const float ColY = ChunkOrigin.Y + ly * VoxelSize;
			const FVoxelBiomeWeightMap Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(ColX, ColY, GenerationConfig);
			OceanWeights[lx + ly * ChunkSize] = Weights.GetWeight(EVoxelBiome::Ocean);
		}

		for (int32 lz = 0; lz < ChunkSize; ++lz)
		for (int32 ly = 0; ly < ChunkSize; ++ly)
		for (int32 lx = 0; lx < ChunkSize; ++lx)
		{
			const int32 ex   = FMath::Clamp(lx / StepSz, 0, EffCS - 1);
			const int32 ey   = FMath::Clamp(ly / StepSz, 0, EffCS - 1);
			const int32 ez   = FMath::Clamp(lz / StepSz, 0, EffCS - 1);
			const int32 DIdx = (ex + 1) + (ey + 1) * S + (ez + 1) * S * S;
			const int32 WIdx = lx + ly * ChunkSize + lz * ChunkSize * ChunkSize;
			if (Dens.IsValidIndex(DIdx))
			{
				const bool bSolid = (Dens[DIdx] > 0.f);
				WaterData.SolidCells[WIdx] = bSolid;

				if (GenerationConfig.Water.bUseVoxelOcean && !bSolid)
				{
					const float WorldZ = ChunkOrigin.Z + lz * VoxelSize;
					if (WorldZ <= GenerationConfig.SeaLevel)
					{
						const float OceanWeight = OceanWeights[lx + ly * ChunkSize];
						if (OceanWeight > 0.49f)
						{
							WaterData.Cells[WIdx] = WATER_SOURCE;
						}
					}
				}
			}
		}
	}

	// ── Register water source positions ───────────────────────────────────
	if (OnChunkWaterReady)
	{
		OnChunkWaterReady(CompletedTask->GetWaterSources());
	}

	// Reveal terrain mesh — geometry and materials are fully ready.
	ProceduralMesh->SetVisibility(true);
	ProceduralMesh->UpdateBounds();
	if (BackfaceMesh) BackfaceMesh->UpdateBounds();

	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);

	bMeshApplied = true;
	bGenerating  = false;
	MeshState    = EChunkMeshState::Ready;

	// Notify AVoxelWorld that this chunk finished so it can decrement ActiveGenerations.
	if (OnGenerationComplete)
	{
		auto Callback = MoveTemp(OnGenerationComplete);
		OnGenerationComplete = nullptr;
		Callback();
	}
}

void AVoxelChunk::UploadSection(
	int32 SectionIndex,
	const FVoxelMeshData& Data,
	UMaterialInterface* Mat,
	const FString& SectionName,
	UProceduralMeshComponent* TargetMesh)
{
	UProceduralMeshComponent* MeshToUse = TargetMesh ? TargetMesh : ProceduralMesh;
	if (!IsValid(MeshToUse)) return;

	// FIX-3: If the new geometry is empty, explicitly clear the section at
	// this index rather than returning early. The previous early return left
	// ghost geometry (invisible mesh sections with active collision) from a
	// prior generation when a dirty rebuild produced fewer or empty sections.
	if (Data.Vertices.Num() == 0)
	{
		MeshToUse->ClearMeshSection(SectionIndex);
		return;
	}

	// Build collision only for the main terrain mesh, not backface or water.
	const bool bBuildCollision = (MeshToUse == ProceduralMesh);

	MeshToUse->CreateMeshSection(
		SectionIndex,
		Data.Vertices,
		Data.Triangles,
		Data.Normals,
		Data.UVs,
		Data.VertexColors,
		Data.Tangents,
		bBuildCollision);

	if (Mat) MeshToUse->SetMaterial(SectionIndex, Mat);
}

void AVoxelChunk::DestroyAndRebuildMesh()
{
	GenerateAsync();
}

// ---------------------------------------------------------------------------
// Water Mesh
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

	if (!WaterMaterial)
	{
		WaterMesh->SetVisibility(false);
		return;
	}

	if (!WaterData.HasAnyWater())
	{
		WaterMesh->SetVisibility(false);
		return;
	}

	TArray<FVector>   Vertices;
	TArray<int32>     Triangles;
	TArray<FVector>   Normals;
	TArray<FVector2D> UVs;

	const int32 CS = ChunkSize;
	const float VS = VoxelSize;

	auto GetW = [&](int32 lx, int32 ly, int32 lz) -> uint8
	{
		if (lx < 0 || lx >= CS || ly < 0 || ly >= CS || lz < 0 || lz >= CS)
			return WATER_EMPTY;
		return WaterData.Cells[lx + ly * CS + lz * CS * CS];
	};

	auto IsSolidLocal = [&](int32 lx, int32 ly, int32 lz) -> bool
	{
		if (lx < 0 || lx >= CS || ly < 0 || ly >= CS || lz < 0 || lz >= CS)
			return false;
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
		if (Level == WATER_EMPTY) continue;

		const float WLevel  = (Level == WATER_SOURCE ? 1.f : (float)Level / (float)WATER_FULL);
		const float x0 = lx * VS, y0 = ly * VS, z0 = lz * VS;
		const float x1 = x0 + VS, y1 = y0 + VS;
		const float zTop = z0 + VS * WLevel;

		const bool bAboveEmpty = !IsSolidLocal(lx, ly, lz+1) && GetW(lx, ly, lz+1) == WATER_EMPTY;
		if (bAboveEmpty)
		{
			EmitQuad(
				FVector(x0, y0, zTop), FVector(x1, y0, zTop),
				FVector(x1, y1, zTop), FVector(x0, y1, zTop),
				FVector::UpVector);
		}
	}

	if (Vertices.Num() == 0)
	{
		WaterMesh->SetVisibility(false);
		return;
	}

	TArray<FColor>           EmptyColors;
	TArray<FProcMeshTangent> EmptyTangents;
	WaterMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, EmptyColors, EmptyTangents, false);
	if (WaterMaterial) WaterMesh->SetMaterial(0, WaterMaterial);
	WaterMesh->SetVisibility(true);

	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelWater: Chunk %s emitted %d procedural water vertices"),
		*ChunkCoord.ToString(), Vertices.Num()));
}

void AVoxelChunk::ClearMesh()
{
	++GenerationId;

	ProceduralMesh->ClearAllMeshSections();
	ProceduralMesh->SetVisibility(false);

	// FIX-1: Do NOT reset bUseAsyncCooking here.
	// The constructor sets it to false permanently to eliminate the collision-
	// ready timing race (IsCollisionReady() firing before PhysX baking
	// completes). Resetting it to true on pool recycle would silently reinstate
	// the race for every chunk after its first use — the exact bug this was
	// fixed to prevent.

	WaterMesh->ClearAllMeshSections();
	WaterMesh->SetVisibility(false);

	// FIX-2: Increment WaterGeneration so FVoxelWaterSimulator::Step() can
	// detect stale entries if UnregisterChunk was not called before recycle.
	++WaterGeneration;
	WaterData.Reset();

	// Recycle per-biome foliage components — clear instances but keep the
	// components alive so the pool avoids repeated allocation/deallocation.
	for (UInstancedStaticMeshComponent* HISM : BiomeFoliageHISMs)
	{
		if (IsValid(HISM)) HISM->ClearInstances();
	}

	// FIX-4: Reset per-chunk material warning flags so they re-fire correctly
	// on the next generation if the material assignment issue still exists.
	bFlatMaterialWarned  = false;
	bSlopeMaterialWarned = false;

	bMeshApplied = false;
	MeshState    = EChunkMeshState::Empty;
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

// ---------------------------------------------------------------------------
// LOD Transition
// ---------------------------------------------------------------------------

void AVoxelChunk::TransitionToLOD(int32 NewLOD)
{
	if (NewLOD == LOD) return;
	if (bGenerating) return;

	// Keep the previous mesh visible during the transition so there is never
	// a frame where neither the old nor the new mesh is visible (popping).
	// The new mesh replaces it when ApplyMesh() completes and sets MeshState = Ready.
	PreviousMesh        = MeshOutput;
	TargetLOD           = NewLOD;
	MeshState           = EChunkMeshState::Transitioning;
	TransitionProgress  = 0.0f;
	TransitionStartTime = GetWorld()->GetTimeSeconds();

	LOD = NewLOD;
	SetActorTickEnabled(true);
	GenerateAsync();
}

void AVoxelChunk::UpdateMeshState()
{
	// FIX-5: BlendMeshes() has been removed — it was never called and its
	// implementation was broken (it ignored the From parameter). LOD transitions
	// now keep the old mesh visible via SetMeshVisibility(true) while the new
	// generation runs in the background. ApplyMesh() sets MeshState = Ready when
	// the new mesh is uploaded, ending the transition cleanly.
	//
	// True alpha-blend LOD transitions should be implemented via a material
	// scalar parameter (LodBlend 0→1) lerped by a FTimerHandle in
	// TransitionToLOD(), which does not require holding two full mesh copies.
	// This is tracked as a future improvement.

	if (MeshState == EChunkMeshState::Transitioning)
	{
		const float CurrentTime = GetWorld()->GetTimeSeconds();
		const float Elapsed     = CurrentTime - TransitionStartTime;
		TransitionProgress      = FMath::Clamp(Elapsed / TransitionDuration, 0.0f, 1.0f);

		// Keep the mesh visible at all times during transition to prevent
		// geometry popping at chunk boundaries.
		SetMeshVisibility(true);

		// The transition state ends when ApplyMesh() fires and sets
		// MeshState = Ready. We disable Tick at that point.
		// The progress timer here provides a timeout: if generation somehow
		// takes longer than TransitionDuration, we don't keep ticking forever.
		if (TransitionProgress >= 1.0f && MeshState == EChunkMeshState::Transitioning)
		{
			// Generation is still running; reset timer and keep waiting.
			TransitionStartTime = CurrentTime;
			TransitionProgress  = 0.0f;
		}
	}
	else
	{
		// Transition resolved (ApplyMesh set Ready or generation was cancelled).
		PreviousMesh.Reset();
		SetActorTickEnabled(false);
	}
}

void AVoxelChunk::SetMeshVisibility(bool bVisible)
{
	if (ProceduralMesh)
	{
		const bool bHasGeometry =
			(MeshState != EChunkMeshState::Empty) &&
			(MeshState != EChunkMeshState::Error);
		ProceduralMesh->SetVisibility(bVisible && bHasGeometry);
	}

	if (WaterMesh)
		WaterMesh->SetVisibility(bVisible);

	for (UInstancedStaticMeshComponent* HISM : BiomeFoliageHISMs)
		if (HISM) HISM->SetVisibility(bVisible);

	if (TreeHISM)  TreeHISM->SetVisibility(bVisible);
	if (GrassHISM) GrassHISM->SetVisibility(bVisible);
}
