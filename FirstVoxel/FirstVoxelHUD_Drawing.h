// FirstVoxelHUD_Drawing.h
// Extracted drawing helpers for AFirstVoxelHUD.
// Keeps the monolithic DrawHUD() readable by splitting each logical section
// into its own function: title screen, loading screen, gameplay overlay,
// and tool wheel. Each function writes only to the HUD canvas via standard
// DrawRect/DrawText/DrawLine — no UObject dependencies.
#pragma once

#include "CoreMinimal.h"

class AFirstVoxelHUD;
class UFont;

// All helpers receive the owning HUD pointer so they can call DrawRect, DrawText etc.
namespace FirstVoxelHUDDraw
{
    /** Full-screen darkened title/main-menu overlay with keyboard navigation. */
    void DrawTitleScreen(AFirstVoxelHUD* HUD, UFont* Font);

    /** Fullscreen loading overlay with progress bar and chunk grid map. */
    void DrawLoadingScreen(AFirstVoxelHUD* HUD, UFont* Font);

    /** In-game crosshair, controls legend, coordinates, FPS counter. */
    void DrawGameplayOverlay(AFirstVoxelHUD* HUD, UFont* Font);

    /** Radial tool-selection wheel centered on the screen. */
    void DrawToolWheel(AFirstVoxelHUD* HUD, UFont* Font);
}
