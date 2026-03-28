// VoxelChunk.cpp
//
// FIX-1  ClearMesh() no longer resets bUseAsyncCooking to true.
// FIX-2  ClearMesh() and CancelGeneration() increment WaterGeneration.
// FIX-3  UploadSection() explicitly clears empty sections (no ghost collision).
// FIX-4  Material warning booleans are instance members, reset in ClearMesh().
// FIX-5  BlendMeshes() removed (was never called).
// FIX-N2 WaterData.WaterCellCount now incremented when filling ocean WATER_SOURCE
//        voxels in ApplyMesh(). Previously the counter stayed at 0, making
//        HasAnyWater() always return false for voxel-ocean chunks even when
//        hundreds of WATER_SOURCE cells had been written.
// FIX RIM-3 bIsDistantHeightmesh forced false — LOD 2 now always runs Surface Nets
//           (GenerateMesh, StepSize=4). The old heightmap path produced a flat 2-D
//           surface that could not render vertical features (crater rim walls, cliffs).
//           StepSize=4 means 64× fewer voxels than LOD 0, so performance is similar
//           to the heightmap but with correct 3-D geometry.

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
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralMesh"));
	RootComponent  = ProceduralMesh;
	ProceduralMesh->SetCollisionProfileName(TEXT("BlockAll"));
	ProceduralMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ProceduralMesh->bUseComplexAsSimpleCollision = true;
	ProceduralMesh->bUseAsyncCooking = false; // FIX-1: permanently false
	ProceduralMesh->SetCastShadow(true);
	ProceduralMesh->SetCanEverAffectNavigation(true);
	ProceduralMesh->SetVisibility(false);
	// FIX BOUNDS-CULL: UE5 frustum-culls the entire component when the camera
	// moves far enough that the computed bounding box (derived from section 0 /
	// FlatMesh) no longer intersects the frustum. SlopeMesh (section 1) vertices
	// can extend slightly beyond that box, so slope faces disappear at distance
	// while flat faces remain visible. BoundsScale=2 doubles the box radius,
	// ensuring both sections stay inside the cull volume at all render distances.
	ProceduralMesh->SetBoundsScale(2.0f);


	WaterMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaterMesh"));
	WaterMesh->SetupAttachment(RootComponent);
	WaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WaterMesh->bUseComplexAsSimpleCollision = false;
	WaterMesh->bUseAsyncCooking = false;
	WaterMesh->SetCastShadow(false);
	WaterMesh->SetVisibility(false);

	TreeHISM  = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TreeHISM"));
	TreeHISM->SetupAttachment(RootComponent);
	GrassHISM = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GrassHISM"));
	GrassHISM->SetupAttachment(RootComponent);

	MeshState          = EChunkMeshState::Empty;
	TransitionProgress = 0.f;
	TransitionStartTime = 0.f;
}

void AVoxelChunk::BeginPlay()
{
	Super::BeginPlay();
	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelChunk: BeginPlay (%d,%d,%d)"), ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z));
	bMeshApplied = false;
	bGenerating  = false;
}

void AVoxelChunk::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bPendingLODTransition && IsReady() && !IsGenerating())
	{
		TransitionToLOD(PendingLOD.Load());
		bPendingLODTransition = false;
		PendingLOD.Store(0);
	}
	if (MeshState == EChunkMeshState::Transitioning)
		UpdateMeshState();
}

void AVoxelChunk::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelGeneration();
	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelChunk: EndPlay (%d,%d,%d)"), ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z));
	Super::EndPlay(EndPlayReason);
}

void AVoxelChunk::CancelGeneration()
{
	if (!bGenerating) return;
	
	TSharedPtr<FVoxelGeneratorTask> TaskToCancel;
	{
		FScopeLock Lock(&TaskLock);
		TaskToCancel = CurrentTask;
		CurrentTask.Reset();
	}
	
	if (TaskToCancel.IsValid()) TaskToCancel->Cancel();
	++WaterGeneration; // FIX-2
	++GenerationId;
	bGenerating = false;
	if (OnGenerationComplete)
	{
		auto CB = MoveTemp(OnGenerationComplete);
		OnGenerationComplete = nullptr;
		CB();
	}
}

void AVoxelChunk::Reset()
{
	CancelGeneration();
	ClearMesh();
	
	// FIX-PROXY: Recursive cleanup of physics state to prevent "Ghost Meshes"
	if (ProceduralMesh)
	{
		ProceduralMesh->ClearAllMeshSections();
		ProceduralMesh->RecreatePhysicsState();
	}
	if (WaterMesh)
	{
		WaterMesh->ClearAllMeshSections();
		WaterMesh->RecreatePhysicsState();
	}
}

void AVoxelChunk::GenerateAsync()
{
	if (bGenerating) return;
	
	// FIX-REENTRANCY: prevent infinite loop if callback triggers immediate rebuild
	static thread_local bool bInGenerateAsync = false;
	if (bInGenerateAsync) return;
	bInGenerateAsync = true;
	
	bGenerating  = true;
	bMeshApplied = false;

	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelChunk: GenerateAsync (%d,%d,%d)"), ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z));

	uint64 TaskId = ++GenerationId;

	IVoxelDensityProvider* Provider = DensityGenerator;
	if (!Provider) { static FVoxelDensityGenerator FBD; Provider = &FBD; }

	if (!DenseChunk.IsValid()) DenseChunk = MakeShared<FVoxelDensityChunk>();

	FVector CamPos = FVector::ZeroVector;
	if (GetWorld() && GetWorld()->GetFirstPlayerController())
	{
		if (APlayerCameraManager* CM = GetWorld()->GetFirstPlayerController()->PlayerCameraManager)
			CamPos = CM->GetCameraLocation();
	}

	TSharedPtr<FVoxelGeneratorTask> LocalTask;
	uint32 FrameNum = GFrameCounter;
	{
		FScopeLock Lock(&TaskLock);
		CurrentTask = MakeShared<FVoxelGeneratorTask>(
			ChunkCoord, GetActorLocation(), CamPos, ChunkSize, VoxelSize, GetStepSize(),
			GenerationConfig, Provider, FoliageDensity, MaxFoliageSlope, DataMap,
			FrameNum,
			false);
		LocalTask = CurrentTask;
	}
	TWeakObjectPtr<AVoxelChunk>     SafeThis(this);

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [SafeThis, LocalTask, TaskId]()
	{
		if (!SafeThis.IsValid() || LocalTask->IsCancelled()) return;
		LocalTask->Execute();
		AsyncTask(ENamedThreads::GameThread, [SafeThis, LocalTask, TaskId]()
		{
			if (SafeThis.IsValid() && TaskId == (uint64)SafeThis->GenerationId)
				SafeThis->ApplyMesh(LocalTask);
		});
	});
	
	bInGenerateAsync = false;
}

void AVoxelChunk::GenerateSync()
{
	static FVoxelDensityGenerator GSync;
	if (!DenseChunk.IsValid()) DenseChunk = MakeShared<FVoxelDensityChunk>();

	FVector CamPos = FVector::ZeroVector;
	if (GetWorld() && GetWorld()->GetFirstPlayerController())
	{
		if (APlayerCameraManager* CM = GetWorld()->GetFirstPlayerController()->PlayerCameraManager)
			CamPos = CM->GetCameraLocation();
	}

	TSharedPtr<FVoxelGeneratorTask> LocalTask;
	uint32 FrameNum = GFrameCounter;
	{
		FScopeLock Lock(&TaskLock);
		CurrentTask = MakeShared<FVoxelGeneratorTask>(
			ChunkCoord, GetActorLocation(), CamPos, ChunkSize, VoxelSize, GetStepSize(),
			GenerationConfig, &GSync, FoliageDensity, MaxFoliageSlope, DataMap,
			FrameNum,
			false); // FIX RIM-3: always Surface Nets, see GenerateAsync for rationale
		LocalTask = CurrentTask;
	}
	LocalTask->Execute();
	ApplyMesh(LocalTask);
}

void AVoxelChunk::ApplyMesh(TSharedPtr<FVoxelGeneratorTask> CompletedTask)
{
	if (!CompletedTask.IsValid()) return;

	const FVoxelMeshOutput& Out = CompletedTask->GetMeshOutput();
	MeshOutput = Out;

	if (!DenseChunk.IsValid()) DenseChunk = MakeShared<FVoxelDensityChunk>();
	DenseChunk->Densities = CompletedTask->GetDensities();

	// ── Material resolution ────────────────────────────────────────────────
	const float HalfChunk = ChunkSize * VoxelSize * 0.5f;
	const FVector ChunkCentre = GetActorLocation() + FVector(HalfChunk, HalfChunk, HalfChunk);
	const FVoxelBiomeWeightMap CW = FVoxelBiomeManager::GetBiomeWeightsStatic(ChunkCentre.X, ChunkCentre.Y, GenerationConfig);
	const EVoxelBiome Dom = CW.GetDominantBiome();
	const FVoxelBiomeRenderConfig& BR = GenerationConfig.GetBiomeRender(Dom);

	UMaterialInterface* FlatMat  = MasterFlatMaterial;
	UMaterialInterface* SlopeMat = MasterSlopeMaterial;
	if (BR.bEnableMaterialOverride && CW.GetWeight(Dom) >= BR.MaterialOverrideThreshold)
	{
		if (BR.FlatMaterialOverride)  FlatMat  = BR.FlatMaterialOverride.Get();
		if (BR.SlopeMaterialOverride) SlopeMat = BR.SlopeMaterialOverride.Get();
	}
	if (!FlatMat)  FlatMat  = MasterFlatMaterial;
	if (!SlopeMat) SlopeMat = MasterSlopeMaterial;

	// FIX-4: global warning flags to avoid 1400+ log lines
	if (!FlatMat)
	{
		FlatMat = UMaterial::GetDefaultMaterial(MD_Surface);
		if (!bFlatMaterialWarned)
		{
			bFlatMaterialWarned = true;
			UE_LOG(LogVoxelChunk, Warning, TEXT("VoxelChunk: MasterFlatMaterial unassigned — using default. Please assign it in the VoxelWorld Details Panel."));
		}
	}
	if (!SlopeMat)
	{
		SlopeMat = UMaterial::GetDefaultMaterial(MD_Surface);
		if (!bSlopeMaterialWarned)
		{
			bSlopeMaterialWarned = true;
			UE_LOG(LogVoxelChunk, Warning, TEXT("VoxelChunk: MasterSlopeMaterial unassigned — using default. Please assign it in the VoxelWorld Details Panel."));
		}
	}

	FString CS = FString::Printf(TEXT("%d_%d_%d"), ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z);
	UE_LOG(LogVoxelWorld, Warning,
		TEXT("VoxelChunk: ApplyMesh (%d,%d,%d) LOD=%d FlatV=%d SlopeV=%d FlatT=%d SlopeT=%d"),
		ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z,
		LOD, Out.FlatMesh.Vertices.Num(), Out.SlopeMesh.Vertices.Num(),
		Out.FlatMesh.Triangles.Num(), Out.SlopeMesh.Triangles.Num());

	UploadSection(0, Out.FlatMesh,  FlatMat,  TEXT("Flat"),  nullptr);
	UploadSection(1, Out.SlopeMesh, SlopeMat, TEXT("Slope"), nullptr);

	// Directive 2: Collision Decimation
	CreateCollisionForChunk(Out);

	// ── Per-biome foliage ─────────────────────────────────────────────────
	const TArray<TArray<FTransform>>& PFT = CompletedTask->GetPerFoliageTransforms();
	const TArray<UStaticMesh*>&       PFM = CompletedTask->GetPerFoliageMeshes();
	bool bPBF = false;
	for (int32 s=0; s<PFT.Num(); ++s)
		if (PFM.IsValidIndex(s) && PFM[s] && PFT[s].Num()>0) { bPBF=true; break; }

	if (bPBF)
	{
		for (int32 Slot=0; Slot<PFT.Num(); ++Slot)
		{
			UStaticMesh* SM = (Slot<PFM.Num()) ? PFM[Slot] : nullptr;
			if (!SM || PFT[Slot].Num()==0) continue;
			UInstancedStaticMeshComponent* HISM = nullptr;
			if (Slot < BiomeFoliageHISMs.Num()) { HISM=BiomeFoliageHISMs[Slot]; if (HISM) HISM->ClearInstances(); }
			else
			{
				HISM = NewObject<UInstancedStaticMeshComponent>(this, *FString::Printf(TEXT("BFoliage_%d"), BiomeFoliageHISMs.Num()));
				HISM->SetupAttachment(RootComponent);
				HISM->RegisterComponent();
				BiomeFoliageHISMs.Add(HISM);
			}
			if (HISM) { HISM->SetMobility(EComponentMobility::Movable); HISM->SetStaticMesh(SM); HISM->SetCullDistances(0,12000); HISM->AddInstances(PFT[Slot],false); }
		}
		for (int32 i=PFT.Num(); i<BiomeFoliageHISMs.Num(); ++i)
			if (BiomeFoliageHISMs[i]) { BiomeFoliageHISMs[i]->ClearInstances(); BiomeFoliageHISMs[i]->DestroyComponent(); }
		BiomeFoliageHISMs.SetNum(PFT.Num());
	}
	else
	{
		if (IsValid(TreeHISM))  TreeHISM->ClearInstances();
		if (IsValid(GrassHISM)) GrassHISM->ClearInstances();
		if (IsValid(TreeHISM)  && TreeMesh)  { TreeHISM->SetMobility(EComponentMobility::Movable);  TreeHISM->SetStaticMesh(TreeMesh);  TreeHISM->AddInstances(CompletedTask->GetTreeTransforms(),  false); }
		if (IsValid(GrassHISM) && GrassMesh) { GrassHISM->SetMobility(EComponentMobility::Movable); GrassHISM->SetStaticMesh(GrassMesh); GrassHISM->AddInstances(CompletedTask->GetGrassTransforms(), false); }
	}

	// ── Water data population ─────────────────────────────────────────────
	{
		WaterData.Init(ChunkSize);
		const TArray<float>& Dens = CompletedTask->GetDensities();
		const int32 EffCS  = ChunkSize / GetStepSize();
		const int32 S      = EffCS + 3;
		const int32 StepSz = GetStepSize();
		const FVector ChunkOrigin = GetActorLocation();

		// OPT-4: Consume pre-baked water column data from background task instead of
		// recomputing expensive biome noise here on the game thread.
		// Falls back to direct computation for distant LOD chunks (StepSize>1) where
		// ComputeWaterColumns() was skipped.
		const bool bHasPrebakedWater = CompletedTask->GetWaterColOceanWeights().Num() == ChunkSize * ChunkSize;
		TArray<float> OceanWeights;  OceanWeights.SetNumZeroed(ChunkSize * ChunkSize);
		TArray<float> CraterWeights; CraterWeights.SetNumZeroed(ChunkSize * ChunkSize);
		TArray<float> NeutralHeights; NeutralHeights.SetNumZeroed(ChunkSize * ChunkSize);
		TArray<float> SurfaceHeights; SurfaceHeights.SetNumZeroed(ChunkSize * ChunkSize);

		if (bHasPrebakedWater)
		{
			OceanWeights   = CompletedTask->GetWaterColOceanWeights();
			CraterWeights  = CompletedTask->GetWaterColCraterWeights();
			NeutralHeights = CompletedTask->GetWaterColNeutralHeights();
			SurfaceHeights = CompletedTask->GetWaterColSurfaceHeights();
		}
		else
		{
			// Fallback: compute now (distant/LOD2 chunks that skipped background pre-bake)
			for (int32 ly=0; ly<ChunkSize; ++ly)
			for (int32 lx=0; lx<ChunkSize; ++lx)
			{
				const float ColX = ChunkOrigin.X + lx * VoxelSize;
				const float ColY = ChunkOrigin.Y + ly * VoxelSize;
				float Temp = -999.f, Erosion = -999.f;
				const FVoxelBiomeWeightMap W = FVoxelBiomeManager::GetBiomeWeightsStatic(ColX, ColY, GenerationConfig, &Temp, &Erosion);
				const int32 ColIdx = lx + ly * ChunkSize;
				OceanWeights  [ColIdx] = W.GetWeight(EVoxelBiome::Ocean);
				CraterWeights [ColIdx] = W.GetWeight(EVoxelBiome::Craters);
				NeutralHeights[ColIdx] = FVoxelBiomeManager::GetNeutralSurfaceHeightStatic(ColX, ColY, GenerationConfig, Temp, Erosion);
				SurfaceHeights[ColIdx] = FVoxelBiomeManager::GetSurfaceHeightStatic(ColX, ColY, W, GenerationConfig, Temp, Erosion);
			}
		}

		for (int32 lz=0; lz<ChunkSize; ++lz)
		for (int32 ly=0; ly<ChunkSize; ++ly)
		for (int32 lx=0; lx<ChunkSize; ++lx)
		{
			const int32 ex   = FMath::Clamp(lx/StepSz, 0, EffCS-1);
			const int32 ey   = FMath::Clamp(ly/StepSz, 0, EffCS-1);
			const int32 ez   = FMath::Clamp(lz/StepSz, 0, EffCS-1);
			const int32 DIdx = (ex+1) + (ey+1)*S + (ez+1)*S*S;
			const int32 WIdx = lx + ly*ChunkSize + lz*ChunkSize*ChunkSize;
			if (!Dens.IsValidIndex(DIdx)) continue;

			const bool bSolid = (Dens[DIdx] > 0.f);
			WaterData.SolidCells[WIdx] = bSolid;

			const int32 ColIdx = lx + ly * ChunkSize;
			if (GenerationConfig.Water.bUseVoxelOcean && !bSolid)
			{
				const float WorldZ = ChunkOrigin.Z + lz * VoxelSize;
				if (WorldZ <= GenerationConfig.SeaLevel)
				{
					const float OceanWeight = OceanWeights[ColIdx];
					if (OceanWeight > 0.49f)
					{
						WaterData.Cells[WIdx] = WATER_SOURCE;
						WaterData.WaterCellCount++;
					}
				}
			}

			// Procedural Crater Lakes — independent of SeaLevel
			if (!bSolid && CraterWeights[ColIdx] > 0.5f)
			{
				const float WorldZ = ChunkOrigin.Z + lz * VoxelSize;
				const float NH = NeutralHeights[ColIdx];
				const float SH = SurfaceHeights[ColIdx];
				
				// Lake level is 85% of depth from bottom up to original ground level (NeutralH)
				const float TargetLakeZ = SH + (NH - SH) * 0.85f;
				if (WorldZ <= TargetLakeZ && WorldZ > SH + 150.f)
				{
					WaterData.Cells[WIdx] = WATER_SOURCE;
					WaterData.WaterCellCount++;
				}
			}
		}
	}

	// ── Water source callback ─────────────────────────────────────────────
	if (OnChunkWaterReady)
		OnChunkWaterReady(CompletedTask->GetWaterSources());

	// ── Finalise ──────────────────────────────────────────────────────────
	ProceduralMesh->SetVisibility(true);
	ProceduralMesh->UpdateBounds();
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);

	bMeshApplied = true;
	bGenerating  = false;
	MeshState    = EChunkMeshState::Ready;

	if (OnGenerationComplete)
	{
		auto CB = MoveTemp(OnGenerationComplete);
		OnGenerationComplete = nullptr;
		CB();
	}
}

void AVoxelChunk::UploadSection(int32 Idx, const FVoxelMeshData& Data,
                                UMaterialInterface* Mat, const FString& SectionName,
                                UProceduralMeshComponent* Target)
{
	UProceduralMeshComponent* M = Target ? Target : ProceduralMesh;
	if (!IsValid(M)) return;
	if (Data.Vertices.Num() == 0) { M->ClearMeshSection(Idx); return; } // FIX-3
	
	// For Sections 0 and 1 (Visual), we now disable collision in favor of the dedicated collision section (Section 2)
	const bool bCol = false; 
	
	M->ClearMeshSection(Idx); // Force instant PhysX buffer flush before rewrite
	M->CreateMeshSection(Idx, Data.Vertices, Data.Triangles, Data.Normals,
	                     Data.UVs, Data.VertexColors, Data.Tangents, bCol);
	if (Mat) M->SetMaterial(Idx, Mat);
}

void AVoxelChunk::CreateCollisionForChunk(const FVoxelMeshOutput& MeshData)
{
	if (!ProceduralMesh) return;

	// For LOD 0 and LOD 1, we use full-resolution collision.
	// For LOD 2+, we implement vertex clustering.
	if (LOD < 2)
	{
		// Merge Flat and Slope into a single collision section (Section 2)
		FVoxelMeshData Combined;
		Combined.Vertices = MeshData.FlatMesh.Vertices;
		Combined.Triangles = MeshData.FlatMesh.Triangles;
		
		int32 VertOffset = Combined.Vertices.Num();
		Combined.Vertices.Append(MeshData.SlopeMesh.Vertices);
		for (int32 Tri : MeshData.SlopeMesh.Triangles) Combined.Triangles.Add(Tri + VertOffset);

		ProceduralMesh->CreateMeshSection(2, Combined.Vertices, Combined.Triangles, TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), true);
		ProceduralMesh->SetMeshSectionVisible(2, false); // Invisible collision proxy
		return;
	}

	// LOD 2+ vertex clustering implementation
	FVoxelMeshData Decimated;
	TMap<FIntVector, int32> ClusteredMap;
	const float ClusterSize = VoxelSize * 2.0f; // Cluster into 2-voxel blocks
	const float InvCluster = 1.0f / ClusterSize;

	auto ProcessMesh = [&](const FVoxelMeshData& Src)
	{
		TArray<int32> Remap; Remap.SetNumUninitialized(Src.Vertices.Num());

		for (int32 i = 0; i < Src.Vertices.Num(); ++i)
		{
			FIntVector Key(
				FMath::FloorToInt(Src.Vertices[i].X * InvCluster),
				FMath::FloorToInt(Src.Vertices[i].Y * InvCluster),
				FMath::FloorToInt(Src.Vertices[i].Z * InvCluster)
			);

			if (int32* FoundIndex = ClusteredMap.Find(Key))
			{
				Remap[i] = *FoundIndex;
			}
			else
			{
				int32 NewIdx = Decimated.Vertices.Add(Src.Vertices[i]);
				ClusteredMap.Add(Key, NewIdx);
				Remap[i] = NewIdx;
			}
		}

		for (int32 i = 0; i < Src.Triangles.Num(); i += 3)
		{
			int32 i0 = Remap[Src.Triangles[i]];
			int32 i1 = Remap[Src.Triangles[i+1]];
			int32 i2 = Remap[Src.Triangles[i+2]];

			// Skip degenerate triangles
			if (i0 == i1 || i1 == i2 || i2 == i0) continue;

			Decimated.Triangles.Add(i0);
			Decimated.Triangles.Add(i1);
			Decimated.Triangles.Add(i2);
		}
	};

	ProcessMesh(MeshData.FlatMesh);
	ProcessMesh(MeshData.SlopeMesh);

	if (Decimated.Vertices.Num() > 0)
	{
		ProceduralMesh->CreateMeshSection(2, Decimated.Vertices, Decimated.Triangles, TArray<FVector>(), TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), true);
		ProceduralMesh->SetMeshSectionVisible(2, false);
		
		UE_LOG(LogVoxelChunk, Log, TEXT("Chunk (%d,%d,%d) Collision Decimated: %d -> %d vertices"), 
			ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, 
			MeshData.FlatMesh.Vertices.Num() + MeshData.SlopeMesh.Vertices.Num(), 
			Decimated.Vertices.Num());
	}
	else
	{
		ProceduralMesh->ClearMeshSection(2);
	}
}

void AVoxelChunk::DestroyAndRebuildMesh() { GenerateAsync(); }

// ── Water mesh ────────────────────────────────────────────────────────────────
void AVoxelChunk::RebuildWaterMesh()
{
	if (!IsValid(WaterMesh) || !WaterData.bMeshDirty) return;
	
	// FIX WATER-REDUN: Skip rebuild if cell data hasn't actually changed.
	// This avoids clearing sections and re-uploading identical GPU buffers
	// when the simulation is active but this chunk's content is static.
	const uint32 Hash = FCrc::MemCrc32(WaterData.Cells.GetData(), WaterData.Cells.Num() * sizeof(uint8));
	if (Hash == LastWaterMeshHash)
	{
		WaterData.bMeshDirty = false;
		return;
	}
	
	BuildWaterMeshInternal();
	LastWaterMeshHash = Hash;
	WaterData.bMeshDirty = false;
}

void AVoxelChunk::BuildWaterMeshInternal()
{
	WaterMesh->ClearAllMeshSections();
	if (!WaterMaterial || !WaterData.HasAnyWater()) { WaterMesh->SetVisibility(false); return; }

	TArray<FVector>   V; TArray<int32>     T;
	TArray<FVector>   N; TArray<FVector2D> U;

	const int32 CS = ChunkSize; const float VS = VoxelSize;

	auto GetW = [&](int32 lx,int32 ly,int32 lz)->uint8
	{
		if (lx<0||lx>=CS||ly<0||ly>=CS||lz<0||lz>=CS) return WATER_EMPTY;
		return WaterData.Cells[lx+ly*CS+lz*CS*CS];
	};
	auto IsSolidLocal = [&](int32 lx,int32 ly,int32 lz)->bool
	{
		if (lx<0||lx>=CS||ly<0||ly>=CS||lz<0||lz>=CS) return false;
		const int32 i=lx+ly*CS+lz*CS*CS;
		return WaterData.SolidCells.IsValidIndex(i) && WaterData.SolidCells[i];
	};
	auto Quad=[&](FVector v0,FVector v1,FVector v2,FVector v3,FVector Norm)
	{
		const int32 B=V.Num();
		V.Add(v0); V.Add(v1); V.Add(v2); V.Add(v3);
		N.Add(Norm); N.Add(Norm); N.Add(Norm); N.Add(Norm);
		U.Add({0,0}); U.Add({1,0}); U.Add({1,1}); U.Add({0,1});
		T.Add(B); T.Add(B+1); T.Add(B+2);
		T.Add(B); T.Add(B+2); T.Add(B+3);
	};

	for (int32 lz=0;lz<CS;++lz)
	for (int32 ly=0;ly<CS;++ly)
	for (int32 lx=0;lx<CS;++lx)
	{
		uint8 Level = GetW(lx, ly, lz);
		if (Level == WATER_EMPTY) continue;

		const float WL = (Level == WATER_SOURCE ? 1.f : (float)Level / (float)WATER_FULL);
		const float x0 = lx * VS, y0 = ly * VS, z0 = lz * VS;
		const float x1 = x0 + VS, y1 = y0 + VS, z1 = z0 + VS;
		const float zT = z0 + VS * WL;

		// 1. Top Face
		if (lz + 1 >= CS || (!IsSolidLocal(lx, ly, lz + 1) && GetW(lx, ly, lz + 1) == WATER_EMPTY))
		{
			Quad({x0, y0, zT}, {x1, y0, zT}, {x1, y1, zT}, {x0, y1, zT}, FVector::UpVector);
		}

		// 2. Right Face (+X)
		if (lx + 1 >= CS || (!IsSolidLocal(lx + 1, ly, lz) && GetW(lx + 1, ly, lz) == WATER_EMPTY))
		{
			Quad({x1, y0, z0}, {x1, y1, z0}, {x1, y1, zT}, {x1, y0, zT}, FVector::RightVector);
		}

		// 3. Left Face (-X)
		if (lx - 1 < 0 || (!IsSolidLocal(lx - 1, ly, lz) && GetW(lx - 1, ly, lz) == WATER_EMPTY))
		{
			Quad({x0, y1, z0}, {x0, y0, z0}, {x0, y0, zT}, {x0, y1, zT}, -FVector::RightVector);
		}

		// 4. Front Face (+Y)
		if (ly + 1 >= CS || (!IsSolidLocal(lx, ly + 1, lz) && GetW(lx, ly + 1, lz) == WATER_EMPTY))
		{
			Quad({x1, y1, z0}, {x0, y1, z0}, {x0, y1, zT}, {x1, y1, zT}, FVector::ForwardVector);
		}

		// 5. Back Face (-Y)
		if (ly - 1 < 0 || (!IsSolidLocal(lx, ly - 1, lz) && GetW(lx, ly - 1, lz) == WATER_EMPTY))
		{
			Quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, zT}, {x0, y0, zT}, -FVector::ForwardVector);
		}
	}

	if (V.Num()==0) { WaterMesh->SetVisibility(false); return; }
	TArray<FColor> EC; TArray<FProcMeshTangent> ET;
	WaterMesh->CreateMeshSection(0, V, T, N, U, EC, ET, false);
	if (WaterMaterial) WaterMesh->SetMaterial(0, WaterMaterial);
	WaterMesh->SetVisibility(true);
}

// ── ClearMesh ─────────────────────────────────────────────────────────────────
void AVoxelChunk::ClearMesh()
{
	++GenerationId;
	ProceduralMesh->ClearAllMeshSections();
	ProceduralMesh->SetVisibility(false);
	// FIX-1: do NOT reset bUseAsyncCooking
	WaterMesh->ClearAllMeshSections();
	WaterMesh->SetVisibility(false);
	++WaterGeneration; // FIX-2
	WaterData.Reset(); // also resets WaterCellCount to 0
	// Implement eviction policy: keep up to 8 foliage components, destroy the rest
	for (int32 i = BiomeFoliageHISMs.Num() - 1; i >= 0; --i)
	{
		UInstancedStaticMeshComponent* H = BiomeFoliageHISMs[i];
		if (IsValid(H))
		{
			H->ClearInstances();
			if (i >= 8) // Evict excess components
			{
				H->DestroyComponent();
				BiomeFoliageHISMs.RemoveAt(i);
			}
		}
		else
		{
			BiomeFoliageHISMs.RemoveAt(i);
		}
	}
	bFlatMaterialWarned  = false; // FIX-4
	bSlopeMaterialWarned = false;
	bMeshApplied = false;
	MeshState    = EChunkMeshState::Empty;
}

#if WITH_EDITOR
void AVoxelChunk::PostEditChangeProperty(FPropertyChangedEvent& E)
{
	Super::PostEditChangeProperty(E);
	if (E.Property) GenerateSync();
}
#endif

// ── LOD Transition ────────────────────────────────────────────────────────────
void AVoxelChunk::TransitionToLOD(int32 NewLOD)
{
	if (NewLOD==LOD) return;
	
	if (bGenerating)
	{
		// FIX-SILENTDROP: if we are busy, queue the transition for the next Tick
		bPendingLODTransition = true;
		PendingLOD.Store(NewLOD);
		return;
	}
	
	PreviousMesh        = MeshOutput;
	TargetLOD           = NewLOD;
	MeshState           = EChunkMeshState::Transitioning;
	TransitionProgress  = 0.f;
	TransitionStartTime = GetWorld()->GetTimeSeconds();
	LOD = NewLOD;
	SetActorTickEnabled(true);
	GenerateAsync();
}

void AVoxelChunk::UpdateMeshState()
{
	if (MeshState == EChunkMeshState::Transitioning)
	{
		const float T = GetWorld()->GetTimeSeconds();
		TransitionProgress = FMath::Clamp((T-TransitionStartTime)/TransitionDuration, 0.f, 1.f);
		SetMeshVisibility(true);
		if (TransitionProgress >= 1.f && MeshState == EChunkMeshState::Transitioning)
		{ TransitionStartTime=T; TransitionProgress=0.f; }
	}
	else { PreviousMesh.Reset(); SetActorTickEnabled(false); }
}

void AVoxelChunk::SetMeshVisibility(bool bV)
{
	if (ProceduralMesh)
	{
		const bool bHasGeo = (MeshState!=EChunkMeshState::Empty)&&(MeshState!=EChunkMeshState::Error);
		ProceduralMesh->SetVisibility(bV && bHasGeo);
	}
	if (WaterMesh) WaterMesh->SetVisibility(bV);
	for (UInstancedStaticMeshComponent* H : BiomeFoliageHISMs) if (H) H->SetVisibility(bV);
	if (TreeHISM)  TreeHISM->SetVisibility(bV);
	if (GrassHISM) GrassHISM->SetVisibility(bV);
}
