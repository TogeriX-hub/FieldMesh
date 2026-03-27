# FieldMesh v2.02 (pre-GitHub)

> **Pre-GitHub version, originally from 27 March 2026** (approximate, reconstructed from development notes). Developed before this repository existed, based on MeshCore v1.14.1. Documentation only — no firmware or source code available for this version.

**Off-Grid menu on tracking page**

### New features
- **Outdoor settings menu** — long press on TRACKING page opens a menu with GPS-Share toggle, Off-Grid toggle, and Back
- Off-Grid mode switches to 869.4625 MHz / BW125 / SF11 / CR5 and enables client repeat in one step
- Normal radio parameters saved automatically to flash before switching; restored on Off-Grid disable

### Files changed
- `NodePrefs.h`: four new fields (`normal_freq`, `normal_bw`, `normal_sf`, `normal_cr`)
- `DataStore.cpp`: save/load new fields at bytes 90–99
- `MyMesh.h/.cpp`: `isOffGridActive()`, `toggleOffGrid()`
- `UITask.h/.cpp`: `OutdoorMenuScreen`, `outdoor_menu` member, `handleLongPress()` dispatch
