# FieldMesh

**A MeshCore firmware fork optimized for outdoor use — festivals, hiking, off-grid events, and situations where you need your radio network to do more than stock MeshCore offers.**

Built on [MeshCore](https://github.com/ripplebiz/MeshCore) · Tested on: Elecrow ThinkNode M1, Seeed Wio Tracker L1 · Based on MeshCore v1.14.1

-----

## Why FieldMesh?

MeshCore is a great platform, but some things that matter in the field require manual workarounds or aren’t there at all. FieldMesh adds:

- **Automatic GPS advertising** — your node announces itself every 5 minutes to nodes in direct range, zero-hop only — no extra network load
- **A dedicated tracking page** — see your contacts’ GPS positions and distances at a glance
- **A real Off-Grid mode** — builds on MeshCore’s Client Repeat feature, but with sensible frequency defaults (the upstream values were illegal or unusable in most regions) and a one-press toggle instead of manual parameter entry
- **An SOS system** — send and receive emergency alerts with buzzer alarm, even if your phone isn’t connected

The goal is a node you can put in your backpack, forget about, and trust that it’s doing its job.

More about my motivation is readable in my [blog post](https://www.tobiasguertler.de/blog/en/fieldmesh).  

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
- Backlight Fix vor Elecrow M1

-----

## Hardware

|                  |                                |
|------------------|--------------------------------|
|**Tested on**     |Elecrow ThinkNode M1 (E-Ink), Seeed Wio Tracker L1 (OLED)|
|**Other hardware**|Untested — contributions welcome|
|**Base firmware** |MeshCore v1.14.1                |

FieldMesh has been developed and tested on the ThinkNode M1 (E-Ink, single button) and the Wio Tracker L1 (OLED, joystick). The code is written to be portable — all hardware-specific pins are in `variant.h`, layout uses runtime display size checks, and joystick-specific UI behaviour is guarded by `#if UI_HAS_JOYSTICK`. If you test it on another platform, please open an issue with your findings.

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
|`variants/wio-tracker-l1/target.cpp`        |Adds `joystick_up` / `joystick_down` MomentaryButton instances              |
|`variants/wio-tracker-l1/target.h`          |Adds `extern` declarations for `joystick_up` / `joystick_down`              |

-----

## Building

FieldMesh builds the same way as MeshCore. Use PlatformIO and select the appropriate environment:

- `companion_radio_ble` — BLE connection to companion app
- `companion_radio_usb` — USB connection (GPS page not included in this build)

No additional dependencies beyond what MeshCore already requires.

-----

## Limitations & Known Issues

- Tested on ThinkNode M1 and Wio Tracker L1 — other hardware untested
- Off-Grid frequency is hardcoded for EU (869.4625 MHz) — other regions need a different default
- SOS requires a channel named “sos” to exist in your MeshCore setup
- Tracking page shows a maximum of 3 contacts (favourited in the companion app)

-----

## FAQ

<details>
<summary><strong>Why do I not see some nodes in the TRACKING view?</strong></summary>


Only nodes that have recently sent a GPS position are shown in TRACKING.
Additionally, the node must be marked as a favorite in the MeshCore app.

If a node appears in RECENT but not in TRACKING, it most likely:
- has not sent GPS data
- is not marked as favorite

</details>


<details>
<summary><strong>Why is my own position not shown?</strong></summary>


Your position is only available if your device has a valid GPS fix.

Make sure:
- GPS is enabled (via long press on the GPS screen or hardware switch on supported devices)
- your device has a GPS fix (not "GPS NO FIX")

Without a GPS fix, no position can be sent or displayed.

</details>


<details>
<summary><strong>How often is my position sent?</strong></summary>


If GPS sharing is enabled, your position is automatically sent every 5 minutes.
In normal mode, this is done with hop limit = 0 (local only).

</details>


<details>
<summary><strong>Why is the distance to other nodes inaccurate?</strong></summary>


The distance is only a rough estimate.
It is calculated once when a GPS advert is received and is not continuously updated to save energy.

Additionally, GPS accuracy itself can vary depending on the environment.

</details>


<details>
<summary><strong>Why are only a few nodes shown in TRACKING?</strong></summary>

  
TRACKING is limited to a maximum of 3 favorite nodes.
This is intentional to keep the display clean and usable in the field.

New incoming adverts may replace older entries.

</details>


<details>
<summary><strong>What happens if a node has no GPS?</strong></summary>


Nodes can still send adverts without GPS.
This allows you to see that a node is nearby even if no position is available.
To calculate distances, your own device should have GPS enabled.

</details>


<details>
<summary><strong>Why are my GPS adverts not forwarded through the mesh?</strong></summary>


In normal mode, GPS auto-adverts are sent with hop limit = 0.
This is intentional to reduce network load and avoid unnecessary traffic.
Only in Off-Grid mode, adverts are flooded.

</details>


<details>
<summary><strong>Why am I not seeing nodes from the wider network?</strong></summary>


Check if Off-Grid mode is enabled.
In Off-Grid mode, your device operates on a different frequency and is isolated from the normal mesh.
Disable Off-Grid mode to reconnect to the main network.

</details>


<details>
<summary><strong>What does Off-Grid mode do?</strong></summary>


Off-Grid mode enables Client Repeat functionality.
Your node behaves similar to a repeater and forwards messages.
This happens on a separate frequency to avoid interfering with the main MeshCore network.

</details>


<details>
<summary><strong>Can I change the Off-Grid frequency?</strong></summary>


Currently, the Off-Grid frequency can only be changed in the code.
You need to clone the repository and adjust the configuration manually.

</details>


<details>
<summary><strong>Why do I see fewer nodes in Off-Grid mode?</strong></summary>


Off-Grid mode uses a separate frequency.
You will only see nodes that are also in Off-Grid mode.

</details>


<details>
<summary><strong>When should I use Off-Grid mode?</strong></summary>


Off-Grid mode is useful when:

- you are in areas without existing mesh infrastructure (e.g. mountains)
- you are operating in a closed group
- you want to avoid interfering with the main network

Note that you will not receive messages from the main mesh while in Off-Grid mode.

</details>


<details>
<summary><strong>What happens to my settings when switching Off-Grid mode?</strong></summary>


Your normal radio settings are stored and restored automatically.
Switching between modes does not require manual reconfiguration.

</details>


<details>
<summary><strong>How does the SOS feature work?</strong></summary>


The SOS function is protected by a two-step confirmation to prevent accidental triggering.

When activated:
- a message "!SOS" is sent
- if available, your GPS position is included

</details>


<details>
<summary><strong>What happens when an SOS is received?</strong></summary>


When an SOS message is received:

- a dedicated screen opens
- the sender's node name is displayed
- the buzzer (if available) will sound

The alarm will continue until manually confirmed.

</details> 

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
