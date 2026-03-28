# FieldMesh v3.07 (pre-GitHub)

> **Pre-GitHub version, originally from 28 March 2026** (approximate, reconstructed from development notes). Developed before this repository existed, based on MeshCore v1.14.1. Documentation only — no firmware or source code available for this version.

**Portability and cleanup**

### Changes (3.07a)
- `PIN_BUTTON2` corrected to GPIO 39 in `variant.h` (was 11) — fix moved out of `UITask.cpp` where it did not belong
- `DISP_BACKLIGHT` / `BACKLIGHT_BTN` conflict resolved with `#if defined(DISP_BACKLIGHT) && !defined(BACKLIGHT_BTN)` guard
- All `PIN_BUTTON2` usages wrapped in `#ifdef PIN_BUTTON2`

### Changes (3.07b)
- Alert texts standardised to English: "SOS sent!", "No SOS channel!", "Advert sent!"

### Changes (3.07c)
- Long press on RECENT page triggers an immediate advert send with "Advert sent!" alert
- `isOnRecentPage()` helper added to `HomeScreen`
- `_next_refresh = 0` added in `handleLongPress()` RECENT branch so the alert appears immediately
