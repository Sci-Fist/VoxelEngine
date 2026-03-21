// FirstVoxelHUD.h
//
// N12: DrawHUD decomposed into focused private helpers.
// Each screen lives in its own method — no more 400-line monolith.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FirstVoxelHUD.generated.h"

class UVoxelPauseMenu;

UCLASS()
class FIRSTVOXEL_API AFirstVoxelHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void BeginPlay() override;
    virtual void DrawHUD()   override;

    UPROPERTY(Transient, BlueprintReadWrite, Category="Voxel")
    bool bShowTitleScreen = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Voxel")
    bool bShowLoadBar = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Voxel")
    float LoadProgress = 0.f;

    UPROPERTY(Transient)
    bool bShowLoadingScreen = false;

    UPROPERTY(Transient)
    int32 TitleSelection = 0;

    UPROPERTY(Transient)
    bool bShowWorldOptions = false;

    UFUNCTION(BlueprintCallable, Category="Voxel|UI")
    void TogglePause();

    class UCanvas* GetCanvas() const { return Canvas; }

    UFUNCTION(BlueprintPure, Category="Voxel|UI")
    bool IsPaused() const;

private:
    UPROPERTY()
    TObjectPtr<UVoxelPauseMenu> PauseMenu = nullptr;

    UPROPERTY(Transient)
    class AVoxelWorld* CachedVoxelWorld = nullptr;

    // ── Screen helpers (N12: extracted from DrawHUD monolith) ──────────────
    void DrawLoadingScreen(UFont* Font);
    void DrawTitleScreen(UFont* Font);
    void DrawGameplayHUD(UFont* Font);
    void DrawToolWheel(UFont* Font);
    void DrawStatsOverlay(UFont* Font);
};
