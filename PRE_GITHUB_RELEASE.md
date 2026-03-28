# FieldMesh v4.00 (pre-GitHub)

> **Pre-GitHub version, originally from 28 March 2026** (approximate, reconstructed from development notes). Developed before this repository existed, based on MeshCore v1.14.1. Documentation only — no firmware or source code available for this version.

**UI foundation rework**

### Changes
- Splash screen redesigned: two visual groups (MeshCore info top, "FieldMesh" branding bottom), large-display only
- All hint texts and status lines moved from hardcoded pixel values to `display.height() - 13`
- Display size guards unified: `if (display.height() >= 128)` for large-display layouts throughout
