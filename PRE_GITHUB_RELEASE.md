# FieldMesh v2.01 (pre-GitHub)

> **Pre-GitHub version, originally from 27 March 2026** (approximate, reconstructed from development notes). Developed before this repository existed, based on MeshCore v1.14.1. Documentation only — no firmware or source code available for this version.

**Off-Grid frequency correction**

### Bug fixes
- EU 433 MHz default was a single illegal point frequency → corrected to 433.050–434.790 MHz
- EU 869 MHz default was in a 0.1% duty cycle sub-band (practically unusable) → corrected to 869.400–869.650 MHz (Sub-Band g1, 10% DC)
- US/AU 915 MHz default was outside the legal band → corrected to 915.000–928.000 MHz
