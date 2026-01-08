# Power Profiling Notes

## Profile Comparison: Jan 4 vs Jan 8, 2026

### Important Context: macOS 26.2 VSync Behavior Change

The profile from **January 4, 2026** (`power_realapp_gameplay_20260104_073256.plist`) contains
anomalous data due to a **macOS 26.2-specific behavior change** where the OS disables VSync
blocking when a window becomes occluded (minimized, hidden behind other windows, or off-screen).

This behavior was **not present in earlier macOS versions** and was discovered after upgrading
to macOS 26.2.

When window occlusion occurred during profiling:
- VSync blocking was removed by the OS
- The render loop spun freely without frame limiting
- CPU usage spiked to ~186% (observed in Activity Monitor)
- Power draw spiked to **28W** (vs normal ~5-8W peak gameplay)

### The Fix (Commit 40af664)

```
Occluded window Event handling falling back to software limiting when app
loses focus to prevent render CPU spin! just found this on MacOS 26.2
Solution will work for any platform!
```

The engine now handles SDL3 window events:
- `SDL_EVENT_WINDOW_OCCLUDED` - Switch to software frame limiting
- `SDL_EVENT_WINDOW_EXPOSED` - Resume normal VSync operation

This is a **macOS 26.2-specific workaround**, but the solution is cross-platform and will
handle similar behavior on any OS that exhibits this pattern.

### Profile Data Comparison

| Metric | Jan 4 (with bug) | Jan 8 (with fix) | Notes |
|--------|------------------|------------------|-------|
| Average Power | 0.71 W | 0.79 W | Old avg artificially low |
| Max Power | 28.00 W | 8.18 W | 28W was bug artifact |
| Std Deviation | 1.37 W | 0.70 W | High variance from spikes |
| Idle Residency | 85.87% | 84.46% | Similar when stable |

### Interpretation

- **The 28W spike in the Jan 4 profile is NOT representative of normal gameplay**
- **The 0.71W average was artificially lowered** by periods where the bug may have been
  triggered followed by recovery
- **The Jan 8 profile (0.79W avg, 8.18W peak)** represents the true power baseline with:
  - Stable 60 FPS gameplay
  - Proper frame limiting under all conditions
  - ~199 NPCs with AI behaviors, collision, pathfinding
  - 500x500 world with full rendering

### Recommendations for Future Profiling

1. **Ensure the game window remains visible and unoccluded** during the entire test
2. **Do not switch to other applications** during power profiling
3. **Use fullscreen mode** if possible to avoid accidental occlusion
4. Profiles taken before commit `40af664` **on macOS 26.2+** should be considered potentially
   contaminated if they show power spikes >15W during normal gameplay
5. Profiles from earlier macOS versions are unaffected by this issue

### Valid Baseline (Post-Fix)

For SDL3 HammerEngine on M3 Pro MacBook Pro:
- **Idle/Menu**: ~0.05W (high C-state residency)
- **Normal Gameplay** (~200 entities): 0.7-1.0W average
- **Peak Gameplay** (heavy AI/pathfinding): 5-8W
- **Idle Residency**: 80-85% (race-to-idle working correctly)

---

## Profile Index

| Date | File | Duration | Notes |
|------|------|----------|-------|
| 2025-12-25 | `*_20251225_*.plist` | Various | Initial profiling, baseline |
| 2025-12-27 | `*_20251227_*.plist` | 10 min | Pre-EDM optimizations |
| 2025-12-30 | `*_20251230_*.plist` | 10 min | Various tests |
| 2026-01-04 | `*_20260104_073256.plist` | 10 min | **Contaminated by occlusion bug** |
| 2026-01-08 | `*_20260108_070830.plist` | 10 min | **Valid baseline with occlusion fix** |
