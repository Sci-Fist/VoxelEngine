// VoxelWorld.cpp
// Core implementation of AVoxelWorld.
// Generation, streaming, modification, and water logic live in separate
// _Generation / _Streaming / _Modification .cpp files to keep compile units small.
//
// BUG FIXES IN THIS REVISION:
//   - DensityGenerator and WaterSimulator are now properly initialized in BeginPlay()
//   - TickWater now correctly runs the sim step and rebuilds dirty water meshes
//   - InitChunkWater now wires OnChunkWaterReady so sources reach the simulator
//   - PostEditChangeProperty is correctly wrapped in WITH_EDITOR
//   - EndPlay properly cancels all chunks before clearing LoadedChunks

#include "VoxelWorld.h"
#include "Voxel/Core/World/Water/VoxelWorldWater.h"
#include "Voxel/Core/VoxelChunk.h"
#include "Voxel/Core/VoxelChunkPool.h"
#include "Voxel/Core/VoxelDataMap.h"
#include "Voxel/Generation/VoxelGeneratorTask.h"
#include "Voxel/Generation/VoxelDensityGenerator.h"
#include "Voxel/Biomes/VoxelBiomeManager.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "Voxel/VoxelLogger.h"
#include "Voxel/Water/VoxelWaterSimulator.h"
#include "Voxel/Water/VoxelWaterComponent.h"
#include "Engine/World.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Misc/DateTime.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformProcess.h"

DEFINE_LOG_CATEGORY(LogVoxelWorld);

// ============================================================
//  Constructor
// ============================================================
AVoxelWorld::AVoxelWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
	RootComponent = Root;

	WaterComponent = CreateDefaultSubobject<UVoxelWaterComponent>(TEXT("WaterComponent"));

	WaterSystemComponent = CreateDefaultSubobject<UVoxelWorldWaterComponent>(TEXT("WaterSystemComponent"));
}

AVoxelWorld::~AVoxelWorld()
{
	// TUniquePtr members auto-destruct; nothing extra needed here.
}

// ============================================================
//  BeginPlay
//  Critical: initialize DensityGenerator + WaterSimulator here,
//  not in the constructor, so ChunkSize / VoxelSize are final.
// ============================================================
void AVoxelWorld::BeginPlay()
{
	Super::BeginPlay();

	// ---- Density generator (all 3 layers: Surface / Skylands / Caves) ----
	DensityGenerator = MakeUnique<FVoxelDensityGenerator>();

	// ---- Water subsystem component ----
	if (WaterSystemComponent)
	{
		WaterSystemComponent->Initialize(MakeUnique<FVoxelWaterSimulator>(ChunkSize, VoxelSize), WaterComponent);
	}

	// ---- Data map (tracks player edits) ----
	if (!bInitialized)
	{
		DataMap.Init(ChunkSize);
		bInitialized = true;
	}

	// ---- Optional random seed ----
	if (bRandomizeSeedOnStartup)
	{
		GenerationConfig.Seed = FMath::RandRange(0, TNumericLimits<int32>::Max());
		UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: Randomised seed = %d"), GenerationConfig.Seed);
	}

	if (bAutoGenerateOnBeginPlay)
	{
		GenerateWorld();
	}
}

// ============================================================
//  EndPlay
// ============================================================
void AVoxelWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bShutdown = true;

	// Cancel all in-flight async tasks before we destroy anything.
	for (auto& It : LoadedChunks)
	{
		if (AVoxelChunk* Chunk = It.Value)
		{
			if (Chunk->IsGenerating())
				Chunk->CancelGeneration();
		}
	}

	// Brief spin to let queued game-thread callbacks drain (max 3 s).
	const double Deadline = FPlatformTime::Seconds() + 3.0;
	while (ActiveGenerations > 0 && FPlatformTime::Seconds() < Deadline)
		FPlatformProcess::Sleep(0.01f);

	LoadedChunks.Empty();
	GenerationQueue.Empty();
	QueueHead = 0;
	ActiveGenerations = 0;

	Super::EndPlay(EndPlayReason);
}

// ============================================================
//  Tick
// ============================================================
void AVoxelWorld::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (GetWorld()->IsGameWorld())
		UpdateChunkStreaming();

	DrainGenerationQueue();


	// Rebuild any chunks dirtied by player edits.
	for (auto& It : LoadedChunks)
	{
		if (AVoxelChunk* Chunk = It.Value)
		{
			if (Chunk->bMeshDirty && !Chunk->IsGenerating())
			{
				Chunk->GenerateAsync();
				Chunk->bMeshDirty = false;
			}
		}
	}
}

// ============================================================
//  OnConstruction
// ============================================================
void AVoxelWorld::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!bInitialized && ChunkSize > 0)
	{
		DataMap.Init(ChunkSize);
		bInitialized = true;
	}
}


// ============================================================
//  ClearWorld
// ============================================================
void AVoxelWorld::ClearWorld()
{
	TArray<FIntVector> Keys;
	LoadedChunks.GetKeys(Keys);
	for (const FIntVector& Coord : Keys)
		DestroyChunk(Coord);

	LoadedChunks.Empty();
	GenerationQueue.Empty();
	QueueHead = 0;
	ActiveGenerations = 0;

	UE_LOG(LogVoxelWorld, Log, TEXT("VoxelWorld: World cleared"));
}

// ============================================================
//  SnapPlayerToGround
// ============================================================
void AVoxelWorld::SnapPlayerToGround()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player) return;

	FVector Pos = Player->GetActorLocation();
	Pos = SnapToVoxelGrid(Pos);

	const FVoxelGenerationConfig& Config = GetEffectiveConfig();
	if (bForceCraterSpawn)
		Pos = FindCraterSpawnLocation(Pos, Config);

	Player->SetActorLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);
}

// ============================================================
//  Preset helpers (stub — wiring done via Editor buttons)
// ============================================================
void AVoxelWorld::SaveCurrentToPreset()
{
	if (BiomePreset)
		BiomePreset->Config = GetEffectiveConfig();
	else
		UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: No BiomePreset assigned"));
}

void AVoxelWorld::LoadFromPreset()
{
	if (!BiomePreset)
		UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: No BiomePreset assigned"));
}

// ============================================================
//  Public API stubs
// ============================================================
void AVoxelWorld::GenerateWorld()    { GenerateWorldDeferred(); }
void AVoxelWorld::RandomizeSeed()    { GenerationConfig.Seed = FMath::RandRange(0, TNumericLimits<int32>::Max()); }
void AVoxelWorld::ClearWorldModifications() { DataMap.Clear(); }
void AVoxelWorld::RebuildWorld()     { ClearWorld(); GenerateWorldDeferred(); }
void AVoxelWorld::RunTests()         { RunVoxelTests(); }

// ============================================================
//  Coordinate helpers
// ============================================================
FIntVector AVoxelWorld::WorldToChunkCoord(const FVector& WorldPos) const
{
	const FVector Anchor = GetActorLocation();
	return FIntVector(
		FMath::FloorToInt((WorldPos.X - Anchor.X) / (ChunkSize * VoxelSize)),
		FMath::FloorToInt((WorldPos.Y - Anchor.Y) / (ChunkSize * VoxelSize)),
		FMath::FloorToInt((WorldPos.Z - Anchor.Z) / (ChunkSize * VoxelSize)));
}

FVector AVoxelWorld::ChunkCoordToWorld(const FIntVector& Coord) const
{
	const FVector Anchor = GetActorLocation();
	return Anchor + FVector(
		Coord.X * ChunkSize * VoxelSize,
		Coord.Y * ChunkSize * VoxelSize,
		Coord.Z * ChunkSize * VoxelSize);
}

// ============================================================
//  Editor
// ============================================================
#if WITH_EDITOR
void AVoxelWorld::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Future: trigger selective rebuild when specific properties change.
}
#endif
