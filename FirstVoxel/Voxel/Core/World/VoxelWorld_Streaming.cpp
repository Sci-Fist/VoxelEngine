// VoxelWorld_Streaming.cpp
// Implementation of chunk streaming and LOD management for AVoxelWorld.
// This file contains streaming-related function implementations to reduce
// the size of VoxelWorld.cpp and improve maintainability.

#include "Voxel/Core/World/VoxelWorld.h"
#include "Voxel/Core/VoxelChunk.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "Voxel/Biomes/VoxelBiomeManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"


#include "Voxel/VoxelLogger.h"

// ============================================================
//  Chunk Streaming Implementation
// ============================================================

void AVoxelWorld::UpdateChunkStreaming()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player || !GetWorld()->IsGameWorld()) 
	{
		return;
	}

	StreamingTimer += GetWorld()->GetDeltaSeconds();
	if (StreamingTimer < StreamingInterval) return;
	StreamingTimer = 0.f;

	// --- ⚡ Optimization: Skip building streaming volumes if player is stationary ---
	const FVector CurrentPos = Player->GetActorLocation();
	const float MinStep = ChunkSize * VoxelSize * 0.4f; 
	if (FVector::DistSquared(CurrentPos, LastStreamedPos) < MinStep * MinStep)
	{
		return; // No movement, preserve CPU budget
	}
	LastStreamedPos = CurrentPos;

	// Use full 3D player position for volumetric streaming.
	FVector PlayerPos = Player->GetActorLocation();
	FIntVector PlayerCoord = WorldToChunkCoord(PlayerPos);

	const FVoxelGenerationConfig& Config = GetEffectiveConfig();
	const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
	const float ChunkWorldSize = ChunkSize * VoxelSize;

	// Compute the skyland altitude directly above the player's current terrain so
	// that over mountains (surface ~80,000cm) islands at ~100,000cm are streamed in.
	// The old approach used only BaseAltitudeAboveTerrain (4500cm), placing the
	// skylands pass at chunk Z ~3, while mountain skylands live at chunk Z ~60+.
	const FVoxelBiomeManager::FWeightsAndHeight Wh = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(
		PlayerPos.X, PlayerPos.Y, Config);
	const float PlayerSurfH = Wh.SurfaceHeight;

	const float HeightNormSky    = FMath::Clamp(PlayerSurfH / SC.MaxTerrainReference, 0.f, 1.f);
	const float RoughnessNormSky = FMath::Clamp(Wh.Weights.GetRoughness() / SC.RoughnessReference, 0.f, 1.f);
	const float TerrainStrSky    = FMath::Clamp(HeightNormSky * 1.5f + RoughnessNormSky * 0.8f, 0.f, 1.f);
	const float CurvedH          = FMath::Pow(HeightNormSky,    2.5f);
	const float CurvedR          = FMath::Pow(RoughnessNormSky, 2.0f);
	const float AltBase          = FMath::Lerp(SC.MinAltitudeAboveTerrain, SC.BaseAltitudeAboveTerrain, TerrainStrSky);
	const float SkyAltWorld      = PlayerSurfH + AltBase
		                           + CurvedH * SC.HeightAltitudeBonus
		                           + CurvedR * SC.RoughnessAltitudeBonus;

	// Island vertical half-thickness in chunks (+ 2 margin chunks on each side)
	const float IslandSize    = FMath::Max(SC.BaseIslandSize,
		                          SC.BaseIslandSize + CurvedH * SC.HeightSizeBonus + CurvedR * SC.RoughnessSizeBonus);
	const float HalfThickCm   = FMath::Max(200.f, IslandSize * SC.ThicknessRatio);
	const int32 SkyThickness  = FMath::CeilToInt(HalfThickCm / ChunkWorldSize) + 2;
	const int32 SkyZCoordCenter = FMath::RoundToInt(SkyAltWorld / ChunkWorldSize);
	TSet<FIntVector> Desired;

	// 1. Ground area
	for (int32 z = -RenderDistanceZ; z <= RenderDistanceZ; ++z)
	for (int32 y = -RenderDistanceXY; y <= RenderDistanceXY; ++y)
	for (int32 x = -RenderDistanceXY; x <= RenderDistanceXY; ++x)
	{
		Desired.Add(PlayerCoord + FIntVector(x, y, z));
	}

	// 2. Skylands Pass: larger range to maintain distance visibility without overloading ground chunks
	for (int32 z = -SkyThickness; z <= SkyThickness; ++z)
	for (int32 y = -SkylandsRenderDistanceXY; y <= SkylandsRenderDistanceXY; ++y)
	for (int32 x = -SkylandsRenderDistanceXY; x <= SkylandsRenderDistanceXY; ++x)
	{
		Desired.Add(FIntVector(PlayerCoord.X + x, PlayerCoord.Y + y, SkyZCoordCenter + z));
	}

	TArray<FIntVector> ToRemove;
	for (auto& It : LoadedChunks)
	{
		if (!Desired.Contains(It.Key)) ToRemove.Add(It.Key);
	}
	for (const FIntVector& C : ToRemove) DestroyChunk(C);

	// --- 3. DYNAMIC LOD MULTIPLIERS FOR EXISTING CHUNKS ---
	for (auto& It : LoadedChunks)
	{
		AVoxelChunk* Chunk = It.Value;
		if (IsValid(Chunk))
		{
			// Midpoint of the chunk for accurate spherical distance
			FVector ChunkPos = ChunkCoordToWorld(It.Key) + FVector(ChunkSize * VoxelSize * 0.5f);
			float DistSq = FVector::DistSquared(PlayerPos, ChunkPos);

			int32 TargetLOD = 0;
			if (DistSq > LOD2Distance * LOD2Distance) TargetLOD = 2;
			else if (DistSq > LOD1Distance * LOD1Distance) TargetLOD = 1;

			if (Chunk->LOD != TargetLOD)
			{
				// Use smooth LOD transition instead of instant rebuild
				Chunk->TransitionToLOD(TargetLOD);
			}
		}
	}

	// Add new desired chunks, sorted nearest-first
	TArray<TPair<int32, FIntVector>> NewChunks;

	// OPTIMIZATION: convert queue to set for O(1) lookup to prevent main-thread freeze with large volumes
	TSet<FIntVector> QueueSet(GenerationQueue);

	for (const FIntVector& C : Desired)
	{
		if (!LoadedChunks.Contains(C) && !QueueSet.Contains(C))
		{
			FIntVector Local = C - PlayerCoord;
			int32 DistSq = Local.X*Local.X + Local.Y*Local.Y + Local.Z*Local.Z;
			NewChunks.Add(TPair<int32, FIntVector>(DistSq, C));
		}
	}
	// PRIORITY SORT: Ensure nearest chunks land at the HEAD of the queue
	NewChunks.Sort([](const TPair<int32,FIntVector>& A, const TPair<int32,FIntVector>& B){ return A.Key < B.Key; });
	
	const int32 NumNew = NewChunks.Num();
	if (NumNew > 0)
	{
		GenerationQueue.Reserve(GenerationQueue.Num() + NumNew);
		for (int32 i = 0; i < NumNew; ++i)
		{
			GenerationQueue.Add(NewChunks[i].Value);
		}
		UE_LOG(LogVoxelWorld, Verbose, TEXT("VoxelWorld: Streaming added %d new chunks to queue"), NumNew);
	}
}
