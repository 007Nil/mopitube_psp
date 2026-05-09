#!/usr/bin/env bash
# Build MopiTube and copy EBOOT.PBP to a mounted PSP memory stick.
#
# Usage:
#   ./deploy.sh                   # auto-detect PSP mount under /media/$USER, /run/media/$USER, /mnt
#   ./deploy.sh /path/to/mount    # explicit mount root (the dir that contains PSP/)
#
# Honours $PSPDEV (defaults to /usr/local/pspdev).

set -euo pipefail

PSPDEV="${PSPDEV:-/usr/local/pspdev}"
export PSPDEV
export PATH="$PSPDEV/bin:$PATH"

if [[ ! -x "$PSPDEV/bin/psp-config" ]]; then
    echo "error: psp-config not found in $PSPDEV/bin" >&2
    echo "       set PSPDEV to your pspdev installation root" >&2
    exit 1
fi

echo "==> Building MopiTube"
make

[[ -f EBOOT.PBP ]] || { echo "error: EBOOT.PBP was not produced" >&2; exit 1; }

PSP_ROOT=""
if [[ $# -gt 0 ]]; then
    PSP_ROOT="$1"
else
    for base in "/media/$USER" "/run/media/$USER" /mnt; do
        [[ -d "$base" ]] || continue
        for candidate in "$base"/*; do
            if [[ -d "$candidate/PSP" ]]; then
                PSP_ROOT="$candidate"
                break 2
            fi
        done
    done
fi

if [[ -z "$PSP_ROOT" ]]; then
    echo "error: no PSP memory stick mount found" >&2
    echo "       insert the memory stick or pass its mount path explicitly" >&2
    exit 1
fi

DEST="$PSP_ROOT/PSP/GAME/MopiTube"
echo "==> Deploying to $DEST"

mkdir -p "$DEST"
cp EBOOT.PBP "$DEST/"
echo "    copied EBOOT.PBP ($(stat -c%s EBOOT.PBP) bytes)"

if [[ -f config.txt ]]; then
    if [[ -f "$DEST/config.txt" ]]; then
        echo "    config.txt already on PSP, leaving it alone"
    else
        cp config.txt "$DEST/"
        echo "    copied config.txt"
    fi
fi

sync
echo "==> Done. Safe to eject the memory stick."
