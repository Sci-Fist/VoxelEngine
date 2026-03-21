// VoxelBiomeGenerators.cpp
//
// ── MODULARIZATION NOTE ──────────────────────────────────────────────────────
// This file previously contained all biome generator implementations (~34 KB).
// It has been split into focused sub-files for maintainability:
//
//   VoxelBiomeGenerators_Noise.cpp    — FBM helper
//   VoxelBiomeGenerators_Surface.cpp  — Forest, Desert, Peaks, Cliffs, Mesa,
//                                       Crystal Cavern Delta
//   VoxelBiomeGenerators_Craters.cpp  — Crater system (bowl, rim, uplift,
//                                       melt sheet, ejecta rays, secondaries)
//   VoxelBiomeGenerators_Skylands.cpp — Floating island system with J-curve
//                                       probability (FIX N10)
//
// This file is now intentionally empty.
// All public API is declared in VoxelBiomeGenerators.h and implemented
// in the sub-files above.
// ────────────────────────────────────────────────────────────────────────────

// (intentionally empty)
