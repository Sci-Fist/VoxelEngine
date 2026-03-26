// VoxelMapWidget.cpp
// FIX #21 — Removed UE_LOG from NativePaint (was logging every frame at 60 Hz).
// FIX #26 — Map refresh lambda now captures a lightweight FMapSnapshot struct
//            instead of the full FVoxelGenerationConfig (which contained TArray
//            members — 14+ heap allocations per refresh at 1Hz).

#include "UI/VoxelMapWidget.h"
#include "Voxel/Core/World/VoxelWorld.h"
#include "Voxel/VoxelMapGenerator.h"
#include "Voxel/Biomes/VoxelBiomeManager.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Styling/SlateBrush.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Async/Async.h"
#include "Misc/ScopeLock.h"
#include "Input/Events.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"

// FIX #26: lightweight snapshot capturing only what GeneratePixelBuffer needs.
// Previously the full FVoxelGenerationConfig was deep-copied into the lambda,
// pulling in 7 TArray<FVoxelFoliageEntry> members = 14+ heap allocations per refresh.
struct FMapSnapshot
{
    int32  Seed;
    float  SeaLevel;
    float  MaxTerrainRef;
    FBiomeBlendConfig BiomeBlend;
    FForestBiomeConfig Forest;
    FDesertBiomeConfig Desert;
    FPeaksBiomeConfig  Peaks;
    FCliffsBiomeConfig Cliffs;
    FMesaBiomeConfig   Mesa;
    FCraterBiomeConfig Craters;
    FVoxelPerformanceConfig Performance;
};

static FVoxelGenerationConfig SnapshotToConfig(const FMapSnapshot& S)
{
    FVoxelGenerationConfig C;
    C.Seed        = S.Seed;
    C.SeaLevel    = S.SeaLevel;
    C.SkylandsLayer.MaxTerrainReference = S.MaxTerrainRef;
    C.BiomeBlend  = S.BiomeBlend;
    C.Forest      = S.Forest;
    C.Desert      = S.Desert;
    C.Peaks       = S.Peaks;
    C.Cliffs      = S.Cliffs;
    C.Mesa        = S.Mesa;
    C.Craters     = S.Craters;
    C.Performance = S.Performance;
    return C;
}

namespace
{
    FORCEINLINE FPaintGeometry MakePaintGeom(const FGeometry& G, FVector2D Off, FVector2D Sz)
    {
        return G.ToPaintGeometry(FVector2f((float)Sz.X,(float)Sz.Y),
                                 FSlateLayoutTransform(FVector2f((float)Off.X,(float)Off.Y)));
    }
    FORCEINLINE FPaintGeometry MakePaintGeomFull(const FGeometry& G, FVector2D Sz)
    {
        return G.ToPaintGeometry(FVector2f((float)Sz.X,(float)Sz.Y), FSlateLayoutTransform());
    }
}

FSlateFontInfo UVoxelMapWidget::GetFont(int32 Size) const
{
    return FCoreStyle::GetDefaultFontStyle("Regular",
        (uint16)FMath::Clamp(Size>0?Size:FontSize, 6, 72));
}

void UVoxelMapWidget::NativeConstruct()
{
    Super::NativeConstruct();
    bShuttingDown = false;
    SetVisibility(ESlateVisibility::Hidden);
    SetKeyboardFocus();
}

void UVoxelMapWidget::NativeDestruct()
{
    bShuttingDown = true;
    bMapOpen = false;
    if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(RefreshTimerHandle);
    Super::NativeDestruct();
}

static float GLastMapOpenTime = 0.f;

void UVoxelMapWidget::OpenMap(AVoxelWorld* InVoxelWorld, APawn* InPlayerPawn)
{
    if (bShuttingDown) return;
    // FIX #21: log only on map open, not every frame in NativePaint
    UE_LOG(LogTemp, Log, TEXT("VoxelMapWidget: OpenMap — World=%s Pawn=%s"),
        InVoxelWorld ? *InVoxelWorld->GetName() : TEXT("null"),
        InPlayerPawn ? *InPlayerPawn->GetName() : TEXT("null"));

    CachedVoxelWorld = InVoxelWorld;
    CachedPlayerPawn = InPlayerPawn;
    bMapOpen         = true;
    SetVisibility(ESlateVisibility::Visible);
    SetKeyboardFocus();
    EnsureTexture();
    CachedBiomeName = GetPlayerBiomeName();
    RequestRefresh();

    if (UWorld* W = GetWorld())
    {
        GLastMapOpenTime = W->GetTimeSeconds();
        W->GetTimerManager().SetTimer(RefreshTimerHandle,
            this, &UVoxelMapWidget::OnRefreshTimer,
            RefreshInterval, /*bLoop=*/true);
    }
}

void UVoxelMapWidget::CloseMap()
{
    bMapOpen = false;
    SetVisibility(ESlateVisibility::Hidden);
    if (UWorld* W = GetWorld()) W->GetTimerManager().ClearTimer(RefreshTimerHandle);
}

FString UVoxelMapWidget::GetPlayerBiomeName() const
{
    if (!CachedVoxelWorld || !CachedPlayerPawn) return TEXT("Unknown");
    const FVector Pos = CachedPlayerPawn->GetActorLocation();
    const FVoxelGenerationConfig& Cfg = CachedVoxelWorld->GetEffectiveConfig();
    const FVoxelBiomeWeightMap W = FVoxelBiomeManager::GetBiomeWeightsStatic(Pos.X, Pos.Y, Cfg);
    return FString(FVoxelMapGenerator::BiomeNames[static_cast<uint8>(W.GetDominantBiome())]);
}

FReply UVoxelMapWidget::NativeOnKeyDown(const FGeometry& InGeo, const FKeyEvent& InKey)
{
    const FKey Key = InKey.GetKey();
    if (Key == EKeys::M)
    {
        if (UWorld* W = GetWorld())
            if (W->GetTimeSeconds() - GLastMapOpenTime < 0.25f)
                return FReply::Handled();
    }
    if (Key == EKeys::M || Key == EKeys::Escape)
    {
        CloseMap();
        if (APlayerController* PC = GetOwningPlayer())
        {
            PC->SetInputMode(FInputModeGameOnly());
            PC->bShowMouseCursor = false;
        }
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(InGeo, InKey);
}

void UVoxelMapWidget::EnsureTexture()
{
    if (MapTexture && MapTexture->GetSizeX() == MapResolution && MapTexture->GetSizeY() == MapResolution)
        return;

    MapTexture = UTexture2D::CreateTransient(MapResolution, MapResolution, PF_B8G8R8A8, TEXT("VoxelMapTex"));
    if (!MapTexture) return;
    MapTexture->NeverStream = true; MapTexture->SRGB = false;
    MapTexture->Filter = TF_Bilinear; MapTexture->AddressX = TA_Clamp; MapTexture->AddressY = TA_Clamp;

    FTexture2DMipMap& Mip = MapTexture->GetPlatformData()->Mips[0];
    FColor* Data = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
    FMemory::Memset(Data, 0x40, MapResolution * MapResolution * sizeof(FColor));
    Mip.BulkData.Unlock();
    MapTexture->UpdateResource();
    MapBrush = FSlateBrush();
    MapBrush.SetResourceObject(MapTexture);
    MapBrush.ImageSize = FVector2D(MapResolution, MapResolution);
}

void UVoxelMapWidget::RequestRefresh()
{
    if (bShuttingDown || bGenerating || !CachedVoxelWorld || !CachedPlayerPawn) return;
    bGenerating = true;

    const FVector Pos   = CachedPlayerPawn->GetActorLocation();
    UE_LOG(LogTemp, Log, TEXT("VoxelMapWidget: RequestRefresh started at [%.0f, %.0f]. Res=%d Radius=%.0f"), Pos.X, Pos.Y, MapResolution, MapWorldRadius);
    PlayerWorldPos      = Pos;
    const float  Radius = MapWorldRadius;
    const int32  Res    = MapResolution;
    const float  ChkSz  = (float)CachedVoxelWorld->ChunkSize * CachedVoxelWorld->VoxelSize;

    // FIX #26: build lightweight snapshot — no TArray members captured
    const FVoxelGenerationConfig& FullCfg = CachedVoxelWorld->GetEffectiveConfig();
    FMapSnapshot Snap;
    Snap.Seed        = FullCfg.Seed;
    Snap.SeaLevel    = FullCfg.SeaLevel;
    Snap.MaxTerrainRef = FullCfg.SkylandsLayer.MaxTerrainReference;
    Snap.BiomeBlend  = FullCfg.BiomeBlend;
    Snap.Forest      = FullCfg.Forest;
    Snap.Desert      = FullCfg.Desert;
    Snap.Peaks       = FullCfg.Peaks;
    Snap.Cliffs      = FullCfg.Cliffs;
    Snap.Mesa        = FullCfg.Mesa;
    Snap.Craters     = FullCfg.Craters;
    Snap.Performance = FullCfg.Performance;

    TSet<FIntVector> ChunkKeys;
    for (const auto& Pair : *CachedVoxelWorld->GetLoadedChunks())
        ChunkKeys.Add(Pair.Key);

    TWeakObjectPtr<UVoxelMapWidget> WeakThis(this);
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
    [WeakThis, Pos, Snap, Radius, Res, ChkSz, ChunkKeys]()
    {
        if (!WeakThis.IsValid() || WeakThis->bShuttingDown) return;

        const FVoxelGenerationConfig Config = SnapshotToConfig(Snap); // cheap — no TArray members
        TArray<FColor> NewPixels;
        FVoxelMapGenerator::GeneratePixelBuffer(Pos.X, Pos.Y, Radius, Res,
            Config, ChunkKeys, ChkSz, NewPixels);

        AsyncTask(ENamedThreads::GameThread, [WeakThis, NewPixels = MoveTemp(NewPixels)]() mutable
        {
            if (!WeakThis.IsValid() || WeakThis->bShuttingDown) return;
            UVoxelMapWidget* Self = WeakThis.Get();
            if (!Self) return;
            { FScopeLock Lock(&Self->PixelLock);
              Self->PendingPixels = MoveTemp(NewPixels);
              Self->bTextureDirty = true; }
            Self->bGenerating = false;
            Self->UploadPendingPixels();
        });
    });
}

void UVoxelMapWidget::OnRefreshTimer()
{
    if (bMapOpen && CachedPlayerPawn && !bShuttingDown)
    {
        PlayerWorldPos  = CachedPlayerPawn->GetActorLocation();
        CachedBiomeName = GetPlayerBiomeName();
        RequestRefresh();
    }
}

void UVoxelMapWidget::UploadPendingPixels()
{
    if (bShuttingDown || !bTextureDirty || !MapTexture) return;
    if (MapTexture->GetSizeX() != MapResolution) EnsureTexture();

    TArray<FColor> Local;
    { FScopeLock Lock(&PixelLock);
      if (PendingPixels.Num() != MapResolution*MapResolution)
      {
          UE_LOG(LogTemp, Warning, TEXT("VoxelMapWidget: PendingPixels size mismatch! Expected %d, got %d"), MapResolution*MapResolution, PendingPixels.Num());
          return;
      }
      Local = MoveTemp(PendingPixels);
      bTextureDirty = false; }

    UE_LOG(LogTemp, Log, TEXT("VoxelMapWidget: Uploading %d pixels to texture..."), Local.Num());

    FTexture2DMipMap& Mip = MapTexture->GetPlatformData()->Mips[0];
    FColor* Data = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
    FMemory::Memcpy(Data, Local.GetData(), Local.Num()*sizeof(FColor));
    Mip.BulkData.Unlock();
    MapTexture->UpdateResource();
    MapBrush.SetResourceObject(MapTexture);
}

FSlateRect UVoxelMapWidget::ComputeMapRect(const FGeometry& Geom) const
{
    const FVector2D Sz = Geom.GetLocalSize();
    const float Short  = FMath::Min(Sz.X, Sz.Y);
    const float Panel  = Short * MapPanelFraction;
    const float MapSz  = Panel * 0.68f;
    const float PanX   = (Sz.X - Panel) * 0.5f;
    const float PanY   = (Sz.Y - Panel) * 0.5f;
    const float MapL   = PanX + (Panel-MapSz)*0.5f;
    const float MapT   = PanY + Panel*0.08f;
    return FSlateRect(MapL, MapT, MapL+MapSz, MapT+MapSz);
}

int32 UVoxelMapWidget::NativePaint(
    const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Cull,
    FSlateWindowElementList& Out, int32 Layer,
    const FWidgetStyle& Style, bool bParent) const
{
    if (!bMapOpen) return Layer;

    static float LastPaintLog = 0.f;
    float CurTime = GetWorld()->GetTimeSeconds();
    if (CurTime - LastPaintLog > 2.0f) // Throttle to 0.5 Hz
    {
        UE_LOG(LogTemp, Log, TEXT("VoxelMapWidget: NativePaint active. Tex=%p BrushRes=%p bGenerating=%s"), 
            (UTexture2D*)MapTexture, MapBrush.GetResourceObject(), bGenerating ? TEXT("True") : TEXT("False"));
        LastPaintLog = CurTime;
    }

    const FVector2D VSz   = Geo.GetLocalSize();
    const float     Short = FMath::Min(VSz.X, VSz.Y);
    const float     PanSz = Short * MapPanelFraction;
    const float     PanX  = (VSz.X-PanSz)*0.5f, PanY = (VSz.Y-PanSz)*0.5f;
    const FSlateRect PanR(PanX,PanY,PanX+PanSz,PanY+PanSz);
    const FSlateRect MapR = ComputeMapRect(Geo);

    // Full-screen overlay
    { FSlateBrush B; B.TintColor=FSlateColor(FLinearColor(0,0,0,0.75f));
      FSlateDrawElement::MakeBox(Out,Layer,MakePaintGeomFull(Geo,VSz),&B,ESlateDrawEffect::None,FLinearColor(0,0,0,0.75f)); }
    ++Layer;
    PaintPanelBackground(Out,Layer,Geo,PanR); Layer+=2;
    PaintMapTexture    (Out,Layer,Geo,MapR);  ++Layer;
    PaintCompass       (Out,Layer,Geo,MapR);  ++Layer;
    PaintOverlayText   (Out,Layer,Geo,PanR); Layer+=4;
    return Layer;
}

void UVoxelMapWidget::PaintPanelBackground(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FSlateRect& PR) const
{
    const FVector2D Pos(PR.Left,PR.Top), Sz(PR.GetSize());
    FSlateBrush Fill; Fill.TintColor=FSlateColor(FLinearColor(0.06f,0.06f,0.08f,0.96f));
    FSlateDrawElement::MakeBox(Out,L,MakePaintGeom(G,Pos,Sz),&Fill,ESlateDrawEffect::None,FLinearColor(0.06f,0.06f,0.08f,0.96f));
    FSlateBrush Brd; const FLinearColor BC(0.3f,0.3f,0.35f,1.f); Brd.TintColor=FSlateColor(BC);
    const float B=2.f;
    auto Line=[&](FVector2D P,FVector2D S){ FSlateDrawElement::MakeBox(Out,L+1,MakePaintGeom(G,P,S),&Brd,ESlateDrawEffect::None,BC); };
    Line(Pos,{Sz.X,B}); Line(Pos+FVector2D(0,Sz.Y-B),{Sz.X,B});
    Line(Pos,{B,Sz.Y}); Line(Pos+FVector2D(Sz.X-B,0),{B,Sz.Y});
}

void UVoxelMapWidget::PaintMapTexture(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FSlateRect& MR) const
{
    const FVector2D Pos(MR.Left,MR.Top), Sz(MR.GetSize());
    if (MapTexture && MapBrush.GetResourceObject())
        FSlateDrawElement::MakeBox(Out,L,MakePaintGeom(G,Pos,Sz),&MapBrush,ESlateDrawEffect::None,FLinearColor::White);
    else
    {
        FSlateBrush PH; PH.TintColor=FSlateColor(FLinearColor(0.1f,0.1f,0.12f,1.f));
        FSlateDrawElement::MakeBox(Out,L,MakePaintGeom(G,Pos,Sz),&PH,ESlateDrawEffect::None,FLinearColor(0.1f,0.1f,0.12f,1.f));
        FSlateDrawElement::MakeText(Out,L+1,MakePaintGeom(G,Pos+Sz*0.4f,{120,24}),
            FText::FromString(TEXT("Generating...")),GetFont(FontSize),ESlateDrawEffect::None,FLinearColor(0.6f,0.6f,0.6f,1.f));
    }
    FSlateBrush Brd; const FLinearColor BC(0.25f,0.25f,0.3f,1.f); Brd.TintColor=FSlateColor(BC);
    const float B=1.f;
    auto Line=[&](FVector2D P,FVector2D S){ FSlateDrawElement::MakeBox(Out,L+1,MakePaintGeom(G,P,S),&Brd,ESlateDrawEffect::None,BC); };
    Line(Pos,{Sz.X,B}); Line(Pos+FVector2D(0,Sz.Y-B),{Sz.X,B});
    Line(Pos,{B,Sz.Y}); Line(Pos+FVector2D(Sz.X-B,0),{B,Sz.Y});
}

void UVoxelMapWidget::PaintCompass(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const FSlateRect& MR) const
{
    const FSlateFontInfo Font = GetFont(FontSize-2);
    const FLinearColor Col(0.9f,0.9f,0.6f,1.f);
    const FVector2D LSz(20,20);
    struct { FVector2D P; const TCHAR* T; } C[] = {
        {{(MR.Left+MR.Right)*0.5f-7.f, MR.Top-20.f},   TEXT("N")},
        {{(MR.Left+MR.Right)*0.5f-5.f, MR.Bottom+4.f}, TEXT("S")},
        {{MR.Left-18.f, (MR.Top+MR.Bottom)*0.5f-8.f},  TEXT("W")},
        {{MR.Right+4.f, (MR.Top+MR.Bottom)*0.5f-8.f},  TEXT("E")},
    };
    for (const auto& Cp : C)
        FSlateDrawElement::MakeText(Out,L,MakePaintGeom(G,Cp.P,LSz),FText::FromString(Cp.T),Font,ESlateDrawEffect::None,Col);
}

void UVoxelMapWidget::PaintOverlayText(FSlateWindowElementList& Out, int32 L,
                                        const FGeometry& G, const FSlateRect& PR) const
{
    const FSlateFontInfo Font=GetFont(), FontBig=GetFont(FontSize+4), FontSm=GetFont(FontSize-2);
    const FLinearColor TC(0.9f,0.9f,0.9f,1.f), DC(0.6f,0.6f,0.6f,1.f), YC(1.f,1.f,0.7f,1.f);
    const FSlateRect MR = ComputeMapRect(G);
    const float TX = PR.Left+14.f;
    float TY = MR.Bottom+10.f;
    const float LH = FontSize+5.f;
    const FVector2D RSz(PR.GetSize().X-28.f, LH+2.f);

    FSlateDrawElement::MakeText(Out,L,MakePaintGeom(G,{TX,PR.Top+8.f},{300,28}),
        FText::FromString(TEXT("WORLD MAP")),FontBig,ESlateDrawEffect::None,YC);

    FSlateDrawElement::MakeText(Out,L+1,MakePaintGeom(G,{TX,TY},RSz),
        FText::FromString(FString::Printf(TEXT("X: %.0f   Y: %.0f   Z: %.0f"),
            PlayerWorldPos.X,PlayerWorldPos.Y,PlayerWorldPos.Z)),
        Font,ESlateDrawEffect::None,TC); TY+=LH;

    FSlateDrawElement::MakeText(Out,L+1,MakePaintGeom(G,{TX,TY},RSz),
        FText::FromString(FString::Printf(TEXT("Radius: %.0f m"),MapWorldRadius/100.f)),
        FontSm,ESlateDrawEffect::None,DC); TY+=LH;

    FSlateDrawElement::MakeText(Out,L+1,MakePaintGeom(G,{TX,TY},RSz),
        FText::FromString(FString::Printf(TEXT("Biome: %s"),*CachedBiomeName)),
        Font,ESlateDrawEffect::None,TC); TY+=LH+4.f;

    // Legend
    const float SwSz=(float)FontSize; float LX=TX;
    for (int32 b=0; b<FVoxelBiomeWeightMap::MaxBiomes; ++b)
    {
        const FLinearColor& BC=FVoxelMapGenerator::BiomeColors[b];
        FSlateBrush SW; SW.TintColor=FSlateColor(BC);
        FSlateDrawElement::MakeBox(Out,L+2,MakePaintGeom(G,{LX,TY+2},{SwSz,SwSz}),&SW,ESlateDrawEffect::None,BC);
        FSlateDrawElement::MakeText(Out,L+2,MakePaintGeom(G,{LX+SwSz+3,TY},{90,LH}),
            FText::FromString(FVoxelMapGenerator::BiomeNames[b]),FontSm,ESlateDrawEffect::None,DC);
        if ((b%2)==0) LX+=110.f; else { LX=TX; TY+=LH; }
    }
    TY+=LH*1.5f;

    FSlateDrawElement::MakeText(Out,L+3,MakePaintGeom(G,{(PR.Left+PR.Right)*0.5f-80.f,TY},{200,LH+2}),
        FText::FromString(TEXT("[ M ]  Close Map")),FontSm,ESlateDrawEffect::None,FLinearColor(0.5f,0.5f,0.5f,1.f));

    if (bGenerating)
        FSlateDrawElement::MakeText(Out,L+3,MakePaintGeom(G,{PR.Right-100.f,PR.Top+10.f},{90,18}),
            FText::FromString(TEXT("Refreshing...")),FontSm,ESlateDrawEffect::None,FLinearColor(0.4f,0.7f,0.4f,1.f));
}
