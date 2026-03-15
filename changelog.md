# Changelog

## [Unreleased]
### Fixed
- Prevented editor shutdown freezes by cancelling voxel chunk generation, avoiding post-PIE rebuilds during engine exit, and skipping voxel tick work after shutdown begins.
- Added shutdown-safe guards to the voxel map widget to prevent async refresh work during teardown.
- Added shutdown checks around voxel world startup/timer callbacks to avoid scheduling work while exiting.
- Fixed build errors by updating engine-exit checks and ensuring box component headers are included.
- Corrected right-stick look inversion for gamepad input.

### Added
- Added 4 selectable terrain tools (Dig/Build/Smooth/Flatten) with keyboard 1-4 and D-pad bindings.

### Updated
- Enforced crater biome spawning with voxel-grid snapping and safe spawn height offsets to avoid unsafe placements.
- Tuned Skylands generation with sharper low-terrain falloff, updated altitude/size scaling, and new low-terrain altitude boosts for island shards.
- Tightened skylands probability defaults and reduced cave/cavern carve strength for less over-carving.

### Fixed
- Prevented the editor "Generate World" action from freezing by draining the generation queue with a non-blocking ticker instead of a tight loop.
- Prevented skylands from blanketing low terrain by adding a low-terrain early-out and scaling skyland probability by terrain falloff.