# FieldMesh v2.07 (pre-GitHub)

> **Pre-GitHub version, originally from 27 March 2026** (approximate, reconstructed from development notes). Developed before this repository existed, based on MeshCore v1.14.1. Documentation only — no firmware or source code available for this version.

**contact.type serial fix**

### Bug fixes
- `writeContactRespFrame()` was sending `contact.type` with bit 4 set to the companion app — app received `type=0x11` instead of `0x01`, breaking contact recognition, messaging, and telemetry
- Fix: mask bit 4 in both `writeContactRespFrame()` (outgoing) and `updateContactFromFrame()` (incoming)
