"""Bilateral trigger timing from a logic-analyzer capture (docs/SYNC_VALIDATION.md).

Reads a VCD exported from PulseView/sigrok. Two channels are required: each
glove's SYNC_DEBUG_GPIO pin (A0 on v2, IO40 on v3), where every edge, rising
or falling, is one ACTIVATE. Two more are optional: one LRA terminal per
glove, same finger on both, where each buzz shows as a dense run of driver
switching edges and the first edge after a quiet gap is the moment the drive
started.

Reports, each as signed offsets with mean / std / P95 / worst case:
  firmware trigger offset    SECONDARY debug pin - PRIMARY debug pin
  motor drive offset         SECONDARY motor - PRIMARY motor     (with motors)
  command-to-drive delay     motor - debug pin, per glove        (with motors)
A negative bilateral offset means the SECONDARY fired first.

    python3 scripts/sync_skew.py run1.vcd
    python3 scripts/sync_skew.py run1.vcd --primary D0 --secondary D1 \\
        --primary-motor D2 --secondary-motor D3 --quiet-ms 4
    python3 scripts/sync_skew.py --selftest
"""

import argparse
import bisect
import statistics
import sys

# ponytail: activations are tens of ms apart, so a 20 ms window can't pair
# neighbours. Edges with no partner inside it are reported as unpaired.
PAIR_WINDOW_US = 20_000

# A burst that runs this long means the motor terminal isn't quiet when idle,
# so burst starts can't be trusted (TIME_ON tops out at 200 ms).
MAX_BURST_US = 1_000_000

UNITS_US = {"s": 1e6, "ms": 1e3, "us": 1.0, "ns": 1e-3, "ps": 1e-6, "fs": 1e-9}


def parse_vcd(text, names):
    """Return {name: [edge times in µs]} for the named 1-bit signals present."""
    header, _, body = text.partition("$enddefinitions")
    tokens = header.split()
    scale_us, ids = None, {}
    for i, tok in enumerate(tokens):
        if tok == "$timescale":
            spec = "".join(tokens[i + 1:tokens.index("$end", i)])
            num = spec.rstrip("munpfs")
            scale_us = float(num) * UNITS_US[spec[len(num):]]
        elif tok == "$var" and tokens[i + 4] in names:
            ids[tokens[i + 3]] = tokens[i + 4]
    if scale_us is None:
        sys.exit("VCD has no $timescale")

    edges = {n: [] for n in ids.values()}
    level = {}
    t = 0.0
    for tok in body.split()[1:]:  # [0] is the "$end" closing $enddefinitions
        if tok[0] == "#":
            t = int(tok[1:]) * scale_us
        elif tok[0] in "01xXzZ" and tok[1:] in ids:
            sig = ids[tok[1:]]
            if sig in level and level[sig] != tok[0]:
                edges[sig].append(t)
            level[sig] = tok[0]  # first value seen is the start level, not an edge
    return edges


def bursts(edges, quiet_us):
    """Group switching edges into (start, end) bursts split by quiet gaps."""
    out = []
    for t in edges:
        if out and t - out[-1][1] <= quiet_us:
            out[-1][1] = t
        else:
            out.append([t, t])
    return [tuple(b) for b in out]


def pair(a, b, window_us=PAIR_WINDOW_US):
    """Signed offsets b - a (µs), each b used once, and unpaired counts."""
    offsets, used = [], set()
    for ta in a:
        i = bisect.bisect_left(b, ta)
        best = min((j for j in (i - 1, i) if 0 <= j < len(b) and j not in used),
                   key=lambda j: abs(b[j] - ta), default=None)
        if best is not None and abs(b[best] - ta) <= window_us:
            used.add(best)
            offsets.append(b[best] - ta)
    return offsets, len(a) - len(offsets), len(b) - len(used)


def summary(title, offsets, count_line, bilateral):
    print(f"\n{title}")
    print(f"  {count_line}")
    if len(offsets) < 2:
        print("  not enough pairs; check wiring, channel names and --quiet-ms")
        return
    mags = sorted(abs(o) for o in offsets)
    mean = statistics.fmean(offsets)
    lead = f"  ({'SECONDARY' if mean < 0 else 'PRIMARY'} first)" if bilateral else ""
    print(f"  mean               : {mean / 1000:+.3f} ms{lead}")
    print(f"  std deviation      : {statistics.stdev(offsets) / 1000:.3f} ms")
    print(f"  mean |offset|      : {statistics.fmean(mags) / 1000:.3f} ms")
    print(f"  P95 |offset|       : {mags[int(0.95 * (len(mags) - 1))] / 1000:.3f} ms")
    print(f"  worst case         : {max(offsets, key=abs) / 1000:+.3f} ms")


def analyse(edges, pa0, sa0, pm, sm, quiet_us):
    missing = [n for n in (pa0, sa0) if n not in edges]
    if missing:
        sys.exit(f"VCD has no channel named {', '.join(missing)}")

    offsets, up, us = pair(edges[pa0], edges[sa0])
    summary(f"Firmware trigger offset (debug pin: {sa0} - {pa0})", offsets,
            f"pairs              : {len(offsets)}  (unpaired: PRIMARY {up}, SECONDARY {us})",
            bilateral=True)

    if pm not in edges or sm not in edges:
        print(f"\n(no {pm}/{sm} in the capture: motor timing skipped)")
        return

    starts = {}
    for name in (pm, sm):
        b = bursts(edges[name], quiet_us)
        longest = max((e - s for s, e in b), default=0)
        if longest > MAX_BURST_US:
            print(f"\nWARNING: {name} has a {longest / 1e6:.1f} s burst. The motor terminal "
                  "isn't quiet when idle, so its burst starts are not trustworthy.")
        starts[name] = [s for s, _ in b]

    offsets, up, us = pair(starts[pm], starts[sm])
    summary(f"Motor drive offset ({sm} - {pm})", offsets,
            f"pairs              : {len(offsets)}  (unpaired: PRIMARY {up}, SECONDARY {us})",
            bilateral=True)

    for glove, a0, m in (("PRIMARY", pa0, pm), ("SECONDARY", sa0, sm)):
        # The debug pin toggles for every finger; only the probed finger's edge is near a burst.
        delays, _, _ = pair(edges[a0], starts[m])
        summary(f"Command-to-drive delay, {glove} ({m} - {a0})", delays,
                f"bursts matched     : {len(delays)} of {len(starts[m])}",
                bilateral=False)


def selftest():
    # 1 µs timescale. SECONDARY debug pin 1500 µs early on every edge, one dropped
    # PRIMARY event, and start levels that must not count as edges.
    vcd = """$timescale 1 us $end
$scope module logic $end
$var wire 1 ! D0 $end
$var wire 1 " D1 $end
$upscope $end
$enddefinitions $end
#0 0! 0"
#98500 1"
#100000 1!
#198500 0"
#200000 0!
#300000 1!
#398500 1"
#400000 0!
"""
    e = parse_vcd(vcd, ["D0", "D1"])
    assert e["D0"] == [100000, 200000, 300000, 400000], e
    assert e["D1"] == [98500, 198500, 398500], e
    offsets, up, us = pair(e["D0"], e["D1"])
    assert offsets == [-1500, -1500, -1500] and (up, us) == (1, 0), (offsets, up, us)
    assert parse_vcd(vcd.replace("1 us", "10ns"), ["D0"])["D0"][0] == 1000

    # Two bursts of 50 µs switching with a 2 ms mid-burst pause, 167 ms apart.
    sw = [t0 + k * 50 for t0 in (0, 167_000) for k in range(40)]
    sw += [167_000 + 4_000 + k * 50 for k in range(40)]  # 2 ms pause after 2 ms
    b = bursts(sorted(sw), quiet_us=4_000)
    assert [s for s, _ in b] == [0, 167_000], b
    print("selftest ok")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("vcd", nargs="?")
    ap.add_argument("--primary", default="D0", help="PRIMARY debug pin channel (default D0)")
    ap.add_argument("--secondary", default="D1", help="SECONDARY debug pin channel (default D1)")
    ap.add_argument("--primary-motor", default="D2", help="PRIMARY LRA terminal (default D2)")
    ap.add_argument("--secondary-motor", default="D3", help="SECONDARY LRA terminal (default D3)")
    # Calibration knob: must exceed the longest pause inside one buzz and stay
    # under the shortest gap between buzzes (MIN_INTER_BURST_GAP_MS = 5).
    ap.add_argument("--quiet-ms", type=float, default=4.0,
                    help="gap that ends a motor burst (default 4)")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        selftest()
    elif not args.vcd:
        ap.error("give a .vcd file or --selftest")
    else:
        names = [args.primary, args.secondary, args.primary_motor, args.secondary_motor]
        with open(args.vcd) as f:
            edges = parse_vcd(f.read(), names)
        analyse(edges, *names, quiet_us=args.quiet_ms * 1000)
