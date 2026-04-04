# RELEASES

All notable changes to FieldMesh are documented here.
For full technical details, see the internal documentation PDF.

---

## [5.10] — Headless Device Support (T1000-E, RAK WiseMesh TAG)

### New features
- **Headless device support** — `ui-orig` now fully integrated into FieldMesh; devices with no display and a single button (Seeed T1000-E, RAK WiseMesh TAG) are supported as first-class citizens
- **Off-Grid toggle via 5× click** — 5 presses on the single button toggles Off-Grid mode on/off; buzzer feedback is forced regardless of mute state (ascending tone = on, descending = off)
- **SOS alarm via buzzer** — incoming `!SOS` messages trigger a continuous buzzer siren that overrides quiet mode and loops until acknowledged with a short press

### Changes
- All button actions (advert, GPS toggle, buzzer toggle, Off-Grid toggle) now produce forced buzzer feedback regardless of quiet mode — essential for display-less operation
- Ascending tone (`c,e`) = feature enabled; descending tone (`e,c`) = feature disabled — consistent across all actions
- `newMsg()` signature updated to include `is_favorite` parameter — compatible with V5 `AbstractUITask` interface
- `refreshDisplay()` and `isOnRecentOrTrackingPage()` added as explicit no-op overrides — required for `MyMesh` compatibility

### Button mapping (headless devices — single button)
| Press | Action |
|---|---|
| 1× | Wake display / acknowledge SOS alarm |
| 2× | Send advert |
| 3× | Buzzer mute on/off |
| 4× | GPS on/off |
| 5× | Off-Grid mode on/off |
| Long press (first 8 s) | CLI Rescue Mode |
| Long press | Shutdown |

### Technical notes
- Changes confined to `ui-orig/Button.h`, `ui-orig/Button.cpp`, `ui-orig/UITask.h`, `ui-orig/UITask.cpp`
- `ui-new` (ThinkNode M1, Wio Tracker L1) is completely unaffected
- `_buzzer_restore_quiet` flag ensures non-blocking buzzer playback before quiet state is restored

---

## [5.00] — Message History & Text Input

### New features
- **Message history** — received messages are stored in a RAM ring buffer (up to 32 entries) and browsable directly on the device, no phone required
- **Redesigned home screen (page 1)** — shows unread message counter and the name + timestamp of the most recent message from a favourite contact
- **Message filter screen** — accessible via long press on home screen page 1; filter between all messages and favourites
- **Message history screen** — one message per view, newest first; short press = next message, long press = clear all; channel tag (`[CH1]`, `[DM]`) and timestamp shown per message
- **Text input & send (Wio Tracker L1 only)** — compose and send channel messages directly from the device via joystick; character selection, 80-character limit, "Sent!" confirmation after sending
- **Channel select screen (Wio Tracker L1 only)** — choose the target channel before composing a message

### Changes
- `MsgPreviewScreen` (pop-up behaviour on new message) removed and replaced by `MsgHistoryScreen`
- `newMsg()` no longer switches screen automatically on incoming message — shows `+1 new` alert in history view instead
- Unread counter on home screen is now independent from MeshCore's internal sync queue — clearing messages on the device never affects smartphone sync
- `setCurrScreen()` moved from private to public to allow navigation from within screen classes
- New `sendChannelMessage()` wrapper added to `MyMesh`

### Technical notes
- Message storage is RAM-only — messages are lost on reboot (intentional; smartphone sync is unaffected)
- Text send is guarded by `#if UI_HAS_JOYSTICK` throughout — not compiled for ThinkNode M1
- Files changed: `UITask.h`, `UITask.cpp`, `MyMesh.h`, `MyMesh.cpp`, `AbstractUITask.h`

---

## [4.04] — Wio Tracker L1 Support

### New features
- Wio Tracker L1 (Seeed) added as second supported device (OLED display, joystick input)
- All joystick-specific UI behaviour guarded by `#if UI_HAS_JOYSTICK`
- Runtime display height checks (`display.height() >= 128`) used throughout instead of compile-time `#ifdef` guards

---

## [4.02] — SOS Screen Improvements

### Changes
- New 48×48px advert icon (`advert_icon_large`) added to `icons.h` for large displays — original 32×32 icon unchanged
- SOS homepage: larger icon on big displays, red "SOS" heading, redundant `showAlert` on page enter removed
- `SOSSendScreen`: icon size adapts to display size, hint texts use relative Y positions
- `SOSAlertScreen`: font size fallback for long node names (≥15 characters drops from size 2 to size 1)

### Notes
- XBM bitmaps for GxEPD must be bit-reversed per byte (GIMP exports MSB-first, GxEPD reads LSB-first)

---

## [4.01] — Version string and hint text cleanup

### Changes
- Splash screen version string updated to "v4"
- All remaining hint texts aligned to `display.height() - 13` (a few had been missed in V4.00)

---

## [4.00] — UI foundation rework

### Changes
- Splash screen redesigned: two visual groups (MeshCore info top, "FieldMesh" branding bottom), large-display only
- All hint texts and status lines moved from hardcoded pixel values to `display.height() - 13`
- Display size guards unified: `if (display.height() >= 128)` for large-display layouts throughout

---

## [3.08] — RX restart fix after Off-Grid toggle

### Bug fixes
- After `toggleOffGrid()`, the receiver was deaf until the next transmitted packet — fixed by calling `advert()` at the end of `toggleOffGrid()`, which runs through the full RadioLibWrapper TX→RX cycle and correctly restores receiver state

---

## [3.07] — Portability and cleanup

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

---

## [3.05] — Timer reset fix + SOS node name display

### Bug fixes
- Auto-advert timer was being reset to +5 minutes on every phone connect or GPS toggle, even if already running — now only starts the timer if it is not already active
- SOS alert screen was showing the channel name instead of the sending node's name — fixed by extracting the node name from the message text (everything before the first `:`)

---

## [3.01] — SOS system

### New features
- **SOS send**: long press on SOS page opens a two-step confirmation screen; sends `!SOS lat,lon` or `!SOS no GPS` to the first channel whose name contains "sos" (case-insensitive)
- **SOS receive**: incoming `!SOS` messages trigger a dedicated alert screen with the sender's node name and a buzzer alarm that overrides quiet mode and loops until manually acknowledged
- Normal incoming messages are suppressed while the SOS alert screen is active
- `triggerSOS()` compatibility stub added to `AbstractUITask.h`
- Page order after V3: FIRST → RECENT → TRACKING → RADIO → GPS → SOS

### Notes
- `strstr()` used instead of `strncmp()` for SOS detection because MeshCore prepends the node name to channel message text

---

## [2.07] — contact.type serial fix

### Bug fixes
- `writeContactRespFrame()` was sending `contact.type` with bit 4 set to the companion app — app received `type=0x11` instead of `0x01`, breaking contact recognition, messaging, and telemetry
- Fix: mask bit 4 in both `writeContactRespFrame()` (outgoing) and `updateContactFromFrame()` (incoming)

---

## [2.06] — contact.type persistence fix

### Bug fixes
- `saveContacts()` was writing `contact.type` with bit 4 set to flash — fixed by masking before write: `clean_type = c.type & 0x0F`

---

## [2.04 / 2.05] — Flood advert in Off-Grid mode

### Changes
- In Off-Grid mode (`client_repeat=1`), `advert()` now sends a flood packet instead of zero-hop
- `allowPacketForward()` returns `true` when `client_repeat != 0`
- V2.05: compiler ambiguity in `sendFlood()` call fixed with explicit `(uint32_t)0` cast

---

## [2.03] — Off-Grid recommended settings (documentation only)

Recommended EU Off-Grid parameters: 869.4625 MHz / BW 125 kHz / SF 11 / CR 5 — Sub-Band g1, 10% duty cycle, no LBT required. Note: the 865–868 MHz band requires LBT and is not usable.

---

## [2.02] — Off-Grid menu on tracking page

### New features
- **Outdoor settings menu** — long press on TRACKING page opens a menu with GPS-Share toggle, Off-Grid toggle, and Back
- Off-Grid mode switches to 869.4625 MHz / BW125 / SF11 / CR5 and enables client repeat in one step
- Normal radio parameters saved automatically to flash before switching; restored on Off-Grid disable

### Files changed
- `NodePrefs.h`: four new fields (`normal_freq`, `normal_bw`, `normal_sf`, `normal_cr`)
- `DataStore.cpp`: save/load new fields at bytes 90–99
- `MyMesh.h/.cpp`: `isOffGridActive()`, `toggleOffGrid()`
- `UITask.h/.cpp`: `OutdoorMenuScreen`, `outdoor_menu` member, `handleLongPress()` dispatch

---

## [2.01] — Off-Grid frequency correction

### Bug fixes
- EU 433 MHz default was a single illegal point frequency → corrected to 433.050–434.790 MHz
- EU 869 MHz default was in a 0.1% duty cycle sub-band (practically unusable) → corrected to 869.400–869.650 MHz (Sub-Band g1, 10% DC)
- US/AU 915 MHz default was outside the legal band → corrected to 915.000–928.000 MHz

---

## [1.08] — Two separate timestamps

### Changes
- `AdvertPath` struct gains `gps_timestamp` (updated only on GPS adverts) alongside `recv_timestamp` (updated on every advert)
- RECENT page uses `recv_timestamp`; TRACKING page uses `gps_timestamp` for colour coding

---

## [1.07] — RECENT always, TRACKING GPS only

### Changes
- `recv_timestamp` now always updated on advert receive (node always appears on RECENT)
- `has_gps` only set when the advert contains GPS coordinates (filter for TRACKING page)

---

## [1.05] — has_gps flag

### Changes
- Bit 4 (`ADV_LATLON_MASK=0x10`) from the advert payload is stored in `contact.type` in `BaseChatMesh.cpp`
- `AdvertPath` gains `has_gps` field — set when at least one GPS advert has been received from that node
- TRACKING page now filters correctly: only nodes with `has_gps=true` are shown

---

## [1.03] — advert() GPS validity fix

### Bug fixes
- `advert()` was sending GPS coordinates even when GPS was powered off, because `isValid()` without `isEnabled()` still returned the last known fix state

---

## [1.00] — Initial release

### New features
- **Auto-advert timer** — node advertises its position every 5 minutes when GPS sharing is enabled; zero-hop only in normal mode
- **TRACKING page** — shows contacts that have sent a GPS position, with distance (Haversine) and colour coding (green < 5 min, yellow older); favourites only, max. 3 entries
- BLUETOOTH and SHUTDOWN pages removed from home screen rotation

