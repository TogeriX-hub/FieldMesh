# FieldMesh v1.03 (pre-GitHub)

> **Pre-GitHub version, originally from 26 March 2026** (approximate, reconstructed from development notes). Developed before this repository existed, based on MeshCore v1.14.1. Documentation only — no firmware or source code available for this version.

**advert() GPS validity fix**

### Bug fixes
- `advert()` was sending GPS coordinates even when GPS was powered off, because `isValid()` without `isEnabled()` still returned the last known fix state
