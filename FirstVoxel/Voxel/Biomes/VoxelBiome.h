// =============================================================================
// VoxelBiome.h
// =============================================================================
//
// Core biome definitions for the FirstVoxel procedural terrain system.
// This header is included by almost every voxel file, so it is kept
// deliberately minimal -- no heavy engine headers, no config dependencies.
//
// -- TYPES DEFINED HERE -------------------------------------------------------
//   EVoxelBiome          -- 7-value enum of surface biome names
//   FVoxelBiomeWeightMap -- per-biome float weights that always sum to 1.0
//
// -- BIOME BEHAVIORS AND CHARACTERISTICS --------------------------------------
//
//  Forest (EVoxelBiome::Forest):
//    - Base biome with moderate terrain variation
//    - Default fallback when no other biome has weight
//    - Supports standard vegetation and foliage systems
//    - Smooth to rolling terrain with gentle slopes
//
//  Peaks (EVoxelBiome::Peaks):
//    - High-altitude mountainous regions
//    - Steep slopes and sharp ridgelines
//    - Contributes to roughness signal for skyland generation
//    - Often combined with Cliffs for dramatic terrain
//
//  Cliffs (EVoxelBiome::Cliffs):
//    - Vertical rock faces and steep drop-offs
//    - High roughness contribution for skyland systems
//    - Creates dramatic elevation changes
//    - Often found at biome transition boundaries
//
//  Mesa (EVoxelBiome::Mesa):
//    - Flat-topped elevated plateaus with steep sides
//    - Distinctive tableland formations
//    - Creates dramatic elevation contrasts
//    - Often found in arid/desert transition zones
//
//  Craters (EVoxelBiome::Craters):
//    - Impact or volcanic crater formations
//    - Bowl-shaped depressions with raised rims
//    - Creates localized terrain features
//    - Often generates crater lakes when combined with water systems
//
//  Desert (EVoxelBiome::Desert):
//    - Arid regions with minimal vegetation
//    - Sandy or rocky terrain with dunes
//    - Often combined with Mesa for desert plateau effects
//    - Supports oasis generation in low-lying areas
//
//  Ocean (EVoxelBiome::Ocean):
//    - Large bodies of water at sea level
//    - Flat or gently rolling underwater terrain
//    - Special handling for water material and physics
//    - Often serves as the base level for other biomes
//
// -- DESIGN NOTES -------------------------------------------------------------
//
//  Weights are stored as named float members (Forest, Peaks, ...) rather than
//  an array. This was a deliberate refactor: the old float Weights[6] array
//  was a fragile second copy that could silently desync from the named members
//  if any code bypassed SetWeight(). Named members with switch-based accessors
//  are safer and the compiler eliminates the switch for constant enum values.
//
//  GetRoughness() = Peaks + Cliffs is a frequently needed signal for the
//  skylands system (high roughness -> bigger, more common islands) and is
//  inlined here to avoid a GetBiomeWeightsStatic() call at the call sites.
//
//  Biome blending occurs through weighted interpolation of height fields,
//  allowing smooth transitions between different terrain types. The sum of
//  all weights is always normalized to 1.0 for consistent blending behavior.
//
// -- EXTENDING THIS FILE ------------------------------------------------------
//  When adding a new biome:
//    1. Add an entry to EVoxelBiome (before the last value to keep uint8
//    order).
//    2. Increment MaxBiomes.
//    3. Add a named float member.
//    4. Update SetWeight, GetWeight, GetDominantBiome, Normalize, operator[].
//    5. Update GBiomeOrder[] in VoxelGeneratorTask.cpp (assert enforces this).
//    6. Add biome-specific generation logic in VoxelBiomeGenerators.cpp
//    7. Update material and foliage configuration in VoxelGenerationConfig.h
// =============================================================================
#pragma once

#include "CoreMinimal.h"
#include "VoxelBiome.generated.h"

// Forward-declare only what this header actually uses.
// Heavy includes (DataAsset, MaterialInterface, StaticMesh) belong
// in the files that actually need those types, not here.
struct FVoxelGenerationConfig;

// ============================================================
//  EVoxelBiome
//  Enumerates all surface biomes that participate in the 2D
//  biome blend. Skylands are not a surface biome and are handled
//  separately in the density generator.
//
//  Biome Selection Guidelines:
//  - Use Forest as the default/base biome for general terrain
//  - Combine Peaks + Cliffs for mountainous regions
//  - Use Mesa for elevated plateaus and tablelands
//  - Apply Craters for impact/volcanic features
//  - Select Desert for arid regions with minimal vegetation
//  - Use Ocean for large water bodies and sea-level areas
// ============================================================
UENUM(BlueprintType)
enum class EVoxelBiome : uint8 {
  /** Base biome with moderate terrain variation and standard vegetation */
  Forest UMETA(DisplayName="Forest"),

  /** High-altitude mountainous regions with steep slopes and sharp ridgelines */
  Peaks UMETA(DisplayName="Peaks"),

  /** Vertical rock faces and steep drop-offs, high roughness contribution */
  Cliffs UMETA(DisplayName="Cliffs"),

  /** Flat-topped elevated plateaus with steep sides, tablelands */
  Mesa UMETA(DisplayName="Mesa"),

  /** Impact or volcanic crater formations with bowl-shaped depressions */
  Craters UMETA(DisplayName="Craters"),

  /** Arid regions with minimal vegetation, sandy or rocky terrain */
  Desert UMETA(DisplayName="Desert"),

  /** Large bodies of water at sea level with flat underwater terrain */
  Ocean UMETA(DisplayName="Ocean")
};

// ============================================================
//  FVoxelBiomeWeightMap
//  Stores per-biome weights that always sum to 1.0.
//  Used to blend surface heights and to drive skyland roughness.
//
//  Design note: weights are stored exclusively as named float members
//  (Forest, Peaks, …). The old duplicate float Weights[6] array has
//  been removed — it was a fragile second copy that could silently
//  desync from the named members if SetWeight() was bypassed.
//  Use operator[] or GetWeight() for indexed access instead.
// ============================================================
struct FIRSTVOXEL_API FVoxelBiomeWeightMap {
  // Per-biome weights. Always normalized to sum to 1.0 after a Normalize()
  // call.
  float Forest = 0.f;
  float Peaks = 0.f;
  float Cliffs = 0.f;
  float Mesa = 0.f;
  float Craters = 0.f;
  float Desert = 0.f;
  float Ocean = 0.f;

  /** Number of surface biomes tracked by this map. */
  static constexpr int32 MaxBiomes = 7;

  /** Set the weight for a specific biome (clamped to >= 0). */
  void SetWeight(EVoxelBiome Biome, float Weight) {
    Weight = FMath::Max(0.f, Weight);
    switch (Biome) {
    case EVoxelBiome::Forest:
      Forest = Weight;
      break;
    case EVoxelBiome::Peaks:
      Peaks = Weight;
      break;
    case EVoxelBiome::Cliffs:
      Cliffs = Weight;
      break;
    case EVoxelBiome::Mesa:
      Mesa = Weight;
      break;
    case EVoxelBiome::Craters:
      Craters = Weight;
      break;
    case EVoxelBiome::Desert:
      Desert = Weight;
      break;
    case EVoxelBiome::Ocean:
      Ocean = Weight;
      break;
    }
  }

  /** Get the weight for a specific biome. */
  float GetWeight(EVoxelBiome Biome) const {
    switch (Biome) {
    case EVoxelBiome::Forest:
      return Forest;
    case EVoxelBiome::Peaks:
      return Peaks;
    case EVoxelBiome::Cliffs:
      return Cliffs;
    case EVoxelBiome::Mesa:
      return Mesa;
    case EVoxelBiome::Craters:
      return Craters;
    case EVoxelBiome::Desert:
      return Desert;
    case EVoxelBiome::Ocean:
      return Ocean;
    default:
      return 0.f;
    }
  }

  /**
   * Normalize all weights so they sum to 1.0.
   * If all weights are zero (degenerate case), defaults to pure Forest.
   */
  void Normalize() {
    const float Sum = Forest + Peaks + Cliffs + Mesa + Craters + Desert + Ocean;
    if (Sum > 1e-6f) {
      const float InvSum = 1.f / Sum;
      Forest *= InvSum;
      Peaks *= InvSum;
      Cliffs *= InvSum;
      Mesa *= InvSum;
      Craters *= InvSum;
      Desert *= InvSum;
      Ocean *= InvSum;
    } else {
      // Degenerate: no biome has any weight — default to Forest.
      Forest = 1.f;
    }
  }

  /** Returns the biome with the highest weight. */
  EVoxelBiome GetDominantBiome() const {
    float MaxW = Forest;
    EVoxelBiome Best = EVoxelBiome::Forest;
    if (Peaks > MaxW) {
      MaxW = Peaks;
      Best = EVoxelBiome::Peaks;
    }
    if (Cliffs > MaxW) {
      MaxW = Cliffs;
      Best = EVoxelBiome::Cliffs;
    }
    if (Mesa > MaxW) {
      MaxW = Mesa;
      Best = EVoxelBiome::Mesa;
    }
    if (Craters > MaxW) {
      MaxW = Craters;
      Best = EVoxelBiome::Craters;
    }
    if (Desert > MaxW) {
      MaxW = Desert;
      Best = EVoxelBiome::Desert;
    }
    if (Ocean > MaxW) {
      Best = EVoxelBiome::Ocean;
    }
    return Best;
  }

  /**
   * Combined roughness signal: sum of Peaks and Cliffs weights.
   * Used by the skylands layer to scale island altitude and size.
   * Range [0, 1] after normalization.
   */
  float GetRoughness() const { return Peaks + Cliffs; }

  /** Index-based mutable access. Order matches EVoxelBiome cast to uint8. */
  float &operator[](int32 Index) {
    switch (Index) {
    case 0:
      return Forest;
    case 1:
      return Peaks;
    case 2:
      return Cliffs;
    case 3:
      return Mesa;
    case 4:
      return Craters;
    case 5:
      return Desert;
    case 6:
      return Ocean;
    default:
      return Forest;
    }
  }

  /** Index-based const access. Order matches EVoxelBiome cast to uint8. */
  const float &operator[](int32 Index) const {
    switch (Index) {
    case 0:
      return Forest;
    case 1:
      return Peaks;
    case 2:
      return Cliffs;
    case 3:
      return Mesa;
    case 4:
      return Craters;
    case 5:
      return Desert;
    case 6:
      return Ocean;
    default:
      return Forest;
    }
  }
};
