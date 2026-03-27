# FieldMesh v1.08 (pre-GitHub)

> **Pre-GitHub version, originally from 27 March 2026** (approximate, reconstructed from development notes). Developed before this repository existed, based on MeshCore v1.14.1. Documentation only — no firmware or source code available for this version.

**Two separate timestamps**

### Changes
- `AdvertPath` struct gains `gps_timestamp` (updated only on GPS adverts) alongside `recv_timestamp` (updated on every advert)
- RECENT page uses `recv_timestamp`; TRACKING page uses `gps_timestamp` for colour coding
