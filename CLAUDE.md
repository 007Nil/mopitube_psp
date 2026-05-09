# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

MopiTube is a PSP homebrew application that acts as a remote control for a [Mopidy](https://mopidy.com/) server via the MPD protocol. It runs on real PSP hardware (or a PSP emulator), connects over Wi-Fi, and displays a Now Playing screen with transport controls.

## Build

Two build systems are provided; both produce `EBOOT.PBP`.

**Makefile (PSPSDK classic):**
```sh
make
```
Requires `psp-config` on `$PATH` (provided by PSPSDK).

**CMake:**
```sh
cmake -B build
cmake --build build
```
Requires `$PSPDEV` set to the pspdev installation root (used to locate the toolchain file automatically).

Deploy the output `EBOOT.PBP` to `ms0:/PSP/GAME/MopiTube/EBOOT.PBP` on the PSP memory stick.

There are no tests — the only way to test is to run on PSP hardware or in PPSSPP.

## Architecture

The startup sequence in `main.c` is strictly ordered: config → Wi-Fi → MPD connect → main loop. Each stage calls `fatal()` on failure, which blocks until the user presses HOME.

The main loop runs at ~60 fps (`sceKernelDelayThread(16 * 1000)`) and polls MPD state at 2 Hz (`poll_interval = tick_res / 2`). Setting `last_poll = 0` forces an immediate refresh after a transport command.

### Module responsibilities

| Module | Role |
|--------|------|
| `config` | Parses `ms0:/PSP/GAME/MopiTube/config.txt` (key=value, `#` comments). Populates the `g_config` global. |
| `net` | Thin wrapper around PSP's `sceNet*` / `sceNetApctl*` APIs. Provides Wi-Fi connect (polls up to 10 s for IP), and blocking TCP helpers used by `mpd`. `host` must be a dotted-decimal IP — no DNS. |
| `mpd` | Speaks the MPD text protocol over the TCP fd returned by `net`. Parses `currentsong` and `status` responses line-by-line; falls back to filename when the Title tag is absent. |
| `ui` | Renders to the PSP debug screen (60 × 34 character grid, 8×8 px cells on 480×272). Colors are **ABGR**, not RGBA — e.g. `0xFFFFFF00` is cyan. |
| `input` | Edge-triggered controller wrapper. Call `input_update()` once per frame, then `input_pressed(btn)`. Mappings: X=play/pause, L=previous, R=next. |

### Config file

Deployed separately to the PSP at `ms0:/PSP/GAME/MopiTube/config.txt`. See `config.example.txt` for the format. The `host` field must be a raw IP address (the PSP TCP stack is used directly via `inet_addr`).

### PSP-specific constraints

- 20 MB heap (`PSP_HEAP_SIZE_KB(20480)`)
- Socket send/recv timeout: 5 seconds
- On MPD connection loss the app attempts one reconnect (2 s delay) before retrying the poll loop
- The exit callback runs in a separate PSP thread and sets `g_running = 0`
