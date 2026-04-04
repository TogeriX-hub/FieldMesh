# FAQ

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