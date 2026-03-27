# FieldMesh v3.05 (pre-GitHub)

> **Pre-GitHub version, originally from 27 March 2026** (approximate, reconstructed from development notes). Developed before this repository existed, based on MeshCore v1.14.1. Documentation only — no firmware or source code available for this version.

**Timer reset fix + SOS node name display**

### Bug fixes
- Auto-advert timer was being reset to +5 minutes on every phone connect or GPS toggle, even if already running — now only starts the timer if it is not already active
- SOS alert screen was showing the channel name instead of the sending node's name — fixed by extracting the node name from the message text (everything before the first `:`)
