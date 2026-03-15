// VoxelMapWidget.h
// Top-down world map HUD rendered entirely in C++ via NativePaint.
// No UMG Blueprint required â€” add to viewport and it works immediately.
//
// Layout (drawn every frame inside NativePaint):
//
//   â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ full viewport dark overlay â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
//   â”‚  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ MapPanel (centred square) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”  â”‚
//   â”‚  â”‚  WORLD MAP                                          [N marker]         â”‚  â”‚
//   â”‚  â”‚  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”    â”‚  â”‚
//   â”‚  â”‚  â”‚  [biome-colour texture, player dot at centre, chunk grid]      â”‚    â”‚  â”‚
//   â”‚  â”‚  â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜    â”‚  â”‚
//   â”‚  â”‚  X: 1234   Y: 5678   Z: 890      Biome: Lush Forest                   â”‚  â”‚
//   â”‚  â”‚                                                                         â”‚  â”‚
//   â”‚  â”‚  â–  Forest  â–  Peaks  â–  Cliffs  â–  Mesa  â–  Craters                       â”‚  â”‚
//   â”‚  â”‚                                                                         â”‚  â”‚
//   â”‚  â”‚                    [ M ]  Close Map                                    â”‚  â”‚
//   â”‚  â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜  â”‚
//   â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
//
// Usage:
//   1. Call OpenMap(VoxelWorld, Pawn) to show.
//   2. Call CloseMap() to hide.
//   3. The map refreshes every RefreshInterval seconds while open.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Voxel/VoxelMapGenerator.h"
#include "Voxel/Config/VoxelGenerationConfig.h"
#include "Voxel/Biomes/VoxelBiome.h"
#include "Engine/Texture2D.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/SlateBrush.h"
#include "VoxelMapWidget.generated.h"

class AVoxelWorld;

UCLASS(BlueprintType, Blueprintable)
class FIRSTVOXEL_API UVoxelMapWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // ---- Configuration (editable in BP subclass Details Panel) ----

    /** World-space half-extent of the map view (cm). 50000 = 500m radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Map",
        meta=(ClampMin="5000.0", ToolTip="Half-radius of the world shown on the map (cm). Increase for a larger view."))
    float MapWorldRadius = 50000.f;

    /** Pixel resolution of the generated map texture (square). Higher = more detail, slower. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Map",
        meta=(ClampMin="64", ClampMax="1024", ToolTip="Texture resolution. 256 is a good balance. 512 for high detail."))
    int32 MapResolution = 256;

    /** Seconds between map texture refreshes while the map is open. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Map",
        meta=(ClampMin="0.5", ToolTip="How often the map resamples the world. Lower = more CPU."))
    float RefreshInterval = 3.f;

    /** Size of the rendered map panel as a fraction of the viewport's shorter axis. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Map",
        meta=(ClampMin="0.3", ClampMax="0.95"))
    float MapPanelFraction = 0.75f;

    /** Font size for coordinate / biome text labels. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voxel|Map",
        meta=(ClampMin="8", ClampMax="32"))
    int32 FontSize = 15;

    // ---- Runtime API ----

    /** Show the map, wired to AVoxelWorld for sampling and Pawn for player position. */
    UFUNCTION(BlueprintCallable, Category="Voxel|Map")
    void OpenMap(AVoxelWorld* InVoxelWorld, APawn* InPlayerPawn);

    /** Hide the map and stop refresh timer. */
    UFUNCTION(BlueprintCallable, Category="Voxel|Map")
    void CloseMap();

    /** Returns true when the map is currently visible. */
    UFUNCTION(BlueprintPure, Category="Voxel|Map")
    bool IsMapOpen() const { return bMapOpen; }

    /**
     * Returns the dominant biome name at the player's current world position.
     * Useful for display in Blueprint HUDs.
     */
    UFUNCTION(BlueprintPure, Category="Voxel|Map")
    FString GetPlayerBiomeName() const;

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                              const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                              int32 LayerId, const FWidgetStyle& InWidgetStyle,
                              bool bParentEnabled) const override;
    virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
    virtual bool NativeSupportsKeyboardFocus() const override { return true; }

private:
    // ---- State ----
    UPROPERTY()
    TObjectPtr<AVoxelWorld> CachedVoxelWorld = nullptr;

    UPROPERTY()
    TObjectPtr<APawn> CachedPlayerPawn = nullptr;

    UPROPERTY()
    TObjectPtr<UTexture2D> MapTexture = nullptr;

    bool bMapOpen      = false;
    bool bTextureDirty = false;   // New pixels ready to upload (game thread only)

    /**
     * Set once destruction/shutdown begins so async tasks can early-out safely.
     * Thread-safe since the map refresh runs on background threads.
     */
    FThreadSafeBool bShuttingDown { false };

    /**
     * True while a background pixel-buffer generation is in flight.
     * Read from NativePaint (potentially render thread) and written from the
     * async completion callback (game thread). FThreadSafeBool ensures
     * the read is always atomic with no tearing.
     */
    FThreadSafeBool bGenerating { false };

    mutable FSlateBrush MapBrush;    // Updated whenever MapTexture changes; mutable for NativePaint
    FTimerHandle        RefreshTimerHandle;

    // Pixel buffer filled on background thread, uploaded on game thread.
    TArray<FColor>      PendingPixels;
    FCriticalSection    PixelLock;

    // Cached player world position for NativePaint (updated each tick while open).
    FVector PlayerWorldPos = FVector::ZeroVector;

    // ---- Helpers ----
    void EnsureTexture();
    void RequestRefresh();
    void OnRefreshTimer();
    void UploadPendingPixels();

    /** Returns the square pixel region for the map image inside AllottedGeometry. */
    FSlateRect ComputeMapRect(const FGeometry& Geom) const;

    /** Draws the panel background + border. */
    void PaintPanelBackground(FSlateWindowElementList& Out, int32 Layer,
                              const FSlateRect& PanelRect) const;

    /** Draws the map texture inside MapRect. */
    void PaintMapTexture(FSlateWindowElementList& Out, int32 Layer,
                         const FSlateRect& MapRect) const;

    /** Draws coordinate text, biome name, and legend below the map image. */
    void PaintOverlayText(FSlateWindowElementList& Out, int32 Layer,
                          const FGeometry& Geom, const FSlateRect& PanelRect) const;

    /** Draws compass rose N/S/E/W labels around the map edges. */
    void PaintCompass(FSlateWindowElementList& Out, int32 Layer,
                      const FSlateRect& MapRect) const;

    FSlateFontInfo GetFont(int32 Size = 0) const;
};
