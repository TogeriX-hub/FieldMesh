# RELEASES

All notable changes to FieldMesh are documented here.
For how to use the features, see the [README](README.md).

## Version numbers

In November 2026 all FieldMesh releases were renumbered to semantic versioning (MAJOR.MINOR.PATCH). Only the numbers changed. Each entry below gives its old number; 2.2.0 follows 2.1.0 (formerly V6.1).

| Old | New |
|---|---|
| V1.00 – V2.01 | 1.0.0 – 1.0.5 |
| V2.02 – V2.07 | 1.1.0 – 1.2.2 |
| V3.01 – V3.08 | 1.3.0 – 1.4.1 |
| V4.00 – V4.04 | 1.5.0 – 1.6.0 |
| V5.00 (tag v5.09) – V5.3 | 1.7.0 – 1.10.0 |
| V6.0 / V6.1 | 2.0.0 / 2.1.0 |

---

## [2.1.0] — GPS Duty Cycle, Smarter Tracking & FieldMesh Repeater

*27 October 2026 · formerly V6.1*

Still based on MeshCore v1.18.0. One firmware file per device for all regions.

### New Features

- **GPS duty cycle** — while GPS is on, the receiver takes a fix every 2 minutes and sleeps in between (timing modelled on Meshtastic). Hard sleep on T1000-E, MeshTracker X1 and Heltec Mesh Node T1, soft sleep via the standby pin on ThinkNode M1 and the Wio Tracker L1 family. Boards whose GPS cannot be switched off keep it running as in 2.0.0. T1000-E with GPS on: about 2–2½ days in our tests (no exact current measurement)
- **Adaptive search time** — the search is graded by the satellites in view: 15 s indoors, up to 30 s at a window, up to 120 s when satellites are strong enough to decode, up to 240 s under clear sky; a longer search stops early when the sky is gone. Failed long searches back off 4/8/16 min, never more
- **All satellite systems** — GPS, GLONASS, Galileo, BeiDou (and QZSS) are switched on where the receiver supports them; NMEA output reduced to the sentences FieldMesh needs
- **Tracking cadence** (GPS-Share on) — adverts are sent when you moved ≥ 100 m (at most every 2 min) plus a heartbeat every 10 min, instead of a fixed timer. Replaces issue #28
- **Fast movement** — at ≥ 30 km/h (car, bike) the GPS measures every 60 s and tracking may send every 60 s; back to 2 min three minutes after the last fast fix. Applies whenever GPS is on
- **Fresh positions only** — adverts and SOS only include a fix that is at most 2 minutes old
- **GPS and Tracking pages** keep the GPS awake while they are on screen (plus 60 s)
- **ThinkNode M1 GPS switch** — the GPS runs only when the hardware switch and the setting are on; switching it on also turns the setting on
- **Off-Grid region chosen automatically** — below 900 MHz → 869.4625 MHz (EU/UK), 900 MHz and above → 916.4625 MHz (US/AU). The alert shows the region ("Off-Grid: ON (EU)"). No separate US firmware any more
- **Identity backup** — node identity and settings are copied to a second file system (QSPI flash on M1, L1, X1; a separate internal flash area on the T1000-E) and restored automatically if the main copy is lost. Protects against the identity reset some users saw when updating to 2.0.0. A factory reset also clears the backup
- **SOS with battery level** — `!SOS 48.7636,9.1621 bat78%` / `!SOS no GPS bat78%`
- **FieldMesh Repeater for the Station G2** — a normal MeshCore repeater with:
  - **Off-Grid switch** (`set offgrid on|off`, G2 menu, dashboard), applied live without a reboot
  - **Message board** — room server as a second identity next to the repeater (`set roomserver <name>`), posts and users survive a restart
  - **Room commands** — `!weather` / `!wetter`, `!ping`, `!status` for everyone; `!commands` and `/<CLI command>` for the repeater admin
  - **SOS on the board** — `!SOS` heard in the box's SOS channels is posted to the board
  - **WiFi dashboard** — open WiFi with board, SOS list, nodes (list and radar), network statistics, team favourites and an admin setup page; reachable at `http://192.168.4.1` or `http://<room name>.local`
  - **Weather** — phones with mobile data in the dashboard WiFi load a 48 h forecast (Open-Meteo); `!weather` answers in English (metric or imperial), `!wetter` in German
  - **Firmware update from the dashboard**, G2 menu (Status, Off-Grid, Room, Room reset, WiFi, Power off), larger app partitions

### Changes

- **SOS alarm only in SOS channels** — `!SOS` raises the alarm only in a channel whose name contains "sos" (the same rule as for sending); in other channels, e.g. Public, it is shown as a normal message
- Release files: 3 firmware files (M1, L1, T1000-E) instead of 6, plus the Station G2 repeater
- Splash screen shows "v2.1.0" on the ThinkNode M1 (release file rebuilt in November 2026; the original build showed "v6.1")
- Outdated PDF documentation (1.0.0–1.6.0) and the duplicate FAQ removed — the README is the documentation

### Bug Fixes

- **Flood-routed DMs** are tagged `[DM]` in the message history instead of `[CH]`
- **"!SOS kein GPS"** is now sent as `!SOS no GPS`
- **E-ink display after auto-off (ThinkNode M1)** — since 2.0.0 the screen was only partly refreshed after the display turned off and on again. Fixed with the upstream MeshCore dev change (display off no longer clears the controller buffer or hibernates the panel)

### Updating from 2.0.0

- Settings, contacts and channels are kept; the identity backup is created on the first start
- Updating from 1.x: export your private key in the MeshCore app first (see the 2.0.0 known issue)
- Station G2: the first installation of the FieldMesh Repeater needs the full image at 0x0 — see the README

---

## [2.0.0] — MeshCore v1.18.0 Base

*18 October 2026 · formerly V6.0*

FieldMesh now runs on MeshCore v1.18.0 (previously v1.15.0). All FieldMesh features are kept and sit on top of the current upstream firmware. New features will follow in 2.x.

### New from upstream (MeshCore v1.16 – v1.18)

- **Repeater discovery page** (`ui-new`) — new home screen page before the SOS page; scans for repeaters in direct range and shows their signal strength
- **Charging indicator** (`ui-new`) — lightning bolt while charging, plug when full, next to the battery icon
- **Startup tone** — short tune on boot on all devices with a buzzer (respects buzzer mute)
- **Low-battery shutdown** — no longer shuts down while externally powered; the warning is shown on all display types
- **Settings stored as JSON** (`/prefs.json`) — migrated automatically from the old binary format on first boot
- **Power saving on nRF52** — CPU sleeps between events
- **T1000-E** — RGB status LED and vibration motor support from upstream `ui-orig`
- Plus all other upstream changes: multi-interface companion (BLE/USB/WiFi), companion CLI, 5-byte ACKs, reserved anonymous contact slots, new display colour API

### Changes

- **UI decoupled from MyMesh** — MeshCore v1.18 introduced a listener interface; FieldMesh's message handling (history only when the app is not connected, online node counter, SOS detection, favourites, advert refresh on RECENT/TRACKING) moved from `MyMesh.cpp` into the UI (`ui-new` and `ui-orig`). `MyMesh.cpp` is now much closer to upstream
- **Off-Grid return parameters** are stored in their own JSON object `"fm"` in the settings file and migrated from 1.x
- **GPS flag of adverts** is now passed via `BaseChatMesh::lastAdvertHasLatLon()` instead of bit 4 (`0x10`) in `contact.type` — `contact.type` is no longer modified, the three masking points are gone
- **Static BLE PIN** moved to `main.cpp` (upstream moved the PIN logic there)
- Off-Grid uses the upstream radio wrapper call `radio_driver.setParams()` (upstream removed `radio_set_params()`)
- Splash screen shows "v2.0.0" on the ThinkNode M1 (release files rebuilt in November 2026; the original build showed "v6")

### Bug Fixes

- **Room server login with GPS adverts** — a room server whose last advert contained GPS had `type = 0x13` instead of `0x03` (bit 4), so the login was sent in the wrong format; fixed by the new GPS flag approach
- **Favourite detection with a full contact list** — `isSenderFavorite()` now also checks the last 8 contacts (upstream added 8 reserved slots at the start of the contact table)
- **CLI text messages** (`TXT_TYPE_CLI_COMMAND`, new in upstream) are not shown in the message history
- `_msgcount` is now initialised in `UITask` (`ui-new`)

### Updating from 1.x (formerly V5.x)

> ⚠️ **Known issue:** On some devices, updating from 1.x to 2.0.0 reset the node identity (new public key) and settings (name, radio parameters). Contacts and channels were kept. **Before updating, export your private key in the MeshCore app** so you can re-import it if needed. Investigation in progress.

- Contacts and channels are kept (file format unchanged)
- Settings are migrated to `/prefs.json` — but see the known issue above
- New home screen page: page indicator shows one more dot

---

## [1.10.0] — MeshCore v1.15.0 Base

*28 April 2026 · formerly V5.3*

### Changes

- **MeshCore upstream alignment** — codebase updated to MeshCore v1.15.0 (default flood scope / regions, BLE DFU updates for nRF52 companions, button polarity fixes and more); no FieldMesh-specific feature changes in this release

---

## [1.9.0] — HomeScreen Overhaul & Reliability Fixes

*9 April 2026 · formerly V5.2*
 
### New Features
 
- **Online node counter on HomeScreen** — The FIRST page now shows how many nodes have been heard recently, combining advert senders and message senders into a single deduplicated count with a 30-minute window (`updateOnlineNode()`, `countOnlineNodes()` with 16-entry LRU cache)
- **Node counter icon** — The online count is now displayed with a filled circle icon (`nodes_icon`, 8×8px) instead of plain `[X]` text
- **Mode indicator on HomeScreen** — GPS-SHARE and OFF-GRID modes are shown directly on the FIRST page
- **Real channel name in message history** — `MsgHistoryScreen` and `FavPreviewScreen` now show the actual channel name (e.g. `Public`) instead of a hardcoded `[CH]` tag; the channel name is threaded from `MyMesh` through the full `newMsg()` call chain
- **Channel messages update node counter while connected** — When the phone app is connected, incoming channel messages now also update the active node counter, matching the behaviour for direct messages
 
### Bug Fixes
 
- **HomeScreen FIRST page: advert node cap removed** — `adv_fresh` now uses a dedicated `ADVERT_PATH_TABLE_SIZE` (16) buffer instead of the 4-entry RECENT display array, removing the artificial cap on advert-based nodes in the online counter
- **HomeScreen FIRST page: `formatRelTime()` undefined behaviour fixed** — Replaced signed `int` cast (UB when `now < then`) with safe `uint32_t` arithmetic
- **HomeScreen FIRST page: favourite name clipping fixed** — Long favourite names on small displays now use `drawTextEllipsized()` instead of plain `print()`, preventing silent clipping
- **HomeScreen FIRST page: MSG counter hidden when app is connected** — The `MSG:X` counter no longer shows while the smartphone app is connected
- **Favourite indicator not clearing after reading all messages** — The `* FavName time` line on HomeScreen was not reset when the user deleted all messages; fixed by calling `clearFavoriteCache()` from `deleteEntry()` and `clearAll()` in `MsgHistoryScreen`
- **Stale favourite info after app disconnect** — `_latest_fav_name` and `_latest_fav_time` are now reset on app sync, preventing old contact info from reappearing on HomeScreen after disconnect
- **Double-counting nodes in online counter** — A node heard via both an advert and a message was counted twice; fixed with `countOnlineNodesExcluding()`, which only counts message-cache entries not already present in the fresh advert list
- **Channel message title bar showed sender name redundantly** — For channel messages, the sender name was shown both in the title bar and in the message body; the title bar now shows only the channel name for channel messages and `[DM] NodeName` for direct messages
- **Favourite detection broken for nodes with `|` in name** — Node names containing ` | ` (e.g. `Juno48 | DE.BW`) caused incorrect short-name extraction, so channel messages from such nodes were never recognised as favourites; fixed by using `": "` as the separator (consistent with MeshCore channel format `NodeName: text`)
- **BLE initialization handshake flood fixed** — After `onSecured()`, async pushes (`MSG_WAITING`, `PATH_UPDATED`) fired freely before `CMD_APP_START` arrived, filling all 12 `send_queue` slots and causing `RESP_SELF_INFO` to be silently dropped (app timeout); fixed by introducing `_app_initialized` flag gating all async pushes, plus `isWriteBusy()` guard on `MSG_WAITING` pushes
- **Adaptive row limits on RECENT and TRACKING pages** — Both pages now use a `max_y` variable based on `display.height()` instead of hardcoded pixel values
---

## [1.8.0] — Headless Device Support (T1000-E, RAK WiseMesh TAG)

*4 April 2026 · formerly V5.1*

### New features
- **Headless device support** — `ui-orig` now fully integrated into FieldMesh; devices with no display and a single button (Seeed T1000-E, RAK WiseMesh TAG) are supported as first-class citizens
- **Off-Grid toggle via 5× click** — 5 presses on the single button toggles Off-Grid mode on/off; buzzer feedback is forced regardless of mute state (ascending tone = on, descending = off)
- **SOS alarm via buzzer** — incoming `!SOS` messages trigger a continuous buzzer siren that overrides quiet mode and loops until acknowledged with a short press

### Changes
- All button actions (advert, GPS toggle, buzzer toggle, Off-Grid toggle) now produce forced buzzer feedback regardless of quiet mode — essential for display-less operation
- Ascending tone (`c,e`) = feature enabled; descending tone (`e,c`) = feature disabled — consistent across all actions
- `newMsg()` signature updated to include `is_favorite` parameter — compatible with the 1.7 `AbstractUITask` interface
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

## [1.7.0] — Message History & Text Input

*3 April 2026 · formerly V5.00 (tag v5.09)*

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

## [1.6.0] — Wio Tracker L1 Support

*30 March 2026 · formerly V4.04*

### New features
- Wio Tracker L1 (Seeed) added as second supported device (OLED display, joystick input)
- All joystick-specific UI behaviour guarded by `#if UI_HAS_JOYSTICK`
- Runtime display height checks (`display.height() >= 128`) used throughout instead of compile-time `#ifdef` guards

---

## [1.5.3] — Backlight & Display Logic Overhaul

*30 March 2026 · formerly V4.03*

### Fixes
- `-D AUTO_OFF_MILLIS=0` removed from the ThinkNode M1 `platformio.ini` (added in 1.0.0) — the display sleeps again after 15 seconds of inactivity
- 5-minute backlight auto-off timer (1.5.0) removed — no longer needed now that the display sleeps
- Backlight follows the `_backlight_on` state on every wake-up (`checkDisplayOn()`), overriding the GxEPD driver

### Changes
- Backlight is decoupled from display wake events: incoming messages and adverts wake the display without touching the backlight
- `refreshDisplay()` — wakes the display briefly when an advert arrives while the RECENT or TRACKING page is shown; also works while a phone is connected (no `hasConnection()` check)

### Technical notes
- `refreshDisplay()` and `isOnRecentOrTrackingPage()` added to `AbstractUITask` as virtual methods with empty defaults; called from `MyMesh::onDiscoveredContact()` (inside `#ifdef DISPLAY_CLASS`)
- `_backlight_off_at` member removed from `UITask`
- Files changed: `AbstractUITask.h`, `MyMesh.cpp`, `ui-new/UITask.h`, `ui-new/UITask.cpp`, `variants/thinknode_m1/platformio.ini`

---

## Versions before GitHub (1.0.0 – 1.5.2)

1.0.0 to 1.5.2 (formerly V1.00 to V4.02) were developed between 26 and 28 March 2026, before this repository existed, on **MeshCore v1.14.1**, for the **Elecrow ThinkNode M1** only. The first upload to GitHub (28 March 2026, commit `1093236`) already contains 1.5.2. The GitHub releases of these versions are documentation only (no firmware, no source); dates are approximate, reconstructed from the development notes.

Some internal builds of that time were never released and keep their old numbers in the notes: V1.01/V1.02 (see 1.0.1), V1.04 and V1.06 (failed, replaced by 1.0.2 and 1.0.3), V3.02–V3.04 (bug-fix rounds between 1.3.0 and 1.3.1). During development the RX fix of 1.4.1 was briefly called "V4.07" — the code comment still says so. Older code comments also still use the old numbers (e.g. "V3.05", "V5.03").

Several mechanisms of this period were replaced later. They are marked *Superseded* below so the old notes are not mistaken for the current design.

---

## [1.5.2] — SOS Screen Improvements

*28 March 2026 — first upload to GitHub · formerly V4.02*

All 1.5.x changes before 1.5.3 are display-only (`ui-new/UITask.cpp`, `ui-new/icons.h`): no logic or class structure changes. Layouts use `display.height()` / `display.width()`, never absolute pixels; large displays are detected at runtime with `display.height() >= 128` (M1), everything else uses the compact layout.

### Changes
- New 48×48 px icon `advert_icon_large` in `icons.h` (288 bytes, 6 bytes × 48 rows), used only on large displays; the original 32×32 `advert_icon` is unchanged
- **SOS home page:** on large displays "SOS" in size 3 (red) at Y=30, large icon at `display.height()/2 - 24`; small displays keep the 32×32 icon at Y=16. Hint "SOS: long press" at `display.height() - 13`. The redundant `showAlert` on page enter was removed
- **`SOSSendScreen`:** icon vertically centred (`display.height()/2 - icon_size/2`), 48×48 on large and 32×32 on small displays; idle and confirmation hints relative to `display.height()`
- **`SOSAlertScreen`:** on large displays icon at Y=16 below the title, node name centred at Y=72. Node names with 15 or more characters drop from size 2 to size 1 (size 2 fits about 16 characters on 200 px; 15 leaves room for wide letters such as M and W)

### Notes
- XBM bitmaps for GxEPD must be bit-reversed per byte: GIMP exports MSB-first, GxEPD reads LSB-first. Conversion per byte: `int(format(byte, '08b')[::-1], 2)`
- The splash screen in the uploaded code still shows "v3" — the 1.5.1 splash change was not part of the upload

### ThinkNode M1 operation at 1.5.2
Page order: FIRST → RECENT → TRACKING → RADIO → GPS → SOS (USB build without GPS: FIRST → RECENT → TRACKING → RADIO → SOS)

| Lower button | Result |
|---|---|
| 1× click / 2× click | next / previous page |
| 3× click | buzzer mute on/off |
| Long press on RECENT | send advert now ("Advert sent!") |
| Long press on TRACKING | open Outdoor settings menu |
| Long press on SOS | open SOS send screen |
| Long press on GPS | GPS on/off |
| Long press in the first 8 s after boot | CLI rescue mode |

| Upper button (backlight, since 1.5.0) | Result |
|---|---|
| Short press, backlight off | backlight on, 5-minute auto-off starts |
| Short press, backlight on | backlight off |
| Lower button while backlight on | auto-off timer back to 5 minutes |
| 5 minutes without a button press | backlight off (*superseded in 1.5.3*) |

Outdoor settings menu: click = next entry (cyclic), long press = execute. SOS send screen: long press = show confirmation, long press again = send and return home, short press = cancel. SOS alarm: long press = acknowledge, buzzer stops.

TRACKING page symbols: `---` own GPS has no fix (`isEnabled() && isValid()`), `?` the node sent a GPS advert with 0/0 coordinates, `340m` / `1.2km` straight-line distance (Haversine). A node is missing when its last advert had no position. Favourites only, max. 3 entries; green = GPS advert younger than 5 minutes, yellow = older.

`platformio.ini` differences on the M1: both companion environments (BLE and USB) have `AUTO_OFF_MILLIS=0`, `PIN_BUZZER=6` and NonBlockingRTTTL; only BLE has `ENV_INCLUDE_GPS=1` and `BLE_PIN_CODE=123456` (USB without GPS on purpose).

---

## [1.5.1] — Version String and Hint Text Cleanup

*28 March 2026 (approx.) · formerly V4.01*

### Changes
- Splash screen version string changed from "v3" to "v4" (this change did not make it into the first GitHub upload)
- All remaining hint texts moved to `display.height() - 13` (a few were still at `- 11` after 1.5.0)

---

## [1.5.0] — UI Foundation Rework

*28 March 2026 (approx.) · formerly V4.00*

### Changes
- **Splash screen** (large displays only): two groups — top: MeshCore logo (Y=4), version string (size 2, Y=22), build date (size 1, Y=42); intentional gap in the middle; bottom: "FieldMesh" in size 3 at `display.height() - 42` and the version string (size 2) at `display.height() - 20`. Small displays keep the compact upstream layout
- **Hint texts** moved from hard-coded pixel values to `display.height() - 13` (−13 instead of −11 leaves room for descenders g, p, y):

| Screen | Before | After |
|---|---|---|
| RECENT / TRACKING hint | 64 − 11 | `display.height() - 13` |
| Outdoor menu footer, SOS alert hint | 57 | `display.height() - 13` |
| SOS page hint | 54 | `display.height() - 13` |
| SOS send screen hints | 54 / 57 | `display.height() - 13` / `- 24` |

- Display size guards unified to `if (display.height() >= 128)`
- **Backlight toggle on the upper button** (M1): short press switches the backlight, auto-off after 5 minutes, reset by the lower button (`_backlight_on`, `_backlight_off_at`, `_btn2_was_pressed` in `UITask.h`). *Auto-off superseded in 1.5.3*

---

## [1.4.1] — RX Restart Fix after Off-Grid Toggle

*28 March 2026 (approx.; called "V4.07" in the code comment) · formerly V3.08*

### Bug fixes
- After `toggleOffGrid()` the receiver stayed deaf until the next transmitted packet (empty RX log; manual workaround: send an advert)
- Cause: `radio_set_params()` calls RadioLib directly and bypasses the RadioLibWrapper state (`STATE_RX/TX/IDLE`, and the RF switch on the T1000-E). Calling `radio.startReceive()` from `target.cpp` is not enough — the wrapper state stays wrong
- Fix: `advert()` at the end of `toggleOffGrid()`. The normal TX cycle runs through the wrapper, which restarts receive afterwards and handles RF switch and AGC correctly. Side effect: the node announces itself when the mode changes. Applies to the M1 and the T1000-E branch

This is the origin of the rule "never bypass RadioLibWrapper, never call `radio.startReceive()` directly". (`radio_set_params()` was later removed upstream; FieldMesh uses `radio_driver.setParams()` + `advert()`.)

---

## [1.4.0] — Portability and Cleanup

*28 March 2026 (approx.) · formerly V3.07*

Goal: make the code portable to other devices. No functional change on the ThinkNode M1.

### Changes (3.07a)
- `PIN_BUTTON2` corrected to GPIO 39 (P1.07, per Elecrow datasheet) in `variants/thinknode_m1/variant.h` (was 11) — the hack block at the top of `UITask.cpp` was removed
- `DISP_BACKLIGHT` / `BACKLIGHT_BTN` conflict resolved: `#ifdef DISP_BACKLIGHT` → `#if defined(DISP_BACKLIGHT) && !defined(BACKLIGHT_BTN)` in `loop()`
- All three uses of `PIN_BUTTON2` (and `_btn2_was_pressed` in `UITask.h`) wrapped in `#ifdef PIN_BUTTON2` — devices without a second button would not compile otherwise

| Device / flags | Backlight toggle | Auto-off |
|---|---|---|
| ThinkNode M1 (`DISP_BACKLIGHT` + `PIN_BUTTON2`) | upper button (GPIO 39) | 5 minutes |
| Device with `BACKLIGHT_BTN` (e.g. T-Echo) | upstream system | upstream system |
| `DISP_BACKLIGHT` without `PIN_BUTTON2` | none | 5 minutes |
| No `DISP_BACKLIGHT` | — | — |

### Changes (3.07b)
- Alert texts standardised to English: "SOS gesendet!" → "SOS sent!", "Kein SOS-Channel!" → "No SOS channel!", "Advert sent!"

### Changes (3.07c)
- Long press on the RECENT page sends an advert immediately (`advert()` picks zero-hop or flood from `client_repeat`) and shows "Advert sent!"; hint line "Advert: long press" on the page
- `isOnRecentPage()` helper added to `HomeScreen` (like `isOnTrackingPage()`)
- `_next_refresh = 0` in the RECENT branch of `handleLongPress()` — needed because `handleLongPress()` sets `c = 0` before the later `_next_refresh = 100` block; without it the alert appears only after 5 seconds

---

## [1.3.1] — Timer Reset Fix + SOS Node Name Display

*27 March 2026 (approx.) · formerly V3.05*

### Bug fixes
- **Auto-advert timer:** it was reset to +5 minutes on every phone connect or GPS toggle, even when already running. Now (in `setGPSAdvertEnabled()` in `MyMesh.h` and `CMD_SET_OTHER_PARAMS` in `MyMesh.cpp`): off → on = immediate advert + timer starts; on → off = timer stops; already running = no reset, no duplicate advert (`if (next_auto_advert == 0)` check)
- **SOS alert screen** showed the channel name ("#sos") instead of the sender: `SOSAlertScreen::setAlert()` now takes the node name from the message text (everything before the first `:`, trailing spaces trimmed, max. 31 characters), falling back to the channel name. *Later refined: sender = everything before `": "` (`fmExtractSenderName()` in `ChannelText.h`)*

Files changed: `MyMesh.h`, `MyMesh.cpp`, `ui-new/UITask.cpp`

---

## [1.3.0] — SOS System

*27 March 2026 · formerly V3.01*

### New features
- **SOS send**: long press on the SOS page opens a two-step confirmation screen; `sendSOS()` sends `!SOS lat,lon` (4 decimals) or `!SOS no GPS` to the first channel whose name contains "sos" (case-insensitive)
- **SOS receive**: `onChannelMessageRecv()` checks for `!SOS` before the normal `newMsg()` and calls `triggerSOS()` instead; a dedicated alert screen shows the sender and a siren (`siren:d=8,o=5,b=100:d,e,d,e,d,e,d,e`) loops until acknowledged
- Page order after 1.3.0: FIRST → RECENT → TRACKING → RADIO → GPS → SOS (ADVERT page removed)

| Situation | Behaviour |
|---|---|
| No channel with "sos" in its name | alert "No SOS channel!", no crash |
| `!SOS` received, phone connected | display turns on anyway, alarm shown |
| `!SOS` received, buzzer muted | buzzer switched on, stays on after acknowledging |
| Normal message during alarm | ignored, the SOS screen has priority |
| Siren tune ends | `poll()` sees `isPlaying() == false` and restarts it |

### Notes
- `strstr()` instead of `strncmp()` because MeshCore prepends the node name ("NodeName: !SOS lat,lon")
- The siren runs via `triggerSOS()` / `playSOSAlarm()`, not `notify()`, so it can override quiet mode. Other tones unchanged from upstream: direct message `MsgRcv3`, channel message `kerplop`, menu action `ack`
- `triggerSOS()` added to `AbstractUITask.h` as a virtual method with an empty default (keeps `ui-orig` compiling)
- Files changed: `AbstractUITask.h`, `MyMesh.h`, `MyMesh.cpp`, `ui-new/UITask.h`, `ui-new/UITask.cpp`

---

## [1.2.2] — contact.type Serial Fix

*27 March 2026 · formerly V2.07*

### Bug fixes
- Main cause of the contact problems: `writeContactRespFrame()` sent `contact.type` with bit 4 set to the companion app — the app received `type=0x11` instead of `0x01` and did not recognise the contact (no messaging, no telemetry)
- Fix in `MyMesh.cpp`: mask bit 4 in `writeContactRespFrame()` (outgoing, `contact.type & 0x0F`) and in `updateContactFromFrame()` (incoming, bit 4 from the app is not taken over)

*Superseded in 2.0.0: the bit-4 approach was removed completely (see 1.0.2).*

---

## [1.2.1] — contact.type Persistence Fix

*27 March 2026 · formerly V2.06*

### Bug fixes
- `saveContacts()` in `DataStore.cpp` wrote `contact.type` with bit 4 set to flash — now masked before writing (`clean_type = c.type & 0x0F`)

*Superseded in 2.0.0 (see 1.0.2).*

---

## [1.2.0] — Flood Advert in Off-Grid Mode

*27 March 2026 · formerly V2.04 / V2.05*

### Changes
- In Off-Grid mode (`client_repeat=1`), `advert()` sends a flood packet (`sendFlood(pkt, (uint32_t)0, _prefs.path_hash_mode + 1)`) instead of zero-hop, so adverts travel through the group
- `allowPacketForward()` returns `true` when `client_repeat != 0`
- Second build (old 1.2.0): fixes the compiler error of the first build (old 1.2.0) "sendFlood ambiguous" with the explicit `(uint32_t)0` cast

---

## [1.1.1] — Off-Grid Recommended Settings (documentation only)

*27 March 2026 · formerly V2.03*

Recommended EU Off-Grid parameters: 869.4625 MHz (channel 1/2 of sub-band g1 at BW 125) / BW 125 kHz / SF 11 / CR 5 (4/5), max. 25 mW ERP, max. 10 % duty cycle, no LBT required. The 865–868 MHz band requires LBT and AFA, which MeshCore does not implement — it is not usable.

---

## [1.1.0] — Off-Grid Menu on Tracking Page

*27 March 2026 · formerly V2.02*

### New features
- **Outdoor settings menu** — long press on the TRACKING page opens a menu replacing the simple GPS-share toggle:

| Entry | Function |
|---|---|
| GPS-Share [ON]/[OFF] | auto-advert with GPS every 5 minutes |
| Off-Grid [ON]/[OFF] | switch to 869.4625 MHz / BW 125 / SF 11 / CR 5 and enable client repeat |
| Back | close the menu |

- Normal radio parameters are saved before switching and restored when Off-Grid is turned off; the parameters apply immediately without a reboot. If no normal parameters were saved yet (`normal_freq == 0`), only repeat is switched off

### Files changed
- `NodePrefs.h`: four fields at the end of the struct (`float normal_freq`, `float normal_bw`, `uint8_t normal_sf`, `uint8_t normal_cr`)
- `DataStore.cpp`: saved/loaded at bytes 90–99 of the binary prefs file, after `rx_boosted_gain` (byte 89), read backwards-compatibly
- `MyMesh.h/.cpp`: `isOffGridActive()`, `toggleOffGrid()` (constants `OFFGRID_FREQ/BW/SF/CR`), applied with `radio_set_params()`
- `ui-new/UITask.h/.cpp`: `OutdoorMenuScreen`, `outdoor_menu` member, `handleLongPress()` dispatch

*Later changes: prefs moved to JSON in 2.0.0 (the binary migration still reads bytes 90–99); FieldMesh settings live in the `"fm"` object; `radio_set_params()` replaced by `radio_driver.setParams()`; the Off-Grid frequency is chosen automatically per region (`OffGrid.h`).*

---

## [1.0.5] — Off-Grid Frequency Correction

*27 March 2026 · formerly V2.01*

### Bug fixes
`repeat_freq_ranges[]` in `MyMesh.cpp` — the upstream values were single points or outside the legal bands:

| Band | Before | After |
|---|---|---|
| EU 433 MHz | 433.000 MHz (illegal point frequency) | 433.050 – 434.790 MHz |
| EU 869 MHz | 869.000 MHz (0.1 % duty cycle, practically unusable) | 869.400 – 869.650 MHz (sub-band g1, 10 % DC, no LBT) |
| US/AU 915 MHz | 918.000 MHz (outside the range) | 915.000 – 928.000 MHz |

These ranges are still FieldMesh values today and are never replaced by upstream values.

---

## [1.0.4] — Two Separate Timestamps

*27 March 2026 (approx.) — basis for the Off-Grid releases · formerly V1.08*

### Changes
- `AdvertPath` struct gains `gps_timestamp` (updated only on GPS adverts) next to `recv_timestamp` (updated on every advert)
- RECENT page uses `recv_timestamp`; TRACKING page uses `gps_timestamp` for the colour coding
- TRACKING page inserted into the `HomePage` enum, ADVERT page removed; Haversine distance (`calcDistance()`) added at the top of `UITask.cpp`
- Files changed: `MyMesh.h`, `MyMesh.cpp`, `ui-new/UITask.cpp`

---

## [1.0.3] — RECENT Always, TRACKING GPS Only

*26 March 2026 (approx.) · formerly V1.07*

### Changes
- `recv_timestamp` is always updated on advert receive (node always appears on RECENT)
- `has_gps` only set when the advert contains GPS coordinates (filter for the TRACKING page)
- Remaining issue: both pages still shared `recv_timestamp` — solved in 1.0.4

Internal build V1.06 (failed): skipping the `advert_paths` update for adverts without GPS made nodes disappear from RECENT.

---

## [1.0.2] — has_gps Flag

*26 March 2026 (approx.) · formerly V1.05*

### Changes
- Bit 4 (`ADV_LATLON_MASK = 0x10`) of the advert payload is stored in `contact.type` in `BaseChatMesh.cpp` (`populateContactFromAdvert()` and the update block: `type | (hasLatLon() ? 0x10 : 0)`)
- `AdvertPath` gains `has_gps` — set when at least one GPS advert was received from that node
- TRACKING page only shows nodes with `has_gps = true`
- Files changed: `BaseChatMesh.cpp`, `MyMesh.h`, `MyMesh.cpp`, `ui-new/UITask.cpp`

Internal build V1.04 (failed): updating `recv_timestamp` only on GPS adverts did not work because `contact.gps_lat/lon` keeps the old value for known contacts.

*Superseded in 2.0.0: `contact.type` is no longer modified; the GPS flag of the current advert comes from `lastAdvertHasLatLon()` (`BaseChatMesh`). The masking fixes of 1.2.1 and 1.2.2 were removed with it.*

---

## [1.0.1] — advert() GPS Validity Fix

*26 March 2026 (approx.) · formerly V1.03*

### Bug fixes
- `advert()` sent GPS coordinates even when GPS was switched off, because `isValid()` without `isEnabled()` still returned the last fix state. A position is now only included when `loc->isEnabled() && loc->isValid()`

### Earlier steps
- Internal build V1.01: the GPS page showed "fix" while GPS was off — fixed with `nmea->isEnabled() && nmea->isValid()` (sat / pos / alt show `---` otherwise). V1.01 also tried to advert stored coordinates; that part was reverted in V1.02

*Today stricter: a position is only sent with a fix younger than 2 minutes (`GpsCycle::hasFreshFix()`).*

---

## [1.0.0] — Initial Release

*26 March 2026 (approx.) — MeshCore v1.14.1, ThinkNode M1 · formerly V1.00*

### New features
- **Auto-advert timer** — node advertises its position every 5 minutes when GPS sharing is enabled (`advert_loc_policy == ADVERT_LOC_SHARE`); zero-hop only in normal mode
- **TRACKING page** — shows contacts that have sent a GPS position, with distance (Haversine) and colour coding (green < 5 min, yellow older); favourites only, max. 3 entries (GPS filter and timestamps refined in 1.0.2–1.0.4)
- BLUETOOTH and SHUTDOWN pages removed from the home screen rotation
- E-Ink fix: `-D AUTO_OFF_MILLIS=0` in both M1 companion environments — without it the display switched off after 15 seconds; GxEPD only redraws the panel when the content changes (*reverted in 1.5.3*)

### Technical notes
- `MyMesh.h`: `AUTO_ADVERT_INTERVAL_MS` (5 minutes), `AdvertPath` struct (pubkey prefix, path, name, `recv_timestamp`), `next_auto_advert` (0 = inactive)
- `MyMesh.cpp`: timer initialised in the constructor, started in `begin()`, checked in `loop()` before the `dirty_contacts_expiry` block
- Files changed: `MyMesh.h`, `MyMesh.cpp`, `ui-new/UITask.cpp`, `variants/thinknode_m1/platformio.ini`
