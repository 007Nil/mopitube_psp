# MopiTube

A PSP homebrew application that acts as a [Mopidy](https://mopidy.com/) remote control via the MPD protocol. Displays now-playing info, album artwork, and a seek bar on the PSP's native GPU renderer.

---

## Features

- Now-playing display: title, artist, album
- Album artwork (128×128) fetched via MPD binary protocol with Mopidy HTTP API fallback
- Seek bar with elapsed/total time
- Playback controls: play/pause, next, previous
- Auto-reconnect on MPD connection loss
- PSP XMB icon (ICON0.PNG)

---

## Requirements

### Build

- [pspdev](https://github.com/pspdev/pspdev) toolchain (`psp-gcc`, `psp-config`, `psp-build-exports`)
- PSPSDK (included with pspdev)
- Libraries bundled with pspdev: `intrafont`, `libjpeg`, `libpng`, `libz`

```bash
export PSPDEV=/usr/local/pspdev
export PATH=$PATH:$PSPDEV/bin
```

### Runtime

- PSP with custom firmware (CFW) — e.g. ME/LME or PRO
- Wi-Fi access point configured in PSP Network Settings
- [Mopidy](https://mopidy.com/) server with [Mopidy-MPD](https://mopidy-mpd.readthedocs.io/) and [Mopidy-HTTP](https://docs.mopidy.com/en/latest/ext/http/) extensions

**Recommended Mopidy config:**
```ini
[mpd]
enabled = true
hostname = ::
port = 6600

[http]
enabled = true
hostname = ::
port = 6680
```

---

## Build

```bash
make
```

Produces `EBOOT.PBP` (the PSP executable) in the project root.

To clean build artifacts:
```bash
make clean
```

---

## Installation

1. Create the game directory on your PSP memory stick:
   ```
   ms0:/PSP/GAME/MopiTube/
   ```

2. Copy `EBOOT.PBP` into that directory.

3. Create `config.txt` in the same directory (see **Configuration** below).

4. Launch from the PSP XMB under Game → Memory Stick.

---

## Configuration

`ms0:/PSP/GAME/MopiTube/config.txt` — plain text, one `key=value` per line. See `config.example.txt` for a template.

| Key            | Required | Default | Description |
|----------------|----------|---------|-------------|
| `host`         | yes      | —       | Mopidy server IP address (no hostname — PSP has no DNS) |
| `port`         | no       | `6600`  | Mopidy-MPD port |
| `password`     | no       | empty   | MPD password (leave blank if none) |
| `wifi_profile` | yes      | —       | PSP Network Settings slot (1–9) |
| `http_port`    | no       | `6680`  | Mopidy HTTP API port (used for album art fallback) |

**Example:**
```
host=192.168.1.50
port=6600
password=
wifi_profile=1
http_port=6680
```

---

## Controls

| Button | Action |
|--------|--------|
| `X`    | Play / Pause (toggles; starts playback if stopped) |
| `L`    | Previous track |
| `R`    | Next track |
| `HOME` | Exit application |

---

## Architecture

```
src/
  main.c      — startup, Wi-Fi/MPD init, main loop (poll at 2 Hz, dirty-flag render)
  config.c/h  — parse config.txt from ms0:
  net.c/h     — PSP-native TCP sockets (sceNetInet*), Wi-Fi via sceNetApctl
  mpd.c/h     — MPD text protocol: currentsong, status, play/pause/next/prev, albumart
  http.c/h    — minimal HTTP/1.0 client (GET + POST JSON)
  artwork.c/h — album art: fetch → JPEG decode → 128×128 ABGR GPU texture
  ui.c/h      — PSP GU renderer: header, art quad, song info, seek bar, legend
  input.c/h   — sceCtrlReadBufferPositive wrapper with edge-detection
```

### Album art pipeline

1. `artwork_load()` is called every poll with the current song's file URI.
2. Tries `albumart`/`readpicture` binary MPD commands first.
3. Falls back to Mopidy HTTP JSON-RPC: `POST /mopidy/rpc` → `core.library.get_images` → `GET <image path>`.
4. Decodes JPEG via libjpeg with DCT scaling, nearest-neighbour resize to 128×128 ABGR.
5. Flushes CPU D-cache (`sceKernelDcacheWritebackInvalidateRange`) and GPU texture cache (`sceGuTexFlush`) before rendering.

### Key PSP-specific constraints

- **Use `sceNetInet*` native API** — POSIX socket wrappers (`recv`, `send`, `connect`) are broken on PSPSDK and return garbage without writing buffers.
- **GU state reset before every rect draw** — intraFont leaves the GPU in a CLUT/palette texture mode; forgetting to call `sceGuDisable(GU_TEXTURE_2D)` makes plain-colour rectangles invisible.
- **Texture alignment** — GPU textures must be declared `__attribute__((aligned(16)))`.
- **PPSSPP networking** — PPSSPP's `sceNetInet*` stubs are not functional; test networking on real hardware only.

---

## Development

`deploy.sh` builds and copies `EBOOT.PBP` to a mounted PSP memory stick in one step:

```bash
./deploy.sh                   # auto-detects the mount under /media/$USER, /run/media/$USER, /mnt
./deploy.sh /path/to/mount    # or pass the mount root explicitly
```

If a local `config.txt` exists it is copied too (without overwriting any existing one on the PSP).

The standard edit/test cycle is: edit → `./deploy.sh` → eject → run on PSP hardware.

---

## Debugging

The bottom-right corner of the now-playing screen shows a live diagnostic line:

```
art:ok 42318B px=FF3A2B1C
```

| Field    | Meaning |
|----------|---------|
| `art:ok` | Artwork loaded successfully (`none`/`0B`/`!jpg`/`fail` on error) |
| `NNNNNB` | Bytes received from server |
| `px=…`   | First pixel of the decoded texture (ABGR hex) |

MPD `ACK` error messages are shown in purple above the button legend.
