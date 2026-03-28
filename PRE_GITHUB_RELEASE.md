# FieldMesh v3.08 (pre-GitHub)

> **Pre-GitHub version, originally from 28 March 2026** (approximate, reconstructed from development notes). Developed before this repository existed, based on MeshCore v1.14.1. Documentation only — no firmware or source code available for this version.

**RX restart fix after Off-Grid toggle**

### Bug fixes
- After `toggleOffGrid()`, the receiver was deaf until the next transmitted packet — fixed by calling `advert()` at the end of `toggleOffGrid()`, which runs through the full RadioLibWrapper TX→RX cycle and correctly restores receiver state
