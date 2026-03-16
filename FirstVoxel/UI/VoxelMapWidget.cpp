// VoxelMapWidget.cpp
// Full implementation â€” no Blueprint required.
// UE5.7-clean: uses FCoreStyle::GetDefaultFontStyle for fonts,
// and the FVector2f + FSlateLayoutTransform overload of ToPaintGeometry.

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

// ============================================================
//  Local helpers â€” avoids repeating the UE5.7 new API everywhere
// ============================================================
namespace
{
    // UE5.7 dropped FGeometry::ToPaintGeometry(FVector2D offset, FVector2D size).
    // New API: ToPaintGeometry(FVector2f size, FSlateLayoutTransform(FVector2f offset))
    FORCEINLINE FPaintGeometry MakePaintGeom(const FGeometry& G, FVector2D Offset, FVector2D Size)
    {
        return G.ToPaintGeometry(
            FVector2f((float)Size.X,   (float)Size.Y),
            FSlateLayoutTransform(FVector2f((float)Offset.X, (float)Offset.Y)));
    }

    // Overload for zero-offset (full geometry)
    FORCEINLINE FPaintGeometry MakePaintGeomFull(const FGeometry& G, FVector2D Size)
    {
        return G.ToPaintGeometry(
            FVector2f((float)Size.X, (float)Size.Y),
            FSlateLayoutTransform());
    }
}

// ============================================================
//  Font helper â€” UE5.7 uses FCoreStyle::GetDefaultFontStyle
// ============================================================
FSlateFontInfo UVoxelMapWidget::GetFont(int32 Size) const
{
    const uint16 ActualSize = (uint16)FMath::Clamp(Size > 0 ? Size : FontSize, 6, 72);
    return FCoreStyle::GetDefaultFontStyle("Regular", ActualSize);
}

// ============================================================
//  Lifecycle
// ============================================================
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
    if (UWorld* W = GetWorld())
        W->GetTimerManager().ClearTimer(RefreshTimerHandle);

    Super::NativeDestruct();
}

// ============================================================
//  Runtime API
// ============================================================
void UVoxelMapWidget::OpenMap(AVoxelWorld* InVoxelWorld, APawn* InPlayerPawn)
{
	if (bShuttingDown)
	{
		return;
	}

    CachedVoxelWorld  = InVoxelWorld;
    CachedPlayerPawn  = InPlayerPawn;
    bMapOpen          = true;

    SetVisibility(ESlateVisibility::Visible);
    SetKeyboardFocus();
    EnsureTexture();
    RequestRefresh();

    if (UWorld* W = GetWorld())
    {
        W->GetTimerManager().SetTimer(
            RefreshTimerHandle,
            this, &UVoxelMapWidget::OnRefreshTimer,
            RefreshInterval, /*bLoop=*/true);
    }
}

void UVoxelMapWidget::CloseMap()
{
    bMapOpen = false;
    SetVisibility(ESlateVisibility::Hidden);

    if (UWorld* W = GetWorld())
        W->GetTimerManager().ClearTimer(RefreshTimerHandle);
}

FString UVoxelMapWidget::GetPlayerBiomeName() const
{
    if (!CachedVoxelWorld || !CachedPlayerPawn) return TEXT("Unknown");

    const FVector Pos = CachedPlayerPawn->GetActorLocation();
    const FVoxelGenerationConfig& Config = CachedVoxelWorld->GetEffectiveConfig();
    const FVoxelBiomeWeightMap Weights = FVoxelBiomeManager::GetBiomeWeightsStatic(Pos.X, Pos.Y, Config);
    return FString(FVoxelMapGenerator::BiomeNames[static_cast<uint8>(Weights.GetDominantBiome())]);
}

// ============================================================
//  Input â€” M or Escape closes the map
// ============================================================
FReply UVoxelMapWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
    const FKey Key = InKeyEvent.GetKey();
    if (Key == EKeys::M || Key == EKeys::Escape)
    {
        CloseMap();
        if (APlayerController* PC = GetOwningPlayer())
        {
            FInputModeGameOnly Mode;
            PC->SetInputMode(Mode);
            PC->bShowMouseCursor = false;
        }
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// ============================================================
//  Texture management
// ============================================================
void UVoxelMapWidget::EnsureTexture()
{
    if (MapTexture &&
        MapTexture->GetSizeX() == MapResolution &&
        MapTexture->GetSizeY() == MapResolution)
    {
        return;
    }

    MapTexture = UTexture2D::CreateTransient(MapResolution, MapResolution, PF_B8G8R8A8, TEXT("VoxelMapTex"));
    if (!MapTexture) return;

    MapTexture->NeverStream = true;
    MapTexture->SRGB        = false;
    MapTexture->Filter      = TF_Bilinear;
    MapTexture->AddressX    = TA_Clamp;
    MapTexture->AddressY    = TA_Clamp;

    // Fill grey as placeholder until first generation
    {
        FTexture2DMipMap& Mip = MapTexture->GetPlatformData()->Mips[0];
        FColor* Data = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
        FMemory::Memset(Data, 0x40, MapResolution * MapResolution * sizeof(FColor));
        Mip.BulkData.Unlock();
        MapTexture->UpdateResource();
    }

    MapBrush = FSlateBrush();
    MapBrush.SetResourceObject(MapTexture);
    MapBrush.ImageSize = FVector2D(MapResolution, MapResolution);
}

void UVoxelMapWidget::RequestRefresh()
{
	if (bShuttingDown || bGenerating || !CachedVoxelWorld || !CachedPlayerPawn) return;
    bGenerating = true;

    const FVector Pos          = CachedPlayerPawn->GetActorLocation();
    PlayerWorldPos             = Pos;
    const FVoxelGenerationConfig Config = CachedVoxelWorld->GetEffectiveConfig();
    const float  Radius        = MapWorldRadius;
    const int32  Res           = MapResolution;
    const float  ChunkWorldSz  = (float)CachedVoxelWorld->ChunkSize * CachedVoxelWorld->VoxelSize;

    TSet<FIntVector> ChunkKeys;
    for (const auto& Pair : *CachedVoxelWorld->GetLoadedChunks())
        ChunkKeys.Add(Pair.Key);

	TWeakObjectPtr<UVoxelMapWidget> WeakThis(this);
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[WeakThis, Pos, Config, Radius, Res, ChunkWorldSz, ChunkKeys]()
    {
		if (!WeakThis.IsValid() || WeakThis->bShuttingDown)
		{
			return;
		}

        TArray<FColor> NewPixels;
        FVoxelMapGenerator::GeneratePixelBuffer(
            Pos.X, Pos.Y, Radius, Res, Config, ChunkKeys, ChunkWorldSz, NewPixels);

		AsyncTask(ENamedThreads::GameThread, [WeakThis, NewPixels = MoveTemp(NewPixels)]() mutable
        {
			if (!WeakThis.IsValid() || WeakThis->bShuttingDown)
			{
				return;
			}

			UVoxelMapWidget* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
            {
				FScopeLock Lock(&Self->PixelLock);
				Self->PendingPixels = MoveTemp(NewPixels);
				Self->bTextureDirty = true;
            }
			Self->bGenerating = false;
			Self->UploadPendingPixels();
        });
    });
}

void UVoxelMapWidget::OnRefreshTimer()
{
	if (bMapOpen && CachedPlayerPawn && !bShuttingDown)
    {
        PlayerWorldPos = CachedPlayerPawn->GetActorLocation();
        RequestRefresh();
    }
}

void UVoxelMapWidget::UploadPendingPixels()
{
	if (bShuttingDown || !bTextureDirty || !MapTexture) return;
    if (MapTexture->GetSizeX() != MapResolution) EnsureTexture();

    TArray<FColor> LocalPixels;
    {
        FScopeLock Lock(&PixelLock);
        if (PendingPixels.Num() != MapResolution * MapResolution) return;
        LocalPixels   = MoveTemp(PendingPixels);
        bTextureDirty = false;
    }

    FTexture2DMipMap& Mip = MapTexture->GetPlatformData()->Mips[0];
    FColor* Data = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
    FMemory::Memcpy(Data, LocalPixels.GetData(), LocalPixels.Num() * sizeof(FColor));
    Mip.BulkData.Unlock();
    MapTexture->UpdateResource();
    MapBrush.SetResourceObject(MapTexture);
}

// ============================================================
//  Layout
// ============================================================
FSlateRect UVoxelMapWidget::ComputeMapRect(const FGeometry& Geom) const
{
    const FVector2D Size  = Geom.GetLocalSize();
    const float     Short = FMath::Min(Size.X, Size.Y);
    const float     Panel = Short * MapPanelFraction;
    const float     MapSz = Panel * 0.68f;
    const float     PanX  = (Size.X - Panel) * 0.5f;
    const float     PanY  = (Size.Y - Panel) * 0.5f;
    const float     MapL  = PanX + (Panel - MapSz) * 0.5f;
    const float     MapT  = PanY + Panel * 0.08f;
    return FSlateRect(MapL, MapT, MapL + MapSz, MapT + MapSz);
}

// ============================================================
//  NativePaint
// ============================================================
int32 UVoxelMapWidget::NativePaint(
    const FPaintArgs& Args,
    const FGeometry& AllottedGeometry,
    const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements,
    int32 LayerId,
    const FWidgetStyle& InWidgetStyle,
    bool bParentEnabled) const
{
    if (!bMapOpen) return LayerId;

    const FVector2D ViewSz    = AllottedGeometry.GetLocalSize();
    const float     Short     = FMath::Min(ViewSz.X, ViewSz.Y);
    const float     PanelSz   = Short * MapPanelFraction;
    const float     PanelX    = (ViewSz.X - PanelSz) * 0.5f;
    const float     PanelY    = (ViewSz.Y - PanelSz) * 0.5f;
    const FSlateRect PanelRect(PanelX, PanelY, PanelX + PanelSz, PanelY + PanelSz);
    const FSlateRect MapRect  = ComputeMapRect(AllottedGeometry);

    // 0. Full-screen overlay
    {
        FSlateBrush B;
        B.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.75f));
        FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
            MakePaintGeomFull(AllottedGeometry, ViewSz),
            &B, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.75f));
    }
    ++LayerId;

    PaintPanelBackground(OutDrawElements, LayerId, PanelRect);
    LayerId += 2;

    PaintMapTexture(OutDrawElements, LayerId, MapRect);
    ++LayerId;

    PaintCompass(OutDrawElements, LayerId, MapRect);
    ++LayerId;

    PaintOverlayText(OutDrawElements, LayerId, AllottedGeometry, PanelRect);
    LayerId += 4;

    return LayerId;
}

// ============================================================
//  Sub-painters
// ============================================================
void UVoxelMapWidget::PaintPanelBackground(
    FSlateWindowElementList& Out, int32 Layer, const FSlateRect& PanelRect) const
{
    const FGeometry& G   = GetCachedGeometry();
    const FVector2D  Pos = FVector2D(PanelRect.Left, PanelRect.Top);
    const FVector2D  Sz  = FVector2D(PanelRect.GetSize());

    // Fill
    FSlateBrush Fill;
    Fill.TintColor = FSlateColor(FLinearColor(0.06f, 0.06f, 0.08f, 0.96f));
    FSlateDrawElement::MakeBox(Out, Layer,
        MakePaintGeom(G, Pos, Sz), &Fill, ESlateDrawEffect::None,
        FLinearColor(0.06f, 0.06f, 0.08f, 0.96f));

    // Border lines
    FSlateBrush Border;
    Border.TintColor = FSlateColor(FLinearColor(0.3f, 0.3f, 0.35f, 1.f));
    const FLinearColor BC(0.3f, 0.3f, 0.35f, 1.f);
    const float B = 2.f;
    auto Line = [&](FVector2D P, FVector2D S)
    {
        FSlateDrawElement::MakeBox(Out, Layer + 1, MakePaintGeom(G, P, S), &Border, ESlateDrawEffect::None, BC);
    };
    Line(Pos,                               FVector2D(Sz.X, B));
    Line(Pos + FVector2D(0, Sz.Y - B),      FVector2D(Sz.X, B));
    Line(Pos,                               FVector2D(B, Sz.Y));
    Line(Pos + FVector2D(Sz.X - B, 0),      FVector2D(B, Sz.Y));
}

void UVoxelMapWidget::PaintMapTexture(
    FSlateWindowElementList& Out, int32 Layer, const FSlateRect& MapRect) const
{
    const FGeometry& G   = GetCachedGeometry();
    const FVector2D  Pos = FVector2D(MapRect.Left,  MapRect.Top);
    const FVector2D  Sz  = FVector2D(MapRect.GetSize());

    if (MapTexture && MapBrush.GetResourceObject())
    {
        FSlateDrawElement::MakeBox(Out, Layer,
            MakePaintGeom(G, Pos, Sz), &MapBrush, ESlateDrawEffect::None, FLinearColor::White);
    }
    else
    {
        FSlateBrush PH;
        PH.TintColor = FSlateColor(FLinearColor(0.1f, 0.1f, 0.12f, 1.f));
        FSlateDrawElement::MakeBox(Out, Layer,
            MakePaintGeom(G, Pos, Sz), &PH, ESlateDrawEffect::None,
            FLinearColor(0.1f, 0.1f, 0.12f, 1.f));

        FSlateDrawElement::MakeText(Out, Layer + 1,
            MakePaintGeom(G, Pos + Sz * 0.4f, FVector2D(120.f, 24.f)),
            FText::FromString(TEXT("Generating...")),
            GetFont(FontSize), ESlateDrawEffect::None,
            FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
    }

    // Border
    FSlateBrush Border;
    Border.TintColor = FSlateColor(FLinearColor(0.25f, 0.25f, 0.3f, 1.f));
    const FLinearColor BC(0.25f, 0.25f, 0.3f, 1.f);
    const float B = 1.f;
    auto Line = [&](FVector2D P, FVector2D S)
    {
        FSlateDrawElement::MakeBox(Out, Layer + 1, MakePaintGeom(G, P, S), &Border, ESlateDrawEffect::None, BC);
    };
    Line(Pos,                               FVector2D(Sz.X, B));
    Line(Pos + FVector2D(0, Sz.Y - B),      FVector2D(Sz.X, B));
    Line(Pos,                               FVector2D(B, Sz.Y));
    Line(Pos + FVector2D(Sz.X - B, 0),      FVector2D(B, Sz.Y));
}

void UVoxelMapWidget::PaintCompass(
    FSlateWindowElementList& Out, int32 Layer, const FSlateRect& MapRect) const
{
    const FGeometry& G    = GetCachedGeometry();
    const FSlateFontInfo  Font = GetFont(FontSize - 2);
    const FLinearColor    Col(0.9f, 0.9f, 0.6f, 1.f);
    const FVector2D       LSz(20.f, 20.f);
    const float           Off = 4.f;

    struct { FVector2D P; const TCHAR* L; } C[] =
    {
        { FVector2D((MapRect.Left+MapRect.Right)*0.5f-7.f, MapRect.Top-20.f),              TEXT("N") },
        { FVector2D((MapRect.Left+MapRect.Right)*0.5f-5.f, MapRect.Bottom+Off),            TEXT("S") },
        { FVector2D(MapRect.Left-18.f, (MapRect.Top+MapRect.Bottom)*0.5f-8.f),             TEXT("W") },
        { FVector2D(MapRect.Right+Off,  (MapRect.Top+MapRect.Bottom)*0.5f-8.f),            TEXT("E") },
    };

    for (const auto& Cardinal : C)
    {
        FSlateDrawElement::MakeText(Out, Layer,
            MakePaintGeom(G, Cardinal.P, LSz),
            FText::FromString(Cardinal.L),
            Font, ESlateDrawEffect::None, Col);
    }
}

void UVoxelMapWidget::PaintOverlayText(
    FSlateWindowElementList& Out, int32 Layer,
    const FGeometry& Geom, const FSlateRect& PanelRect) const
{
    const FGeometry&     G         = GetCachedGeometry();
    const FSlateFontInfo Font      = GetFont();
    const FSlateFontInfo FontBig   = GetFont(FontSize + 4);
    const FSlateFontInfo FontSmall = GetFont(FontSize - 2);
    const FLinearColor   TextCol(0.9f, 0.9f, 0.9f, 1.f);
    const FLinearColor   DimCol (0.6f, 0.6f, 0.6f, 1.f);
    const FLinearColor   TitleCol(1.f, 1.f, 0.7f, 1.f);

    const FSlateRect MapRect = ComputeMapRect(Geom);
    const float TextX = PanelRect.Left + 14.f;
    float       TextY = MapRect.Bottom + 10.f;
    const float LineH = (float)FontSize + 5.f;
    const FVector2D RowSz(PanelRect.GetSize().X - 28.f, LineH + 2.f);

    // Title
    FSlateDrawElement::MakeText(Out, Layer,
        MakePaintGeom(G, FVector2D(TextX, PanelRect.Top + 8.f), FVector2D(300.f, 28.f)),
        FText::FromString(TEXT("WORLD MAP")), FontBig, ESlateDrawEffect::None, TitleCol);

    // Coordinates
    FSlateDrawElement::MakeText(Out, Layer + 1,
        MakePaintGeom(G, FVector2D(TextX, TextY), RowSz),
        FText::FromString(FString::Printf(TEXT("X: %.0f   Y: %.0f   Z: %.0f"),
            PlayerWorldPos.X, PlayerWorldPos.Y, PlayerWorldPos.Z)),
        Font, ESlateDrawEffect::None, TextCol);
    TextY += LineH;

    // Radius hint
    FSlateDrawElement::MakeText(Out, Layer + 1,
        MakePaintGeom(G, FVector2D(TextX, TextY), RowSz),
        FText::FromString(FString::Printf(TEXT("Radius: %.0f m"), MapWorldRadius / 100.f)),
        FontSmall, ESlateDrawEffect::None, DimCol);
    TextY += LineH;

    // Biome name
    FSlateDrawElement::MakeText(Out, Layer + 1,
        MakePaintGeom(G, FVector2D(TextX, TextY), RowSz),
        FText::FromString(FString::Printf(TEXT("Biome: %s"), *GetPlayerBiomeName())),
        Font, ESlateDrawEffect::None, TextCol);
    TextY += LineH + 4.f;

    // Legend
    {
        const float SwSz = (float)FontSize;
        float LegX = TextX;
        for (int32 b = 0; b < FVoxelBiomeWeightMap::MaxBiomes; ++b)
        {
            const FLinearColor& BC = FVoxelMapGenerator::BiomeColors[b];
            FSlateBrush SW;
            SW.TintColor = FSlateColor(BC);
            FSlateDrawElement::MakeBox(Out, Layer + 2,
                MakePaintGeom(G, FVector2D(LegX, TextY + 2.f), FVector2D(SwSz, SwSz)),
                &SW, ESlateDrawEffect::None, BC);

            FSlateDrawElement::MakeText(Out, Layer + 2,
                MakePaintGeom(G, FVector2D(LegX + SwSz + 3.f, TextY), FVector2D(90.f, LineH)),
                FText::FromString(FVoxelMapGenerator::BiomeNames[b]),
                FontSmall, ESlateDrawEffect::None, DimCol);

            if ((b % 2) == 0)  LegX += 110.f;
            else { LegX = TextX; TextY += LineH; }
        }
    }
    TextY += LineH * 1.5f;

    // Close hint
    FSlateDrawElement::MakeText(Out, Layer + 3,
        MakePaintGeom(G,
            FVector2D((PanelRect.Left + PanelRect.Right) * 0.5f - 80.f, TextY),
            FVector2D(200.f, LineH + 2.f)),
        FText::FromString(TEXT("[ M ]  Close Map")),
        FontSmall, ESlateDrawEffect::None, FLinearColor(0.5f, 0.5f, 0.5f, 1.f));

    // Refresh indicator
    if (bGenerating)
    {
        FSlateDrawElement::MakeText(Out, Layer + 3,
            MakePaintGeom(G,
                FVector2D(PanelRect.Right - 100.f, PanelRect.Top + 10.f),
                FVector2D(90.f, 18.f)),
            FText::FromString(TEXT("Refreshing...")),
            FontSmall, ESlateDrawEffect::None, FLinearColor(0.4f, 0.7f, 0.4f, 1.f));
    }
}
