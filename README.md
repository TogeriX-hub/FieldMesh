# FieldMesh

**A MeshCore firmware fork optimized for outdoor use — festivals, hiking, off-grid events, and situations where you need your radio network to do more than stock MeshCore offers.**

Built on [MeshCore](https://github.com/ripplebiz/MeshCore) ·  Based on MeshCore v1.14.1 ·  Download latest [release](../../releases/latest)

-----

## Why FieldMesh?

MeshCore is a great platform, but some things that matter in the field require manual workarounds or aren't there at all. FieldMesh adds:

- **Automatic GPS advertising** — your node announces itself every 5 minutes to nodes in direct range, zero-hop only — no extra network load
- **A dedicated tracking page** — see your contacts' GPS positions and distances at a glance
- **A real Off-Grid mode** — builds on MeshCore's Client Repeat feature, but with sensible frequency defaults (the upstream values were illegal or unusable in most regions) and a one-press toggle instead of manual parameter entry
- **An SOS system** — send and receive emergency alerts with buzzer alarm, even if your phone isn't connected
- **Message history** — browse received messages directly on the device, no phone required
- **Text input & send** — compose and send channel messages via joystick on supported devices (Wio Tracker L1)

The goal is a node you can put in your backpack, forget about, and trust that it's doing its job.

More about my motivation is readable in my [blog post](https://www.tobiasguertler.de/blog/en/fieldmesh).

-----

## Features

### GPS Tracking & Auto-Advert

FieldMesh separates two things that stock MeshCore conflates: *when did I last hear from this node* and *when did this node last have a GPS fix*. The RECENT page shows all recently heard nodes. The TRACKING page shows only nodes that have sent a GPS position — with distance calculated via Haversine formula — color-coded green (< 5 min) or yellow (older). Only contacts marked as favourites in the companion app appear on the Tracking page (max. 3).

Your own node advertises its position automatically every 5 minutes when GPS sharing is enabled — **zero-hop in normal mode**, so it only reaches nodes in direct range without adding load to the wider network. In Off-Grid mode, adverts are sent as flood packets instead, which is the right behaviour when you're actively trying to extend reach across a local group. Sending works correctly regardless of GPS hardware state (fixes a bug where `isValid()` returned stale data after GPS was powered off).

### Off-Grid Mode

MeshCore has a feature called **Client Repeat** where a node actively relays packets to extend network reach. FieldMesh builds on this — but the upstream frequency defaults for Client Repeat were problematic: the EU 433 MHz value was a single illegal point frequency, the 869 MHz value landed in a sub-band with 0.1% duty cycle (practically unusable), and the 915 MHz value was outside the legal band in some regions.

FieldMesh corrects the frequency ranges to legal values and adds a **one-press Off-Grid toggle** accessible from the Tracking page menu. It switches to a dedicated off-grid frequency — **869.4625 MHz / BW125 / SF11 / CR5** (EU Sub-Band g1, 10% duty cycle, no LBT required) — and enables Client Repeat in one step, saving your normal parameters automatically so you can switch back without re-entering anything. There is a dedicated US build with US legal frequencies available under [releases](../../releases/latest).

**Off-Grid uses a completely separate frequency from your normal MeshCore network.** For example, the EU/UK Narrow preset runs on 869.618 MHz / BW62.5. Off-Grid runs on 869.4625 MHz / BW125. The two signals don't overlap — there is a ~62 kHz gap between them. You can run Off-Grid mode at a festival or in the field without interfering with the wider MeshCore network around you.

Switching modes triggers an immediate advert so nearby nodes know you've changed frequency.

Frequency ranges are set correctly for legal operation:

|Region       |Range                              |
|-------------|-----------------------------------|
|EU 433 MHz   |433.050 – 434.790 MHz              |
|EU 869 MHz   |869.400 – 869.650 MHz (Sub-Band g1)|
|US/AU 915 MHz|915.000 – 928.000 MHz              |

### SOS System

Long-press the button on the SOS page to open a two-step confirmation screen (accidental trigger protection). FieldMesh looks for a channel whose name contains "sos" (case-insensitive) and sends `!SOS lat,lon` with your current GPS coordinates, or `!SOS no GPS` if no fix is available.

On the receiving end: incoming SOS messages trigger a buzzer alarm that overrides quiet mode and loops until manually acknowledged. The alert screen shows the sending node's name. Normal messages are suppressed while an SOS alert is active.

### Message History & Text Input (V5)

V5 adds standalone messaging — no phone required.

**Message History (all devices)**

Received messages are stored in a RAM ring buffer (up to 32 entries) and browsable directly on the device. The home screen shows an unread counter and the name and timestamp of the most recent message from a favourite contact. Long-pressing on the home screen opens a filter menu where you can choose to view all messages or messages from favourites only.

The history view shows one message at a time — newest first — with sender name, channel tag (`[CH1]`, `[DM]`, etc.), timestamp, and message text. Short press advances to the next message; long press clears all remaining messages at once. When you've stepped through all unread messages, the display returns to the home screen automatically.

The unread counter in the UI is independent from MeshCore's internal sync queue — browsing or clearing messages on the device never affects smartphone sync.

**Text Input & Send (Wio Tracker L1 only)**

On the Wio Tracker L1 (joystick input), you can compose and send messages directly from the device. From the message filter menu, select "Send Message" to start the flow:

1. **Channel selection** — choose from your configured channels using the joystick
2. **Text composition** — select characters one by one using left/right on the joystick, confirm each with a short press; long press sends the message
3. After sending, a brief "Sent!" confirmation is shown before returning to the message history

Message length is limited to 20 characters. This feature is not available on the ThinkNode M1 (single-button input only).

### UI & Usability

- Splash screen with version info and "FieldMesh" branding
- Backlight fix for Elecrow M1
- → [UI images](docs/FieldMesh/images/)

-----

## Hardware

|                  |                                |
|------------------|--------------------------------|
|**Tested on**     |Elecrow ThinkNode M1 (E-Ink), Seeed Wio Tracker L1 (OLED), Seeed T1000-E (no display)|
|**Other hardware**|RAK WiseMesh TAG and similar single-button headless devices — contributions welcome|
|**Base firmware** |MeshCore v1.14.1                |

FieldMesh has been developed and tested on the ThinkNode M1 (E-Ink, single button) and the Wio Tracker L1 (OLED, joystick). V5.10 adds support for headless devices (no display, single button) — the Seeed T1000-E is the primary reference device for this class. The code is written to be portable — all hardware-specific pins are in `variant.h`, layout uses runtime display size checks, and joystick-specific UI behaviour is guarded by `#if UI_HAS_JOYSTICK`. If you test it on another platform, please open an issue with your findings.

-----

## Changed Files

These files differ from upstream MeshCore. Everything else is untouched.

|File                                        |What changed                                                                |
|--------------------------------------------|----------------------------------------------------------------------------|
|`src/helpers/BaseChatMesh.cpp`              |Stores GPS flag (Bit 4) in `contact.type`                                   |
|`examples/companion_radio/AbstractUITask.h` |Adds `triggerSOS()` virtual stub; adds `newMsg()` signature with `is_favorite` param (V5)|
|`examples/companion_radio/MyMesh.h`         |Auto-advert timer, AdvertPath struct, Off-Grid toggle declarations; `sendChannelMessage()` wrapper (V5)|
|`examples/companion_radio/MyMesh.cpp`       |Core logic: auto-advert, Off-Grid mode, SOS send/receive, frequency ranges; channel message send (V5)|
|`examples/companion_radio/NodePrefs.h`      |Four new fields for saving normal radio parameters                          |
|`examples/companion_radio/DataStore.cpp`    |Saves/loads new fields; masks Bit 4 from contact persistence                |
|`examples/companion_radio/ui-new/UITask.h`  |Outdoor menu, SOS screens, buzzer and backlight members; message history and compose screens (V5)|
|`examples/companion_radio/ui-new/UITask.cpp`|All UI: tracking page, Haversine, outdoor menu, SOS screens, backlight logic; message history, filter, compose, channel select (V5)|
|`examples/companion_radio/ui-new/icons.h`   |Adds 48×48px advert icon for large displays                                 |
|`examples/companion_radio/ui-orig/Button.h` |Adds `QUINTUPLE_PRESS` event and `onQuintuplePress()` callback (V5.10)      |
|`examples/companion_radio/ui-orig/Button.cpp`|Handles 5x click as distinct event; `== 4` fix for quadruple detection (V5.10)|
|`examples/companion_radio/ui-orig/UITask.h` |Adds `handleButtonQuintuplePress()`, `triggerSOS()`, `_sos_active` flag; `newMsg()` signature updated (V5.10)|
|`examples/companion_radio/ui-orig/UITask.cpp`|Off-Grid toggle via 5x click; SOS alarm via buzzer; forced buzzer feedback for all button actions (V5.10)|
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

- Tested on ThinkNode M1, Wio Tracker L1, and Seeed T1000-E — other hardware untested
- Off-Grid frequency is hardcoded — other regions need a different default! On the [releases](../../releases/latest) section you will find both a firmware for EU and US download
- SOS requires a channel named "sos" to exist in your MeshCore setup
- Tracking page shows a maximum of 3 contacts (favourited in the companion app)
- Message history is RAM-only — stored messages are lost on reboot (intentional)
- Text input and send is available on Wio Tracker L1 only — the ThinkNode M1 single button does not support text composition
- Message send supports channels only — no direct message (DM) sending via device UI

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
If you just want to download prebuild firmware you will find a EU and US build unter [releases](../../releases/latest)

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


<details>
<summary><strong>Where are received messages stored?</strong></summary>

Messages are stored in RAM only — up to 32 entries in a ring buffer.
They are lost when the device reboots. This is intentional: the device is not a permanent message store, and smartphone sync via the MeshCore companion app is unaffected by what the device UI shows or clears.

</details>


<details>
<summary><strong>Does clearing messages on the device affect smartphone sync?</strong></summary>

No. The message history shown on the device is a display copy only.
Clearing it — either one by one or all at once — never touches MeshCore's internal sync queue.
Your companion app will still receive all messages that arrived while it was connected or reconnects.

</details>


<details>
<summary><strong>Can I send messages without the companion app?</strong></summary>

Yes, on the Wio Tracker L1. Long-press on the home screen to open the message menu, then select "Send Message". You will be prompted to choose a channel, then compose your text using the joystick. Messages can be up to 80 characters long.

This feature is not available on the ThinkNode M1 — single-button input does not support text composition.

</details>


<details>
<summary><strong>Can I send direct messages (DMs) from the device?</strong></summary>

No. The text send feature supports channel messages only.
Direct messages require the companion app.

</details>


<details>
<summary><strong>What does the unread counter on the home screen show?</strong></summary>

The counter shows how many received messages you have not yet stepped through in the message history view.
It is independent from MeshCore's internal message queue and is not affected by smartphone sync state.

</details>

---

## Headless devices (T1000-E, RAK WiseMesh TAG)

<details>
<summary><strong>What can I do with a device that has no display?</strong></summary>

Headless devices (no display, single button) support a focused set of features:

- receive and relay mesh messages
- toggle Off-Grid mode via 5× button press
- receive SOS alarms via buzzer
- send adverts, toggle GPS, toggle buzzer mute

All actions give forced buzzer feedback — ascending tone means on/enabled, descending means off/disabled.

</details>


<details>
<summary><strong>How do I toggle Off-Grid mode on a headless device?</strong></summary>

Press the button 5 times in quick succession.

The buzzer confirms the result:
- two ascending tones (low → high) = Off-Grid is now **on**
- two descending tones (high → low) = Off-Grid is now **off**

This feedback is always played, even if the buzzer is otherwise muted.

</details>


<details>
<summary><strong>How does SOS work on a headless device?</strong></summary>

When an SOS message is received, the buzzer sounds a continuous siren alarm. The alarm loops until you press the button once (short press) to acknowledge it.

Note: headless devices cannot send SOS — there is no free button slot for a two-step SOS send confirmation. SOS receive only.

</details>


<details>
<summary><strong>What do the different buzzer tones mean on a headless device?</strong></summary>

| Tone | Meaning |
|---|---|
| Ascending (c → e) | Feature turned on (GPS, buzzer, Off-Grid) |
| Descending (e → c) | Feature turned off |
| Double beep | Advert sent |
| Continuous siren | SOS alarm received — press once to acknowledge |

</details>


<details>
<summary><strong>How do I know if my headless device is in Off-Grid mode after a reboot?</strong></summary>

The Off-Grid state is saved to flash and restored on boot. If Off-Grid was active before the reboot, it will be active again after — but there is no visual indicator without a display.

To check: press 5× to toggle, listen to the tone (ascending = was off, now on; descending = was on, now off), then press 5× again to return to the original state.

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
