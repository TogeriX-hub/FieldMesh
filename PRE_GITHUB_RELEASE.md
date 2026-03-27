# FieldMesh v3.01 (pre-GitHub)

> **Pre-GitHub version, originally from 27 March 2026** (approximate, reconstructed from development notes). Developed before this repository existed, based on MeshCore v1.14.1. Documentation only — no firmware or source code available for this version.

**SOS system**

### New features
- **SOS send**: long press on SOS page opens a two-step confirmation screen; sends `!SOS lat,lon` or `!SOS no GPS` to the first channel whose name contains "sos" (case-insensitive)
- **SOS receive**: incoming `!SOS` messages trigger a dedicated alert screen with the sender's node name and a buzzer alarm that overrides quiet mode and loops until manually acknowledged
- Normal incoming messages are suppressed while the SOS alert screen is active
- `triggerSOS()` compatibility stub added to `AbstractUITask.h`
- Page order after V3: FIRST → RECENT → TRACKING → RADIO → GPS → SOS

### Notes
- `strstr()` used instead of `strncmp()` for SOS detection because MeshCore prepends the node name to channel message text
