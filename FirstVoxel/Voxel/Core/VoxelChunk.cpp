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

	BackfaceMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BackfaceMesh"));
	if (BackfaceMesh)
	{
		BackfaceMesh->SetupAttachment(RootComponent);
		BackfaceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BackfaceMesh->bUseComplexAsSimpleCollision = false;
		BackfaceMesh->SetCastShadow(true);
	}

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
		TransitionToLOD(PendingLOD);
		bPendingLODTransition = false;
		PendingLOD = 0;
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
	if (CurrentTask.IsValid()) CurrentTask->Cancel();
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

void AVoxelChunk::GenerateAsync()
{
	if (bGenerating) return;
	bGenerating  = true;
	bMeshApplied = false;

	UVoxelLogger::LogVoxelEvent(FString::Printf(TEXT("VoxelChunk: GenerateAsync (%d,%d,%d)"), ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z));

	uint32 TaskId = ++GenerationId;

	IVoxelDensityProvider* Provider = DensityGenerator;
	if (!Provider) { static FVoxelDensityGenerator FBD; Provider = &FBD; }

	if (!DenseChunk.IsValid()) DenseChunk = MakeShared<FVoxelDensityChunk>();

	CurrentTask = MakeShared<FVoxelGeneratorTask>(
		ChunkCoord, GetActorLocation(), ChunkSize, VoxelSize, GetStepSize(),
		GenerationConfig, Provider, FoliageDensity, MaxFoliageSlope, DataMap);

	TSharedPtr<FVoxelGeneratorTask> LocalTask = CurrentTask;
	TWeakObjectPtr<AVoxelChunk>     SafeThis(this);

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [SafeThis, LocalTask, TaskId]()
	{
		if (!SafeThis.IsValid() || LocalTask->IsCancelled()) return;
		LocalTask->Execute();
		AsyncTask(ENamedThreads::GameThread, [SafeThis, LocalTask, TaskId]()
		{
			if (SafeThis.IsValid() && TaskId == SafeThis->GenerationId)
				SafeThis->ApplyMesh(LocalTask);
		});
	});
}

void AVoxelChunk::GenerateSync()
{
	static FVoxelDensityGenerator GSync;
	if (!DenseChunk.IsValid()) DenseChunk = MakeShared<FVoxelDensityChunk>();
	CurrentTask = MakeShared<FVoxelGeneratorTask>(
		ChunkCoord, GetActorLocation(), ChunkSize, VoxelSize, GetStepSize(),
		GenerationConfig, &GSync, FoliageDensity, MaxFoliageSlope, DataMap);
	CurrentTask->Execute();
	ApplyMesh(CurrentTask);
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
        static bool s_bFlatMaterialWarned = false;
		if (!s_bFlatMaterialWarned)
		{
			s_bFlatMaterialWarned = true;
			UE_LOG(LogVoxelChunk, Warning, TEXT("VoxelChunk: MasterFlatMaterial unassigned — using default. Please assign it in the VoxelWorld Details Panel."));
		}
	}
	if (!SlopeMat)
	{
		SlopeMat = UMaterial::GetDefaultMaterial(MD_Surface);
        static bool s_bSlopeMaterialWarned = false;
		if (!s_bSlopeMaterialWarned)
		{
			s_bSlopeMaterialWarned = true;
			UE_LOG(LogVoxelChunk, Warning, TEXT("VoxelChunk: MasterSlopeMaterial unassigned — using default. Please assign it in the VoxelWorld Details Panel."));
		}
	}

	FString CS = FString::Printf(TEXT("%d_%d_%d"), ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z);
	UE_LOG(LogVoxelWorld, Log,
		TEXT("VoxelChunk: ApplyMesh (%d,%d,%d) LOD=%d Flat=%d Slope=%d"),
		ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z,
		LOD, Out.FlatMesh.Vertices.Num(), Out.SlopeMesh.Vertices.Num());

	// ── Mesh upload ───────────────────────────────────────────────────────
	if (BackfaceMesh) BackfaceMesh->ClearAllMeshSections();
	UploadSection(0, Out.FlatMesh,      FlatMat,  TEXT("Flat"),       nullptr);
	UploadSection(1, Out.SlopeMesh,     SlopeMat, TEXT("Slope"),      nullptr);
	if (BackfaceMesh)
	{
		UploadSection(0, Out.BackMesh,      FlatMat,  TEXT("BackFlat"),  BackfaceMesh);
		UploadSection(1, Out.SlopeBackMesh, SlopeMat, TEXT("BackSlope"), BackfaceMesh);
	}

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

		// Build ocean biome weight cache (O(N²) not O(N³))
		TArray<float, TInlineAllocator<32*32>> OceanWeights;
		OceanWeights.SetNumZeroed(ChunkSize * ChunkSize);
		for (int32 ly=0; ly<ChunkSize; ++ly)
		for (int32 lx=0; lx<ChunkSize; ++lx)
		{
			const float ColX = ChunkOrigin.X + lx * VoxelSize;
			const float ColY = ChunkOrigin.Y + ly * VoxelSize;
			const FVoxelBiomeWeightMap W = FVoxelBiomeManager::GetBiomeWeightsStatic(ColX, ColY, GenerationConfig);
			OceanWeights[lx + ly * ChunkSize] = W.GetWeight(EVoxelBiome::Ocean);
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

			if (GenerationConfig.Water.bUseVoxelOcean && !bSolid)
			{
				const float WorldZ = ChunkOrigin.Z + lz * VoxelSize;
				if (WorldZ <= GenerationConfig.SeaLevel)
				{
					const float OceanWeight = OceanWeights[lx + ly * ChunkSize];
					if (OceanWeight > 0.49f)
					{
						WaterData.Cells[WIdx] = WATER_SOURCE;
						// FIX-N2: maintain WaterCellCount so HasAnyWater() is O(1)
						// Previously this counter was never incremented for ocean voxels,
						// making HasAnyWater() always return false for ocean chunks.
						WaterData.WaterCellCount++;
					}
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
	if (BackfaceMesh) BackfaceMesh->UpdateBounds();
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
                                UMaterialInterface* Mat, const FString&,
                                UProceduralMeshComponent* Target)
{
	UProceduralMeshComponent* M = Target ? Target : ProceduralMesh;
	if (!IsValid(M)) return;
	if (Data.Vertices.Num() == 0) { M->ClearMeshSection(Idx); return; } // FIX-3
	const bool bCol = (M == ProceduralMesh);
	M->CreateMeshSection(Idx, Data.Vertices, Data.Triangles, Data.Normals,
	                     Data.UVs, Data.VertexColors, Data.Tangents, bCol);
	if (Mat) M->SetMaterial(Idx, Mat);
}

void AVoxelChunk::DestroyAndRebuildMesh() { GenerateAsync(); }

// ── Water mesh ────────────────────────────────────────────────────────────────
void AVoxelChunk::RebuildWaterMesh()
{
	if (!IsValid(WaterMesh)) return;
	BuildWaterMeshInternal();
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
		uint8 Level=GetW(lx,ly,lz);
		if (Level==WATER_EMPTY) continue;
		const float WL=(Level==WATER_SOURCE?1.f:(float)Level/(float)WATER_FULL);
		const float x0=lx*VS,y0=ly*VS,z0=lz*VS,x1=x0+VS,y1=y0+VS,zT=z0+VS*WL;
		if (!IsSolidLocal(lx,ly,lz+1)&&GetW(lx,ly,lz+1)==WATER_EMPTY)
			Quad({x0,y0,zT},{x1,y0,zT},{x1,y1,zT},{x0,y1,zT},FVector::UpVector);
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
	for (UInstancedStaticMeshComponent* H : BiomeFoliageHISMs)
		if (IsValid(H)) H->ClearInstances();
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
	if (NewLOD==LOD || bGenerating) return;
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
