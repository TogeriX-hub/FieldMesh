# Upstream sync guide

FieldMesh is a fork of [MeshCore](https://github.com/meshcore-dev/MeshCore). This file lists every
place where FieldMesh changes an upstream file and how to handle each of them when a new upstream
release is merged. Keep it up to date whenever a FieldMesh change touches an upstream file.

## Principle

- FieldMesh logic lives in **its own files** (never touched by upstream).
- Upstream files only get **short hooks** (one-line calls into FieldMesh code), marked with
  `FieldMesh` in a comment or wrapped in `#ifdef FIELDMESH_REPEATER`.
- Classes are split across files where needed: `MyMeshFieldMesh.cpp` implements the FieldMesh
  methods of the companion `MyMesh`, `FieldMesh*.cpp` those of the repeater `MyMesh` and `UITask`.
- The FieldMesh UI (`ui-new/`, `ui-orig/`) intentionally diverges from upstream (see below).

Quick check of all upstream touchpoints:

```
git diff <upstream-tag> HEAD --stat -- src variants examples platformio.ini
```

## Procedure

1. Wait for an upstream **release tag** (e.g. `companion-v1.19.0`). Do not merge upstream `dev`.
   Avoid cherry-picking from upstream `dev`; if a critical fix must be taken early, note it in the
   PLAN so the sync takes the upstream version of those lines.
2. Create the dev branch (`dev/vX.Y`) and merge the tag: `git merge companion-vX.Y.Z`.
3. Resolve conflicts with the rules below. Never blindly "accept both".
4. Push; **FieldMesh Build Check** must be green. Run `pio test -e native`.
5. Hardware smoke test: M1, L1, T1000-E, Station G2 (boot, Off-Grid on/off, SOS in a test
   channel, advert with GPS, app connect).
6. Merge to `main` via PR as a **merge commit, never squash** (keeps the upstream ancestry).

## Rules per file group

### 1. FieldMesh-only files - no conflicts

`examples/companion_radio/`: `MyMeshFieldMesh.cpp`, `OffGrid.h`, `GpsCycle.h`, `TrackScheduler.h`,
`ChannelText.h`, `IdentityBackup.h`, `GpsLog.h`, `FloodPolicy.h` ·
`examples/simple_repeater/`: `FieldMesh*.cpp`, `FieldMesh*.h` ·
`variants/station_g2/partitions_fieldmesh.csv` · `test/test_fieldmesh_*` ·
`.github/workflows/fieldmesh-*.yml` · `.github/ISSUE_TEMPLATE/` · `README.md`, `RELEASE.md`,
`UPSTREAM_SYNC.md`.

If upstream renames or changes an API these files use, the build check shows it.

### 2. Upstream files with FieldMesh hooks - keep upstream, re-apply the hooks

If upstream restructured a function, take the upstream version and put the hook back at the
equivalent place.

**Companion (`examples/companion_radio/`)**

| File | FieldMesh change |
|---|---|
| `MyMesh.h` | includes `GpsCycle.h`, `TrackScheduler.h`; `AdvertPath`: `has_gps`, `gps_timestamp`; `Listener::onClockAdjusted()`; one FieldMesh block at the end of the class (members, `getCADFailMaxDuration()` override, public API) |
| `MyMesh.cpp` | `_app_initialized` guard in `logRxRaw`, `onContactPathUpdated`, `queueMessage`, `onChannelMessageRecv`, `checkSerialInterface`, set in `CMD_APP_START` · `fmNoteAdvertGps()` in `onDiscoveredContact` · `sendFloodScoped`: un-scoped while repeat is on (#25) · `getRetransmitDelay`: `fmRelayDelay()` while Off-Grid · `calcFloodTimeoutMillisFor`: `+ fmFloodTimeoutExtra()` · `fmBegin()` in `begin()` · `repeat_freq_ranges[]` (`#else` branch: FieldMesh ranges, **never replace with upstream values**) · `fmOnNetworkChange()` in `CMD_SET_RADIO_PARAMS` · `fmFilterRelay()` in `filterRecvFloodPacket` (Off-Grid) · `fmUpdateAutoAdvertTimer()` in `CMD_SET_OTHER_PARAMS` · `reportSetting()` in the sensor settings list · `fmLoop()` at the end of `loop()` · `advert()`: `fmCreateSelfAdvert()` / `fmSendSelfAdvert()` |
| `NodePrefs.h` | `normal_freq/bw/sf/cr`; JSON object `"fm"` (`FieldMeshPrefs`) - FieldMesh prefs never go into upstream objects |
| `DataStore.cpp` | `IdentityBackup` hooks (begin, identity saved, prefs saved); binary migration reads the FieldMesh fields at the end of old pref files |
| `main.cpp` | static BLE PIN (no random PIN on display devices); `FM_GPS_LOG` diagnostic build |

**Repeater (`examples/simple_repeater/`)** - everything inside `#ifdef FIELDMESH_REPEATER`

| File | FieldMesh change |
|---|---|
| `MyMesh.h` | include `FieldMeshRepeater.h`; base class `FieldMeshUIHost`; member block after `matching_peer_indexes`; overrides `onAckRecv`, `searchChannelsByHash`, `onGroupDataRecv`; `FieldMeshUIHost` implementation |
| `MyMesh.cpp` | `fmStatsRx/Tx` in `logRx/logTx` · `fmRoomRecv` in `onRecvPacket` · `fmNodeSeen` in `onAnonDataRecv`, `onPeerDataRecv` · `fmNodeHeard` in `onAdvertRecv` · `fmInitMembers()` in the constructor · `fmBegin()` in `begin()` · `fmRoomSendAdvert` after the adverts · `fmHandleCommand` in `handleCommand` · advert after Off-Grid radio switch · `fmRoomLoop/fmStatsLoop/fmWebLoop` in `loop()` · `fmWebIsOn()` in `hasPendingWork()` |
| `UITask.h` / `UITask.cpp` | host pointer + menu declarations; node type on the boot screen; `fmRenderPage()`; button hooks (menu itself in `FieldMeshUI.cpp`) |
| `main.cpp` | `ui_task.setFieldMeshHost()` |

**Shared code (`src/`)**

| File | FieldMesh change |
|---|---|
| `helpers/BaseChatMesh.h/.cpp` | `lastAdvertHasLatLon()` (GPS flag of the advert being processed; replaces the old bit-4 hack in `contact.type`) |
| `helpers/sensors/LocationProvider.h`, `MicroNMEALocationProvider.h` | NMEA sentence callback (GSV signal levels for `GpsCycle`) |
| `helpers/ui/DisplayDriver.h`, `GxEPDDisplay.h/.cpp` | `blankScreen()` / `powerOff()` split off from `turnOff()` (M1 power-off drain) - see 4. |

**Variants**

| File | FieldMesh change |
|---|---|
| `variants/station_g2/platformio.ini` | env `Station_G2_fieldmesh_repeater` |
| `variants/wio-tracker-l1/target.*`, `wio-tracker-l1-1w/target.*` | `joystick_up`, `joystick_down` buttons |
| `variants/thinknode_m1/variant.h` | `PIN_BUTTON2` = 39 - see 4. |

**Other:** `license.txt` (FieldMesh copyright line).

### 3. Intentionally diverged files - keep FieldMesh, review the upstream diff

`examples/companion_radio/ui-new/UITask.*`, `ui-new/icons.h`, `ui-orig/UITask.*`, `ui-orig/Button.*`

These are FieldMesh's own UI; upstream changes there always conflict. Do not resolve block by block:

1. Take the FieldMesh version: `git checkout --ours <file>`.
2. Review what upstream changed: `git diff <old-upstream-tag> <new-upstream-tag> -- <file>`.
3. Port what FieldMesh wants (bug fixes, new display/driver APIs) as separate commits.
4. If upstream changed APIs the UI uses (`DisplayDriver`, `MyMesh::Listener`, prefs), the build
   check shows it.

### 4. Take upstream once it has the same fix

| File | Status (30.10.2026) |
|---|---|
| `src/helpers/ui/DisplayDriver.h`, `GxEPDDisplay.h/.cpp` | Upstream `dev` reworked e-ink power-off (`turnOff(bool blank)`, `blankScreen()`, sleep icon, `setMaxPartialRefresh`). On the next sync take the upstream version, drop FieldMesh's `powerOff()`, and adapt the M1 power-off in `ui-new/UITask.cpp`. Test the M1 power-off drain on hardware. |
| `variants/thinknode_m1/variant.h` | Upstream `dev` has the same `PIN_BUTTON2` fix (39) - take upstream. |

## Watch list (upstream `dev`, 30.10.2026)

- `NodePrefs::tz_offset` becomes `float` (half-hour offsets) and a new `"ui"` JSON object - check
  the FieldMesh UI use of `tz_offset` and the binary prefs migration.
- Scope-0 rule changes - check `sendFloodScoped` (#25).
- `new-direct-mode` - may affect Off-Grid compatibility.
- T1000-E buzzer CLI command, acknowledgement tones removed - compare with `ui-orig` buzzer handling.
