# Security policy

## Supported versions

Security fixes go into the latest FieldMesh release only. If you run an older version, please update first — the [web flasher](https://fieldmesh-radio.github.io/FieldMesh/) keeps your settings and identity.

## Reporting a vulnerability

**Please do not report security problems in public issues or discussions.**

Use GitHub's private vulnerability reporting:
1. Open the **Security** tab of this repository
2. Click **Report a vulnerability**
3. Describe the problem and submit

A useful report says which device and FieldMesh version are affected, what an attacker can do and under which conditions, and how to reproduce it. An incomplete report is better than none.

## FieldMesh or MeshCore?

FieldMesh is a fork of [MeshCore](https://github.com/meshcore-dev/MeshCore).

- **Report here:** problems in the FieldMesh parts — device UI, Off-Grid mode, SOS, tracking, the FieldMesh Repeater (WiFi dashboard, room server, room commands), the web flasher.
- **Report to MeshCore:** problems in the MeshCore core that also affect stock MeshCore (packet handling, routing, encryption, the companion protocol) — see the [MeshCore security policy](https://github.com/meshcore-dev/MeshCore/security/policy).
- **Not sure?** Report here, I will pass it on if it belongs upstream.

Problems in third-party libraries (RadioLib, Crypto, …) belong to those projects.

## Known limitations

These are documented design decisions, not vulnerabilities:

- The dashboard WiFi of the FieldMesh Repeater is open — anyone in range can read the board and see node positions. Setup actions need the repeater admin password.
- A room without a password can be read and written by everyone in radio range.

## What to expect

FieldMesh is maintained by one person in their spare time. I will acknowledge a report as soon as I can, but cannot promise fixed deadlines. Please allow a fair chance to fix the problem before you publish it; if you have not heard back after 90 days, feel free to follow up or disclose.
