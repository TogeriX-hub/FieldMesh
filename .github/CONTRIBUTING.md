# Contributing to FieldMesh

FieldMesh is a personal project that grew out of wanting a more field-ready node. Feedback, test reports and code are all welcome.

## Questions, ideas and reports

| You want to … | Go to |
|---|---|
| ask a question or get help with setup | [Discussions → Q&A](https://github.com/fieldmesh-radio/FieldMesh/discussions/categories/q-a) |
| report how FieldMesh runs on other hardware, or ask if a device could work | [Discussions → Hardware](https://github.com/fieldmesh-radio/FieldMesh/discussions/categories/hardware) or the issue template *Hardware test report* |
| suggest a feature for the trackers or the repeater | [Discussions → Ideas](https://github.com/fieldmesh-radio/FieldMesh/discussions/categories/ideas) |
| report a bug | [New issue](https://github.com/fieldmesh-radio/FieldMesh/issues/new/choose) |
| report a security problem | privately, see the [security policy](https://github.com/fieldmesh-radio/FieldMesh/blob/main/.github/SECURITY.md) |

Please always name your device and the FieldMesh version (shown on the splash screen, in the MeshCore app or with `ver`).

## Code

- **Small fixes:** just open a pull request. **Larger changes:** start a thread in Discussions → Ideas first, so we can agree on the approach before you put work into it.
- **Branch:** firmware changes go against the current development branch (`dev/v2.3` at the moment; `main` is always the latest release). Fixes for the README, the docs or the web flasher can go straight to `main`.
- **Where the code goes:** FieldMesh is a fork that follows MeshCore releases. FieldMesh logic lives in its own files — pure logic in header-only files with a unit test, companion code in `MyMeshFieldMesh.cpp`, the UI in `ui-new/` and `ui-orig/`, the repeater in `simple_repeater/FieldMesh*`. Upstream files only get short, marked hooks, listed in [UPSTREAM_SYNC.md](https://github.com/fieldmesh-radio/FieldMesh/blob/main/UPSTREAM_SYNC.md). Bugs in MeshCore itself are best fixed [upstream](https://github.com/meshcore-dev/MeshCore).
- **Checks:** please build at least the devices your change touches. The unit tests (`pio test -e native`, FieldMesh tests in `test/test_fieldmesh_*`; every test file needs its own `main()`) run on every pull request; *FieldMesh Build Check* builds all FieldMesh devices once the change is on a `dev/` branch or in a pull request to `main`.
- **Style:** follow the existing code (`.clang-format`, 2 spaces). UI text and new comments in English. Device-specific code behind guards (`#if UI_HAS_JOYSTICK`, `#ifdef PIN_BUTTON2`, …). Keep changes small and focused — one topic per pull request.
- **Hardware:** say which devices you tested on. Prebuilt firmware is only released for devices that were tested on real hardware.

## AI-assisted contributions

Welcome — FieldMesh itself is written with a coding assistant (see [How FieldMesh is made](https://github.com/fieldmesh-radio/FieldMesh#how-fieldmesh-is-made)). What counts is that you built and tested the change yourself and can explain it. Please mention in the pull request that you used one. Pull requests that nobody has built or tested will be closed.

## Credits and license

Everyone who reports, tests, suggests or contributes is thanked by name in the release notes. Code taken over from others is committed with a `Co-Authored-By:` line. Contributions are made under the MIT License of this repository ([license.txt](https://github.com/fieldmesh-radio/FieldMesh/blob/main/license.txt)).
