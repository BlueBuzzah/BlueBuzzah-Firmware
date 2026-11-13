#!/bin/bash
# One sync-validation run (docs/SYNC_VALIDATION.md): logs both gloves, runs the
# logic analyzer and starts a TEST session, into temp/analyzer-runs/<timestamp>/.
# Hands-off: it reboots both gloves, waits for them to link, sends DEBUG:1 and
# TEST to the PRIMARY and ends when the session does.
#
#   scripts/sync_capture.sh [longest capture in seconds, default 180]
#
# Quit PulseView first: it holds the analyzer.
# Overrides: PRIMARY=/dev/cu.x SECONDARY=/dev/cu.y CHANNELS=D0,D1,D2,D3 NO_REBOOT=1 NO_TEST=1
set -euo pipefail
cd "$(dirname "$0")/.."

PRIMARY=${PRIMARY:-/dev/cu.usbmodem101}
SECONDARY=${SECONDARY:-/dev/cu.usbmodem1101}
CHANNELS=${CHANNELS:-D0,D1}
SECS=${1:-180}

dir=temp/analyzer-runs/$(date +%Y-%m-%d-%H-%M-%S)
mkdir -p "$dir"
echo "Commit: $(git rev-parse --short HEAD)" > "$dir/run.md"

# raw -echo matters: with echo on, the tty would send a glove's own output
# back to it as commands. The open fds keep the settings alive.
open_ports() {
    exec 3<>"$PRIMARY" 4<>"$SECONDARY"
    stty -f "$PRIMARY" 115200 raw -echo
    stty -f "$SECONDARY" 115200 raw -echo
}

# Reboot both gloves so the logs start at boot. The USB ports drop and come
# back; the firmware waits up to 3 s for the port to be opened before printing.
# NO_REBOOT=1 skips this, to keep a phone connected to the PRIMARY.
open_ports
if [ -z "${NO_REBOOT:-}" ]; then
    printf 'REBOOT\n' >&3
    printf 'REBOOT\n' >&4
    sleep 1
    exec 3>&- 4>&-
    for port in "$PRIMARY" "$SECONDARY"; do
        for _ in $(seq 100); do [ -e "$port" ] && break; sleep 0.1; done
        [ -e "$port" ] || { echo "$port didn't come back after the reboot"; exit 1; }
    done
    open_ports
fi
tee "$dir/primary.log" <&3 &          # PRIMARY output is also shown live
cat <&4 > "$dir/secondary.log" &
trap 'kill $(jobs -p) 2>/dev/null || true' EXIT

echo "Waiting for the gloves to report their link..."
for _ in $(seq 60); do
    grep -aq 'SECONDARY: Connected' "$dir/primary.log" && break
    sleep 1
done
grep -aq 'SECONDARY: Connected' "$dir/primary.log" || { echo "Gloves didn't link within 60 s"; exit 1; }
sleep 5  # ponytail: fixed wait for clock sync; raise it if TEST is refused "Sync not ready"

# NO_TEST=1: the session is started from the app instead. Wait for it, then
# capture for the full time (nothing is sent, so a running session isn't disturbed).
if [ -n "${NO_TEST:-}" ]; then
    echo "Start a session from the app (waiting up to 5 minutes)..."
    for _ in $(seq 300); do
        grep -aq 'RUNNING' "$dir/primary.log" && break
        sleep 1
    done
    grep -aq 'RUNNING' "$dir/primary.log" || { echo "No session started"; exit 1; }
fi

sigrok-cli -d fx2lafw -c samplerate=2M -C "$CHANNELS" --time "${SECS}s" \
    -o "$dir/capture.sr" &
sigrok_pid=$!

sleep 1
kill -0 $sigrok_pid 2>/dev/null || { echo "Analyzer didn't start (is PulseView open?)"; exit 1; }

echo "Capturing ${SECS}s into $dir"
if [ -z "${NO_TEST:-}" ]; then
    printf 'DEBUG:1\n' >&3
    sleep 2
    printf 'TEST\n' >&3
fi

# Done when the PRIMARY reports the session over; the capture time is the fallback.
until grep -aq 'TEST COMPLETE' "$dir/primary.log" || ! kill -0 $sigrok_pid 2>/dev/null; do
    sleep 1
done
sleep 1
kill $(jobs -p) 2>/dev/null || true   # sigrok-cli keeps a valid .sr when terminated
wait $sigrok_pid || true
rm -f "$dir"/capture.sr.*.part
sigrok-cli -i "$dir/capture.sr" -O vcd -o "$dir/capture.vcd"
python3 scripts/sync_skew.py "$dir/capture.vcd" | tee "$dir/skew.txt"
echo "Saved to $dir"
