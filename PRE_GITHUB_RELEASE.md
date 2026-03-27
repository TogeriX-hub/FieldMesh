# FieldMesh v2.06 (pre-GitHub)

> **Pre-GitHub version, originally from 27 March 2026** (approximate, reconstructed from development notes). Developed before this repository existed, based on MeshCore v1.14.1. Documentation only — no firmware or source code available for this version.

**contact.type persistence fix**

### Bug fixes
- `saveContacts()` was writing `contact.type` with bit 4 set to flash — fixed by masking before write: `clean_type = c.type & 0x0F`
