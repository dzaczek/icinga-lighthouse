#!/usr/bin/env bash
#
# flash.sh — build (via build.sh) and flash the firmware to a connected ESP32,
#            auto-detecting the serial port on macOS and Linux.
#
# Usage:
#   ./flash.sh                       # auto-detect port, build + upload
#   ./flash.sh /dev/cu.usbserial-XXX # force a specific port
#   ./flash.sh --erase               # wipe flash (incl. saved config/NVS) before upload
#   ./flash.sh --monitor             # open the serial monitor after flashing
#   PORT=/dev/ttyUSB0 ./flash.sh     # port via env var
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="$HERE/.build"
BOARD="${BOARD:-esp32dev}"

# --- parse args -------------------------------------------------------------
PORT="${PORT:-}"
DO_ERASE=0
DO_MONITOR=0
for a in "$@"; do
  case "$a" in
    --erase)   DO_ERASE=1 ;;
    --monitor) DO_MONITOR=1 ;;
    -*)        echo "Unknown option: $a" >&2; exit 1 ;;
    *)         PORT="$a" ;;
  esac
done

# --- auto-detect the serial port --------------------------------------------
# Prefer cu.* on macOS (non-blocking) and ttyUSB/ttyACM on Linux. The first
# match wins; some adapters expose two nodes for one board, which is fine.
detect_ports() {
  local pat p
  case "$(uname -s)" in
    Darwin) set -- /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART /dev/cu.wchusbserial* /dev/cu.usbmodem* ;;
    Linux)  set -- /dev/ttyUSB* /dev/ttyACM* ;;
    *)      set -- ;;
  esac
  for pat in "$@"; do
    for p in $pat; do [ -e "$p" ] && echo "$p"; done
  done
}

if [ -z "$PORT" ]; then
  found="$(detect_ports || true)"
  PORT="$(printf '%s\n' "$found" | head -n1)"
  count="$(printf '%s\n' "$found" | grep -c . || true)"
  if [ -z "$PORT" ]; then
    echo "ERROR: no serial port found. Plug in the board, or pass one explicitly:" >&2
    echo "  ./flash.sh /dev/cu.usbserial-XXXX   (macOS)" >&2
    echo "  ./flash.sh /dev/ttyUSB0             (Linux)" >&2
    exit 1
  fi
  if [ "$count" -gt 1 ]; then
    echo ">> Multiple ports detected; using the first. Override with: ./flash.sh <port>" >&2
    printf '   %s\n' $found >&2
  fi
fi

[ -e "$PORT" ] || { echo "ERROR: port not found: $PORT" >&2; exit 1; }
echo ">> Target port: $PORT"

# --- build ------------------------------------------------------------------
"$HERE/build.sh"

# --- optional full erase (clears saved WiFi/Icinga config in NVS) -----------
if [ "$DO_ERASE" -eq 1 ]; then
  echo ">> Erasing flash (this also wipes saved config)..."
  pio run -d "$BUILD" -t erase --upload-port "$PORT"
fi

# --- upload -----------------------------------------------------------------
echo ">> Flashing $PORT ..."
pio run -d "$BUILD" -t upload --upload-port "$PORT"
echo ">> Done. The board has rebooted."

# --- optional serial monitor ------------------------------------------------
if [ "$DO_MONITOR" -eq 1 ]; then
  echo ">> Opening serial monitor (Ctrl-C to quit)..."
  pio device monitor -d "$BUILD" -p "$PORT" -b 115200
fi
