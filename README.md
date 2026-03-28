# FieldMesh

**A MeshCore firmware fork optimized for outdoor use — festivals, hiking, off-grid events, and situations where you need your radio network to do more than stock MeshCore offers.**

Built on [MeshCore](https://github.com/ripplebiz/MeshCore) · Tested on: Elecrow ThinkNode M1 · Based on MeshCore v1.14.1

-----

## Why FieldMesh?

MeshCore is a great platform, but some things that matter in the field require manual workarounds or aren’t there at all. FieldMesh adds:

- **Automatic GPS advertising** — your node announces itself every 5 minutes to nodes in direct range, zero-hop only — no extra network load
- **A dedicated tracking page** — see your contacts’ GPS positions and distances at a glance
- **A real Off-Grid mode** — builds on MeshCore’s Client Repeat feature, but with sensible frequency defaults (the upstream values were illegal or unusable in most regions) and a one-press toggle instead of manual parameter entry
- **An SOS system** — send and receive emergency alerts with buzzer alarm, even if your phone isn’t connected

The goal is a node you can put in your backpack, forget about, and trust that it’s doing its job.

-----

## Features

### GPS Tracking & Auto-Advert

FieldMesh separates two things that stock MeshCore conflates: *when did I last hear from this node* and *when did this node last have a GPS fix*. The RECENT page shows all recently heard nodes. The TRACKING page shows only nodes that have sent a GPS position — with distance calculated via Haversine formula — color-coded green (< 5 min) or yellow (older). Only contacts marked as favourites in the companion app appear on the Tracking page (max. 3).

Your own node advertises its position automatically every 5 minutes when GPS sharing is enabled — **zero-hop in normal mode**, so it only reaches nodes in direct range without adding load to the wider network. In Off-Grid mode, adverts are sent as flood packets instead, which is the right behaviour when you’re actively trying to extend reach across a local group. Sending works correctly regardless of GPS hardware state (fixes a bug where `isValid()` returned stale data after GPS was powered off).

### Off-Grid Mode

MeshCore has a feature called **Client Repeat** where a node actively relays packets to extend network reach. FieldMesh builds on this — but the upstream frequency defaults for Client Repeat were problematic: the EU 433 MHz value was a single illegal point frequency, the 869 MHz value landed in a sub-band with 0.1% duty cycle (practically unusable), and the 915 MHz value was outside the legal band in some regions.

FieldMesh corrects the frequency ranges to legal values and adds a **one-press Off-Grid toggle** accessible from the Tracking page menu. It switches to a dedicated off-grid frequency — **869.4625 MHz / BW125 / SF11 / CR5** (EU Sub-Band g1, 10% duty cycle, no LBT required) — and enables Client Repeat in one step, saving your normal parameters automatically so you can switch back without re-entering anything.

**Off-Grid uses a completely separate frequency from your normal MeshCore network.** For example, the EU/UK Narrow preset runs on 869.618 MHz / BW62.5. Off-Grid runs on 869.4625 MHz / BW125. The two signals don’t overlap — there is a ~62 kHz gap between them. You can run Off-Grid mode at a festival or in the field without interfering with the wider MeshCore network around you.

Switching modes triggers an immediate advert so nearby nodes know you’ve changed frequency.

Frequency ranges are set correctly for legal operation:

|Region       |Range                              |
|-------------|-----------------------------------|
|EU 433 MHz   |433.050 – 434.790 MHz              |
|EU 869 MHz   |869.400 – 869.650 MHz (Sub-Band g1)|
|US/AU 915 MHz|915.000 – 928.000 MHz              |

### SOS System

Long-press the button on the SOS page to open a two-step confirmation screen (accidental trigger protection). FieldMesh looks for a channel whose name contains “sos” (case-insensitive) and sends `!SOS lat,lon` with your current GPS coordinates, or `!SOS no GPS` if no fix is available.

On the receiving end: incoming SOS messages trigger a buzzer alarm that overrides quiet mode and loops until manually acknowledged. The alert screen shows the sending node’s name. Normal messages are suppressed while an SOS alert is active.

### UI & Usability

- Splash screen with version info and “FieldMesh” branding
- Backlight control via upper button with 5-minute auto-off

-----

## Hardware

|                  |                                |
|------------------|--------------------------------|
|**Tested on**     |Elecrow ThinkNode M1            |
|**Other hardware**|Untested — contributions welcome|
|**Base firmware** |MeshCore v1.14.1                |

FieldMesh has only been developed and tested on the ThinkNode M1. The code is written to be portable (all hardware-specific pins are in `variant.h`, layout uses runtime display size checks), but behavior on other devices is unknown. If you test it on another platform, please open an issue with your findings.

-----

## Changed Files

These files differ from upstream MeshCore. Everything else is untouched.

|File                                        |What changed                                                                |
|--------------------------------------------|----------------------------------------------------------------------------|
|`src/helpers/BaseChatMesh.cpp`              |Stores GPS flag (Bit 4) in `contact.type`                                   |
|`examples/companion_radio/AbstractUITask.h` |Adds `triggerSOS()` virtual stub for compatibility                          |
|`examples/companion_radio/MyMesh.h`         |Auto-advert timer, AdvertPath struct, Off-Grid toggle declarations          |
|`examples/companion_radio/MyMesh.cpp`       |Core logic: auto-advert, Off-Grid mode, SOS send/receive, frequency ranges  |
|`examples/companion_radio/NodePrefs.h`      |Four new fields for saving normal radio parameters                          |
|`examples/companion_radio/DataStore.cpp`    |Saves/loads new fields; masks Bit 4 from contact persistence                |
|`examples/companion_radio/ui-new/UITask.h`  |Outdoor menu, SOS screens, buzzer and backlight members                     |
|`examples/companion_radio/ui-new/UITask.cpp`|All UI: tracking page, Haversine, outdoor menu, SOS screens, backlight logic|
|`examples/companion_radio/ui-new/icons.h`   |Adds 48×48px advert icon for large displays                                 |
|`variants/thinknode_m1/variant.h`           |Corrects `PIN_BUTTON2` to GPIO 39                                           |
|`variants/thinknode_m1/platformio.ini`      |Sets `AUTO_OFF_MILLIS=0` to prevent E-Ink display timeout                   |

-----

## Building

FieldMesh builds the same way as MeshCore. Use PlatformIO and select the appropriate environment:

- `companion_radio_ble` — BLE connection to companion app
- `companion_radio_usb` — USB connection (GPS page not included in this build)

No additional dependencies beyond what MeshCore already requires.

-----

## Limitations & Known Issues

- Only tested on ThinkNode M1
- Off-Grid frequency is hardcoded for EU (869.4625 MHz) — other regions need a different default
- SOS requires a channel named “sos” to exist in your MeshCore setup
- Tracking page shows a maximum of 3 contacts (favourited in the companion app)

-----

## Contributing

This is a personal project that grew out of wanting a more field-ready node. If you:

- test it on other hardware and want to share results
- find a bug
- have a feature idea that fits the outdoor/off-grid use case
- want to adapt the Off-Grid defaults for another region

…feel free to open an issue or pull request. The more platforms this works on, the better.

-----

## Relationship to MeshCore

FieldMesh is a fork of [MeshCore](https://github.com/ripplebiz/MeshCore) by ripplebiz. It is not affiliated with or endorsed by the MeshCore project. The goal is to stay close to upstream and periodically rebase — but semantic compatibility after upstream changes requires manual review.

-----

*FieldMesh — MeshCore for the field.*
