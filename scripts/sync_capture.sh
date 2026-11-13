#!/bin/bash
# One sync-validation run (docs/SYNC_VALIDATION.md): logs both gloves, runs the
# logic analyzer and starts a TEST session, into temp/analyzer-runs/<timestamp>/.
# Hands-off: it reboots both gloves, waits for them to link, sends DEBUG:1 and
# TEST to the PRIMARY and ends when the session does.
#
#   scripts/sync_capture.sh [longest capture in seconds, default 180]
#
# Quit PulseView first: it holds the analyzer.
# Overrides: PRIMARY=/dev/cu.x SECONDARY=/dev/cu.y CHANNELS=D0,D1,D2,D3
#   NO_REBOOT=1  don't reboot the gloves (keeps a phone connected to the PRIMARY)
#   NO_TEST=1    don't send anything; wait for a session started from the app,
#                then capture for the full time
set -euo pipefail
cd "$(dirname "$0")/.."

PRIMARY=${PRIMARY:-/dev/cu.usbmodem101}
SECONDARY=${SECONDARY:-/dev/cu.usbmodem1101}
CHANNELS=${CHANNELS:-D0,D1}
SECS=${1:-180}

dir=temp/analyzer-runs/$(date +%Y-%m-%d-%H-%M-%S)
mkdir -p "$dir"
echo "Commit: $(git rev-parse --short HEAD)" > "$dir/run.md"

# Logs one port for the whole run, reopening it whenever the glove reboots (the
# REBOOT below, or the app's PROFILE_LOAD) and its USB port drops and comes
# back. The firmware waits up to 3 s at boot for the port to be opened, so the
# boot lines are kept. raw -echo matters: with echo on, the tty would send a
# glove's own output back to it as commands.
log_port() {
    local reader=
    trap '[ -n "$reader" ] && kill $reader 2>/dev/null; exit 0' TERM
    while :; do
        if [ -e "$1" ] && exec 9<"$1" 2>/dev/null; then
            stty -f "$1" 115200 raw -echo || true
            cat <&9 &
            reader=$!
            wait $reader || true
            exec 9<&-
        fi
        sleep 0.2
    done
}
send() { printf '%s\n' "$2" > "$1"; }
# True once the PRIMARY has reported the SECONDARY link, after its latest boot.
linked() {
    awk '/\[BOOT\] Serial ready/ {seen = 0} /SECONDARY: Connected/ {seen = 1} END {exit !seen}' \
        "$dir/primary.log"
}

log_port "$PRIMARY" | tee "$dir/primary.log" &   # PRIMARY output is also shown live
log_port "$SECONDARY" > "$dir/secondary.log" &
trap 'kill $(jobs -p) 2>/dev/null || true; pkill -P $$ 2>/dev/null || true' EXIT
sleep 1

if [ -z "${NO_REBOOT:-}" ]; then
    send "$PRIMARY" REBOOT
    send "$SECONDARY" REBOOT
    for _ in $(seq 100); do
        grep -aq '\[BOOT\] Serial ready' "$dir/primary.log" && break
        sleep 0.1
    done
fi

echo "Waiting for the gloves to report their link..."
for _ in $(seq 60); do linked && break; sleep 1; done
linked || { echo "Gloves didn't link within 60 s"; exit 1; }
sleep 5  # ponytail: fixed wait for clock sync; raise it if TEST is refused "Sync not ready"

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
    send "$PRIMARY" DEBUG:1
    sleep 2
    send "$PRIMARY" TEST
fi

# Done when the PRIMARY reports the TEST over; the capture time is the fallback
# (and the only end for an app-started session).
until grep -aq 'TEST COMPLETE' "$dir/primary.log" || ! kill -0 $sigrok_pid 2>/dev/null; do
    sleep 1
done
sleep 1
kill $sigrok_pid 2>/dev/null || true   # sigrok-cli keeps a valid .sr when terminated
wait $sigrok_pid || true
rm -f "$dir"/capture.sr.*.part
sigrok-cli -i "$dir/capture.sr" -O vcd -o "$dir/capture.vcd"
python3 scripts/sync_skew.py "$dir/capture.vcd" | tee "$dir/skew.txt"
echo "Saved to $dir"
