# FieldMesh v2.04 / v2.05 (pre-GitHub)

> **Pre-GitHub version, originally from 27 March 2026** (approximate, reconstructed from development notes). Developed before this repository existed, based on MeshCore v1.14.1. Documentation only — no firmware or source code available for this version.

**Flood advert in Off-Grid mode**

### Changes
- In Off-Grid mode (`client_repeat=1`), `advert()` now sends a flood packet instead of zero-hop
- `allowPacketForward()` returns `true` when `client_repeat != 0`
- V2.05: compiler ambiguity in `sendFlood()` call fixed with explicit `(uint32_t)0` cast
