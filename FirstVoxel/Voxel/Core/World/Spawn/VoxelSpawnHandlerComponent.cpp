// VoxelSpawnHandlerComponent.cpp
#include "VoxelSpawnHandlerComponent.h"
#include "Voxel/Core/World/VoxelWorld.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "FirstVoxelCharacter.h"
#include "FirstVoxelHUD.h"
#include "Engine/World.h"
#include "Voxel/Biomes/VoxelBiomeManager.h"
#include "Voxel/Generation/VoxelDensityGenerator.h"
#include "GameFramework/PlayerStart.h"

UVoxelSpawnHandlerComponent::UVoxelSpawnHandlerComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UVoxelSpawnHandlerComponent::BeginPlay()
{
    Super::BeginPlay();
    WorldOwner = Cast<AVoxelWorld>(GetOwner());
}

void UVoxelSpawnHandlerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    AVoxelWorld* World = WorldOwner.Get();
    if (!World || !bWaitingForInitialSpawn) return;

    SpawnWaitAccum += DeltaTime;
    const bool bTimedOut = (SpawnWaitAccum > 900.f);

    const int32 Total = InitialSpawnCoords.Num();
    bool bAllReady = bTimedOut;

    if (!bTimedOut)
    {
        int32 ColReady = 0;
        for (const FIntVector& C : InitialSpawnCoords)
        {
            if (AVoxelChunk*const* P = World->GetLoadedChunks()->Find(C))
            {
                if ((*P)->IsCollisionReady()) ColReady++;
            }
        }
        int32 VisReady = 0;
        for (const FIntVector& C : InitialSpawnCoords_Visual)
        {
            if (AVoxelChunk*const* P = World->GetLoadedChunks()->Find(C))
            {
                if ((*P)->IsReady()) VisReady++;
            }
        }
        CachedCollisionReadyCount = ColReady;
        CachedVisualReadyCount = VisReady;

        const int32 CollisionTotal = InitialSpawnCoords.Num();
        const int32 VisualTotal = InitialSpawnCoords_Visual.Num();
        bAllReady = (ColReady == CollisionTotal) && (VisReady == VisualTotal);

        if (bAllReady)
        {
            UE_LOG(LogVoxelWorld, Log, TEXT("VoxelSpawnHandler: All initial chunks ready (%d col, %d vis). Dropping player."), 
                ColReady, VisReady);

            
            if (APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0))
            {
                const FVector TargetPos = World->GetSpawnTargetPos();
                Player->SetActorLocation(FVector(TargetPos.X, TargetPos.Y, TargetCoordsZ + 200.f), false, nullptr, ETeleportType::TeleportPhysics);
                Player->SetActorEnableCollision(true);
                if (ACharacter* Char = Cast<ACharacter>(Player))
                {
                    Char->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
                }
            }
            bWaitingForInitialSpawn = false;
            if (WorldOwner.IsValid()) WorldOwner->OnInitialSpawnComplete();
        }
        else
        {
            SpawnDelayAccum = 0.f;
        }
    }

    APawn* SpawnPlayer = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!SpawnPlayer) return;

    if (!bAllReady)
    {
        if (ACharacter* Ch = Cast<ACharacter>(SpawnPlayer))
        {
            if (UCharacterMovementComponent* CMC = Ch->GetCharacterMovement())
            {
                if (CMC->MovementMode != MOVE_None)
                {
                    CMC->SetMovementMode(MOVE_None);
                    CMC->bJustTeleported = true;
                }
            }
        }
        
        FVector HoverPos = SpawnPlayer->GetActorLocation();
        const FVoxelGenerationConfig EffConfig = WorldOwner->GetEffectiveConfig();
        const FVector TargetPos = WorldOwner->GetSpawnTargetPos();

        if (FMath::Abs(HoverPos.X - TargetPos.X) > 10.f ||
            FMath::Abs(HoverPos.Y - TargetPos.Y) > 10.f ||
            FMath::Abs(HoverPos.Z - TargetCoordsZ) > 10.f)
        {
            HoverPos.X = TargetPos.X;
            HoverPos.Y = TargetPos.Y;
            HoverPos.Z = TargetCoordsZ;
            SpawnPlayer->SetActorLocation(HoverPos, false, nullptr, ETeleportType::TeleportPhysics);
        }
    }
    else
    {
        bWaitingForInitialSpawn = false;
        SpawnWaitAccum = SpawnDelayAccum = 0.f;
        InitialSpawnCoords.Empty();
        
        // Notify World that generation lock is released
        // World->SetGenerationActive(false);

        SpawnPlayer->SetActorHiddenInGame(false);

        if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        {
            if (AFirstVoxelHUD* HUD = Cast<AFirstVoxelHUD>(PC->GetHUD()))
            {
                HUD->LoadProgress = 1.f;
                HUD->bShowLoadBar = false;
            }
        }

        SpawnPlayer->SetActorEnableCollision(true);
        const FVector TraceOrigin = SpawnPlayer->GetActorLocation();
        FHitResult Hit;
        FCollisionQueryParams QP; QP.AddIgnoredActor(SpawnPlayer);
        const FVector Start = TraceOrigin + FVector(0,0,50.f);
        const FVector End   = TraceOrigin + FVector(0,0,-150000.f);
        FCollisionShape Sphere = FCollisionShape::MakeSphere(30.f);
        bool bHit2 = GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility, Sphere, QP);
        if (!bHit2) bHit2 = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QP);

        if (bHit2)
        {
            FVector LandPos = TraceOrigin;
            LandPos.Z = Hit.ImpactPoint.Z + 101.f;
            SpawnPlayer->SetActorLocation(LandPos, false, nullptr, ETeleportType::TeleportPhysics);
        }

        if (ACharacter* Ch = Cast<ACharacter>(SpawnPlayer))
        {
            if (UCharacterMovementComponent* CMC = Ch->GetCharacterMovement())
            {
                CMC->Velocity = FVector::ZeroVector; 
                CMC->SetMovementMode(MOVE_Walking);
                CMC->UpdateFloorFromAdjustment(); 
                CMC->bJustTeleported = false; 
            }
        }
    }
}

void UVoxelSpawnHandlerComponent::ProcessInitialPlayerSpawn()
{
    if (!WorldOwner.IsValid()) return;

    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (!Player) return;

    const FVector OwnerSpawnPos = WorldOwner->GetSpawnTargetPos();
    if (!OwnerSpawnPos.IsZero())
        WorldOwner->GenerationConfig.Craters.ForcedCraterCenter = FVector2D(OwnerSpawnPos.X, OwnerSpawnPos.Y);

    // FIX #30: Use effective config
    FVoxelGenerationConfig Config = WorldOwner->GetEffectiveConfig(); 
    const FVector TargetSpawnPos = WorldOwner->GetSpawnTargetPos();
    if (!TargetSpawnPos.IsZero())
        Config.Craters.ForcedCraterCenter = FVector2D(TargetSpawnPos.X, TargetSpawnPos.Y);

    FVector Pos = TargetSpawnPos;
    if (Pos.IsZero())
    {
        TArray<AActor*> PS;
        UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), PS);
        if (PS.Num() > 0 && PS[0]) Pos = PS[0]->GetActorLocation();
    }
    
    Pos.X = Config.Craters.ForcedCraterCenter.X;
    Pos.Y = Config.Craters.ForcedCraterCenter.Y;
    Pos.Z = 0.f; 
    Pos = WorldOwner->SnapToVoxelGrid(Pos);

    const auto Wh = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(Pos.X, Pos.Y, Config);
    const float Surface = Wh.SurfaceHeight;
    const float SafeOff = WorldOwner->GetSafeSpawnHeightOffset();
    float TargetZ = Surface + SafeOff;
    const float CraterW = Wh.Weights.GetWeight(EVoxelBiome::Craters);
    if (TargetZ > 100000.f || TargetZ < Surface - 1000.f) TargetZ = Surface + SafeOff;

    float MinSkyAlt, MaxSkyAlt;
    FVoxelBiomeGenerators::GetSkylandAltitudeBounds(Surface, Wh.Weights.GetRoughness(), Config, MinSkyAlt, MaxSkyAlt);
    const float SkyAlt = (MinSkyAlt + MaxSkyAlt) * 0.5f;
    const float IHT = (MaxSkyAlt - MinSkyAlt) * 0.5f;

    bool bSky = false;
    if (SkyAlt > Surface + 5000.f && Surface < 50000.f)
    {
        for (float z2 = SkyAlt + IHT; z2 >= FMath::Max(SkyAlt - IHT, Surface + 500.f); z2 -= 200.f)
        {
            if (WorldOwner->GetDensityGenerator() && WorldOwner->GetDensityGenerator()->GetDensity(Pos.X, Pos.Y, z2, Config) > 0.f)
            {
                TargetZ = z2 + SafeOff;
                bSky = true;
                break;
            }
        }
    }

    if (TargetZ > 100000.f || CraterW > 0.3f) TargetZ = Surface + SafeOff;

    UE_LOG(LogVoxelWorld, Warning, TEXT("VoxelWorld: Spawn Pos=(%.0f,%.0f) Surface=%.0f Z=%.0f CraterW=%.2f Sky=%d CONFIG_DEPTH=%.0f CONFIG_RADIUS=%.0f"),
        Pos.X, Pos.Y, Surface, TargetZ, CraterW, bSky ? 1 : 0, Config.Craters.CentralCraterDepth, Config.Craters.CentralCraterRadius);

    Pos.Z = TargetZ;
    // Hover just above the crater rim so it's visible behind the loading overlay.
    // Was +45000 (450m in the sky — camera sees nothing useful during generation).
    TargetCoordsZ = TargetZ + 8000.f; // ~80m above spawn — rim and bowl visible at load
    CachedSurfaceHeight = Surface;

    Player->SetActorLocation(Pos, false, nullptr, ETeleportType::TeleportPhysics);

    if (bWaitingForInitialSpawn) return;
    InitialSpawnCoords.Empty();
    CachedCollisionReadyCount = 0;
    CachedVisualReadyCount = 0;

    bWaitingForInitialSpawn = true;

    const FIntVector SpawnCoord = WorldOwner->WorldToChunkCoord(FVector(Pos.X, Pos.Y, TargetZ));
    const FIntVector GroundCoord = WorldOwner->WorldToChunkCoord(FVector(Pos.X, Pos.Y, Surface));

    TSet<FIntVector> SpawnSet;
    TSet<FIntVector> VisualSet;

    auto AddToCollision = [&](const FIntVector& C) { SpawnSet.Add(C); };
    auto AddToVisual = [&](const FIntVector& C) { VisualSet.Add(C); };

    const float GridSize = WorldOwner->ChunkSize * WorldOwner->VoxelSize;
    const float CraterRadius = Config.Craters.CentralCraterRadius;
    const int32 ChunkRadius = FMath::CeilToInt(CraterRadius / GridSize) + 12;

    for (int32 x = -ChunkRadius; x <= ChunkRadius; x++)
    {
        for (int32 y2 = -ChunkRadius; y2 <= ChunkRadius; y2++)
        {
            if (x * x + y2 * y2 > ChunkRadius * ChunkRadius) continue;

            const float WX = (SpawnCoord.X + x + 0.5f) * GridSize;
            const float WY = (SpawnCoord.Y + y2 + 0.5f) * GridSize;
            const auto WhCol = FVoxelBiomeManager::GetWeightsAndSurfaceHeightStatic(WX, WY, Config);
            const int32 ColGroundZ = FMath::FloorToInt(WhCol.SurfaceHeight / GridSize);

            const bool bCollisionNeeded = (FMath::Abs(x) <= 4 && FMath::Abs(y2) <= 4);
            if (bCollisionNeeded)
            {
                for (int32 z2 = -2; z2 <= 2; z2++)
                {
                    AddToCollision(FIntVector(SpawnCoord.X + x, SpawnCoord.Y + y2, ColGroundZ + z2));
                }
            }
            else
            {
                AddToVisual(FIntVector(SpawnCoord.X + x, SpawnCoord.Y + y2, ColGroundZ));
            }
        }
    }

    const int32 VisualRadius = 16;
    for (int32 x = -VisualRadius; x <= VisualRadius; x++)
    {
        for (int32 y2 = -VisualRadius; y2 <= VisualRadius; y2++)
        {
            if (FMath::Abs(x) <= ChunkRadius && FMath::Abs(y2) <= ChunkRadius) continue;
            AddToVisual(FIntVector(SpawnCoord.X + x, SpawnCoord.Y + y2, GroundCoord.Z));
        }
    }

    if (FMath::Abs(SpawnCoord.Z - GroundCoord.Z) > 1)
    {
        for (int32 x = -25; x <= 25; x++)
        {
            for (int32 y2 = -25; y2 <= 25; y2++)
            {
                const bool bInner = (FMath::Abs(x) <= 10 && FMath::Abs(y2) <= 10);
                if (bInner)
                {
                    AddToCollision(FIntVector(SpawnCoord.X + x, SpawnCoord.Y + y2, GroundCoord.Z));
                    AddToCollision(FIntVector(SpawnCoord.X + x, SpawnCoord.Y + y2, GroundCoord.Z + 1));
                    AddToCollision(FIntVector(SpawnCoord.X + x, SpawnCoord.Y + y2, GroundCoord.Z - 1));
                }
                else
                {
                    AddToVisual(FIntVector(SpawnCoord.X + x, SpawnCoord.Y + y2, GroundCoord.Z));
                }
            }
        }
    }

    TArray<FIntVector> SpawnCoords = SpawnSet.Array();
    TArray<FIntVector> VisualCoords = VisualSet.Array();

    auto SortCoords = [SpawnCoord](const FIntVector& A, const FIntVector& B)
    {
        const bool bBA = (A.X == SpawnCoord.X && A.Y == SpawnCoord.Y && A.Z <= SpawnCoord.Z);
        const bool bBB = (B.X == SpawnCoord.X && B.Y == SpawnCoord.Y && B.Z <= SpawnCoord.Z);
        if (bBA && !bBB) return true;
        if (!bBA && bBB) return false;
        return (FMath::Abs(A.X - SpawnCoord.X) + FMath::Abs(A.Y - SpawnCoord.Y) + FMath::Abs(A.Z - SpawnCoord.Z))
            < (FMath::Abs(B.X - SpawnCoord.X) + FMath::Abs(B.Y - SpawnCoord.Y) + FMath::Abs(B.Z - SpawnCoord.Z));
    };

    SpawnCoords.Sort(SortCoords);
    VisualCoords.Sort(SortCoords);

    TArray<FVoxelGenerationQueueEntry> PriorityQueue;
    InitialSpawnCoords_Visual.Empty();

    for (const FIntVector& C : SpawnCoords)
    {
        InitialSpawnCoords.Add(C);
        if (!WorldOwner->GetLoadedChunks()->Contains(C))
        {
            const bool bSync = (C.X == GroundCoord.X && C.Y == GroundCoord.Y && C.Z <= GroundCoord.Z && C.Z >= GroundCoord.Z - 2);
            if (bSync) WorldOwner->SpawnChunk(C, true);
            else PriorityQueue.Add(FVoxelGenerationQueueEntry(C, 10000.0f)); // Max priority for mandatory spawn chunks
        }
    }

    const int32 WaitRadius = FMath::Max(ChunkRadius, WorldOwner->DistantRenderDistanceXY);

    for (const FIntVector& C : VisualCoords)
    {
        const int32 dx = FMath::Abs(C.X - SpawnCoord.X);
        const int32 dy = FMath::Abs(C.Y - SpawnCoord.Y);

        if (dx <= WaitRadius && dy <= WaitRadius)
        {
            InitialSpawnCoords_Visual.Add(C);

            if (!WorldOwner->GetLoadedChunks()->Contains(C))
            {
                PriorityQueue.Add(FVoxelGenerationQueueEntry(C, 5000.0f)); // High priority for visual spawn area
            }
        }
    }

    if (PriorityQueue.Num() > 0)
    {
        TArray<FVoxelGenerationQueueEntry> NewQueue = PriorityQueue;
        NewQueue.Append(WorldOwner->GetGenerationQueue());
        NewQueue.Heapify(); // Re-heapify after merging
        WorldOwner->SetGenerationQueue(NewQueue);
    }

    int32 PreCollision = 0;
    for (const FIntVector& C : InitialSpawnCoords)
        if (AVoxelChunk*const* P = WorldOwner->GetLoadedChunks()->Find(C)) if ((*P)->IsCollisionReady()) PreCollision++;
    CachedCollisionReadyCount = PreCollision;

    int32 PreVisual = 0;
    for (const FIntVector& C : InitialSpawnCoords_Visual)
        if (AVoxelChunk*const* P = WorldOwner->GetLoadedChunks()->Find(C)) if ((*P)->IsReady()) PreVisual++;
    CachedVisualReadyCount = PreVisual;

    UE_LOG(LogVoxelWorld, Log, TEXT("VoxelSpawnHandler: Waiting for %d collision + %d visual spawn chunks. Pre-ready: collision=%d, visual=%d"),
        InitialSpawnCoords.Num(), InitialSpawnCoords_Visual.Num(), PreCollision, PreVisual);
}

void UVoxelSpawnHandlerComponent::ClearState()
{
    bWaitingForInitialSpawn = false;
    InitialSpawnCoords.Empty();
    InitialSpawnCoords_Visual.Empty();
    CachedCollisionReadyCount = 0;
    CachedVisualReadyCount = 0;

    SpawnWaitAccum = 0.f;
    SpawnDelayAccum = 0.f;
}
