# FieldMesh v1.05 (pre-GitHub)

> **Pre-GitHub version, originally from 26 March 2026** (approximate, reconstructed from development notes). Developed before this repository existed, based on MeshCore v1.14.1. Documentation only — no firmware or source code available for this version.

**has_gps flag**

### Changes
- Bit 4 (`ADV_LATLON_MASK=0x10`) from the advert payload is stored in `contact.type` in `BaseChatMesh.cpp`
- `AdvertPath` gains `has_gps` field — set when at least one GPS advert has been received from that node
- TRACKING page now filters correctly: only nodes with `has_gps=true` are shown
