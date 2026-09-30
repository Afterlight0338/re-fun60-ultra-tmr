#!/usr/bin/env python3
"""
chatter_test.py - double-click / key-chatter tester (FUN60 Ultra, or any evdev keyboard).

Reads the kernel's key events (evdev) from every input device of the keyboard, with
microsecond monotonic timestamps, and flags a key re-pressed within --gap-ms of its own
release. Stdlib only; needs read access to /dev/input/event* (root or the `input` group).

  nix-shell -p python3 --run 'sudo "$(command -v python3)" scripts/chatter_test.py'
      --gap-ms 100    deliberate test: press keys one at a time, slowly
      --vendor 03f0   same test on another keyboard for comparison (HyperX)
  python3 scripts/chatter_test.py --selftest       (no device needed)

Each keystroke prints its hold time and the gap since that key's previous release.
Ctrl+C prints the report: per-key double rates, gap/hold histograms, every double with
its timing, and the smallest debounce lockout that would have suppressed all of them.
Every event also goes to a CSV. Timing resolution is the keyboard's polling interval
(0.125 ms at 8000 Hz), as seen on arrival at the host.
"""
import argparse, csv, fcntl, glob, os, select, struct, sys, termios, time
from statistics import median

EV_KEY, EV_MSC, MSC_SCAN = 1, 4, 4
EVENT = struct.Struct("llHHi")             # struct input_event (64-bit Linux)
EVIOCSCLOCKID = 0x400445A0                 # _IOW('E', 0xa0, int)

_BASE = ("ESC 1 2 3 4 5 6 7 8 9 0 - = BKSP TAB Q W E R T Y U I O P [ ] ENTER LCTRL "
         "A S D F G H J K L ; ' ` LSHIFT \\ Z X C V B N M , . / RSHIFT KP* LALT SPACE CAPS").split()
KEYS = {i + 1: n for i, n in enumerate(_BASE)}
KEYS.update({59 + i: f"F{i + 1}" for i in range(10)})
KEYS.update({87: "F11", 88: "F12", 97: "RCTRL", 100: "RALT", 102: "HOME", 103: "UP",
             104: "PGUP", 105: "LEFT", 106: "RIGHT", 107: "END", 108: "DOWN", 109: "PGDN",
             110: "INS", 111: "DEL", 113: "MUTE", 114: "VOL-", 115: "VOL+",
             125: "LMETA", 126: "RMETA", 127: "MENU"})


def keyname(code):
    return KEYS.get(code, f"KEY_{code}")


class Analyzer:
    """Per-(device, key) press/release bookkeeping. Event times in seconds, results in ms."""

    def __init__(self, gap_ms):
        self.gap_ms = gap_ms
        self.down, self.up, self.gap, self.last_hold = {}, {}, {}, {}
        self.strokes = []     # (t, dev, key, hold, gap|None, double)
        self.doubles = []     # (t, dev, key, hold_before, gap, hold_after)
        self.dups = []        # (t, key, dev_a, dev_b, dt): one key pressed on two interfaces
        self.last_press = {}  # key -> (t, dev)
        self.edges = {}       # (dev, key) -> [(t_ms, 1|0)], raw stream for the filter replay
        self.repeats = self.lost_release = 0

    def press(self, t, dev, key):
        k = (dev, key)
        self.edges.setdefault(k, []).append((t * 1e3, 1))
        if k in self.down:
            self.lost_release += 1            # down while already down: a release went missing
        self.down[k] = t
        self.gap[k] = (t - self.up[k]) * 1e3 if k in self.up else None
        prev = self.last_press.get(key)
        if prev and prev[1] != dev and (t - prev[0]) * 1e3 < 2:
            self.dups.append((t, key, prev[1], dev, (t - prev[0]) * 1e3))
        self.last_press[key] = (t, dev)
        return self.gap[k]

    def release(self, t, dev, key):
        k = (dev, key)
        self.edges.setdefault(k, []).append((t * 1e3, 0))
        if k not in self.down:
            return None                       # was already held when the test started
        hold = (t - self.down.pop(k)) * 1e3
        gap = self.gap.pop(k, None)
        double = gap is not None and gap < self.gap_ms
        if double:
            self.doubles.append((t, dev, key, self.last_hold.get(k), gap, hold))
        self.last_hold[k] = hold
        self.up[k] = t
        s = (t, dev, key, hold, gap, double)
        self.strokes.append(s)
        return s


# ---- filter replay: run each key's recorded raw edges through candidate debounce filters ----
INF = float("inf")


def eager(edges, L):
    """firmware/common/debounce.c: accept a change at once, then ignore the key for L ms."""
    out, state, raw, t_raw, lock = [], 0, 0, 0.0, -INF
    for t, v in edges + [(INF, None)]:
        ta = max(t_raw, lock)
        if raw != state and ta < t:           # a change held through the lock is taken when it ends
            state = raw
            out.append((ta, state))
            lock = ta + L
        if v is None:
            break
        raw, t_raw = v, t
        if raw != state and t >= lock:
            state = raw
            out.append((t, state))
            lock = t + L
    return out


def defer(edges, N):
    """Report a change only once the raw state has held for N ms (adds N ms latency)."""
    out, state = [], 0
    for (t, v), (t2, _) in zip(edges, edges[1:] + [(INF, None)]):
        if v != state and t2 - t >= N:
            state = v
            out.append((t + N, v))
    return out


def asym(edges, R):
    """Eager press, deferred release (QMK asym_eager_defer_pk): a press goes out at once; a
    release only after R ms with no re-press, so dropouts shorter than R vanish. Adds R ms to
    releases only, and merges real re-presses closer than R."""
    out, state = [], 0
    for (t, v), (t2, _) in zip(edges, edges[1:] + [(INF, None)]):
        if v == 1 and state == 0:
            state = 1
            out.append((t, 1))
        elif v == 0 and state == 1 and t2 - t >= R:
            state = 0
            out.append((t + R, 0))
    return out


def intervals(edges):
    res, start = [], None
    for t, v in edges:
        if v == 1 and start is None:
            start = t
        elif v == 0 and start is not None:
            res.append((start, t))
            start = None
    return res


def real_presses(edges, join=10, min_hold=30):
    """Reference answer: raw presses with gaps under `join` ms joined, kept if >= `min_hold` ms.
    ponytail: heuristic ground truth; a filled-in 'I pressed N times' count would be exact."""
    merged = []
    for s, e in intervals(edges):
        if merged and s - merged[-1][1] < join:
            merged[-1] = (merged[-1][0], e)
        else:
            merged.append((s, e))
    return [(s, e) for s, e in merged if e - s >= min_hold]


def score(out, real):
    """(extra, missed, worst_delay_ms): phantom or split presses, real presses lost or merged,
    and the latest any real press reached the host."""
    ov = lambda a, b: a[0] < b[1] and b[0] < a[1]
    per_out = [sum(ov(o, r) for r in real) for o in out]
    per_real = [sum(ov(o, r) for o in out) for r in real]
    extra = sum(c == 0 for c in per_out) + sum(max(0, c - 1) for c in per_real)
    missed = sum(c == 0 for c in per_real) + sum(max(0, c - 1) for c in per_out)
    delays = [min(o[0] for o in out if ov(o, r)) - r[0] for r, c in zip(real, per_real) if c]
    return extra, missed, max(delays, default=0.0)


FILTERS = [("none", 0)] + [("eager", x) for x in (2, 5, 10, 20, 30)] + \
          [("defer", x) for x in (1, 2, 3, 5, 8, 10)] + [("asym", x) for x in (2, 5, 8, 10, 15)]
APPLY = {"none": lambda e, p: e, "eager": eager, "defer": defer, "asym": asym}


def compare(edges_by_key):
    real = {k: real_presses(e) for k, e in edges_by_key.items()}
    n = sum(map(len, real.values()))
    print(f"\nfilter replay ({n} real presses = raw presses >= 30 ms after joining gaps < 10 ms):")
    print(f"  {'filter':<15}{'phantom/split':>15}{'lost/merged':>13}{'worst delay':>14}")
    rows = []
    for name, p in FILTERS:
        ex = mi = 0
        worst = 0.0
        for k, e in edges_by_key.items():
            out = APPLY[name](e, p)
            x, m, dl = score(intervals(out), real[k])
            ex, mi, worst = ex + x, mi + m, max(worst, dl)
        rows.append((name, p, ex, mi, worst))
        label = "none (stock)" if name == "none" else f"{name} {p:g} ms"
        print(f"  {label:<15}{ex:>15}{mi:>13}{worst:>11.2f} ms")
    clean = [r for r in rows if r[2] == 0 and r[3] == 0]
    if clean:
        best = min(clean, key=lambda r: r[4])
        print(f"  -> best perfect filter here: {best[0]} {best[1]:g} ms (worst delay {best[4]:.2f} ms)")
    else:
        best = min(rows, key=lambda r: (r[2] + r[3], r[4]))
        print(f"  -> no filter is perfect on this log; fewest errors: {best[0]} {best[1]:g} ms "
              f"({best[2]} phantom, {best[3]} lost)")


EDGES = [0, 1, 2, 5, 10, 20, 30, 50, 100, 200, 500, float("inf")]


def histogram(title, values):
    print(f"\n{title}")
    if not values:
        print("  (none)")
        return
    counts = [sum(lo <= v < hi for v in values) for lo, hi in zip(EDGES, EDGES[1:])]
    top = max(counts)
    for (lo, hi), c in zip(zip(EDGES, EDGES[1:]), counts):
        label = f"{lo:>4g}-{hi:<4g}" if hi != float("inf") else f"{lo:>4g}+    "
        print(f"  {label} ms {c:6d}  {'#' * max(1, round(40 * c / top)) if c else ''}")


def report(an, duration, csv_path):
    n, d = len(an.strokes), len(an.doubles)
    print("\n" + "=" * 78)
    if not n:
        print("no complete keystrokes recorded")
        return
    print(f"{n} keystrokes in {duration:.1f} s, {d} doubles ({100 * d / n:.2f}%), "
          f"threshold: re-press < {an.gap_ms:g} ms after release")

    per = {}
    for _, _, key, hold, gap, dbl in an.strokes:
        p = per.setdefault(key, {"n": 0, "d": 0, "gaps": [], "holds": []})
        p["n"] += 1
        p["d"] += dbl
        p["holds"].append(hold)
        if gap is not None:
            p["gaps"].append(gap)
    print(f"\n{'key':<8}{'presses':>8}{'doubles':>9}{'rate':>8}{'min gap':>11}{'min hold':>11}{'med hold':>11}")
    for key, p in sorted(per.items(), key=lambda kv: (-kv[1]["d"], -kv[1]["n"])):
        mg = f"{min(p['gaps']):.2f}" if p["gaps"] else "-"
        print(f"{keyname(key):<8}{p['n']:>8}{p['d']:>9}{100 * p['d'] / p['n']:>7.1f}%"
              f"{mg:>11}{min(p['holds']):>11.2f}{median(p['holds']):>11.2f}")

    histogram("gap: release -> next press of the same key", [s[4] for s in an.strokes if s[4] is not None])
    histogram("hold: press -> release", [s[3] for s in an.strokes])

    if an.doubles:
        print(f"\ndoubles ({d}{', first 60 shown' if d > 60 else ''}):")
        for t, dev, key, hb, gap, ha in an.doubles[:60]:
            hbs = f"{hb:7.2f}" if hb is not None else "      -"
            print(f"  [{t:9.3f}s] {keyname(key):<7} held {hbs} ms, released, re-pressed after "
                  f"{gap:6.2f} ms, held {ha:7.2f} ms   ({dev})")

    else:
        print("\nno doubles recorded")
    genuine = [s[4] for s in an.strokes if s[4] is not None and not s[5]]
    if genuine:
        print(f"\nfastest re-press not counted as a double: {min(genuine):.2f} ms")
    compare(an.edges)

    if an.dups:
        print(f"\n{len(an.dups)} presses arrived on two interfaces within 2 ms (host sees them twice):")
        for t, key, a, b, dt in an.dups[:10]:
            print(f"  [{t:9.3f}s] {keyname(key)}: {a} then {b} after {dt:.3f} ms")
    if an.lost_release:
        print(f"\n{an.lost_release} presses arrived while the key was already down (lost release)")
    print(f"\nautorepeat events ignored: {an.repeats}")
    if csv_path:
        print(f"full event log: {csv_path}")


def find_devices(vendor):
    devs = []
    for ev in sorted(glob.glob("/sys/class/input/event*"), key=lambda p: int(p.rsplit("event", 1)[1])):
        d = ev + "/device"
        try:
            if open(d + "/id/vendor").read().strip().lower() != vendor.lower():
                continue
            if int(open(d + "/capabilities/key").read().replace(" ", "").strip() or "0", 16) == 0:
                continue
            devs.append(("/dev/input/" + os.path.basename(ev), open(d + "/name").read().strip()))
        except (OSError, ValueError):
            continue
    return devs


def quiet_tty():
    """Stop typed keys echoing here; restore() also discards them so they can't reach the
    shell prompt after the test."""
    if not sys.stdin.isatty():
        return lambda: None
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    new = termios.tcgetattr(fd)
    new[3] &= ~(termios.ECHO | termios.ICANON)
    termios.tcsetattr(fd, termios.TCSANOW, new)

    def restore():
        termios.tcflush(fd, termios.TCIFLUSH)
        termios.tcsetattr(fd, termios.TCSANOW, old)
    return restore


def run(args):
    devs = find_devices(args.vendor)
    if not devs:
        sys.exit(f"no input device with USB vendor {args.vendor} (plugged in?)")
    fds, clock = {}, time.CLOCK_MONOTONIC
    for path, name in devs:
        try:
            fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
        except PermissionError:
            sys.exit(f"no permission to read {path}; run with sudo (see --help)")
        try:
            fcntl.ioctl(fd, EVIOCSCLOCKID, struct.pack("i", time.CLOCK_MONOTONIC))
        except OSError:
            clock = time.CLOCK_REALTIME       # all devices then report wall-clock time
        fds[fd] = f"{os.path.basename(path)}"
        print(f"  reading {path:<20} {name}")
    if clock == time.CLOCK_REALTIME:
        for fd in fds:                        # keep every device on one clock
            try:
                fcntl.ioctl(fd, EVIOCSCLOCKID, struct.pack("i", time.CLOCK_REALTIME))
            except OSError:
                pass
        print("  (monotonic clock unavailable; using wall clock)")

    csv_path = None if args.no_csv else (args.csv or time.strftime("chatter-%Y%m%d-%H%M%S.csv"))
    out = open(csv_path, "w", newline="") if csv_path else None
    w = csv.writer(out) if out else None
    if w:
        w.writerow(["t_s", "device", "code", "key", "hid_usage", "value(1=down,0=up,2=repeat)",
                    "hold_ms", "gap_ms", "flag"])
        if os.environ.get("SUDO_UID"):
            os.chown(csv_path, int(os.environ["SUDO_UID"]), int(os.environ.get("SUDO_GID", "-1")))

    tty = sys.stdout.isatty()
    RED, YEL, DIM, RST = ("\033[1;31m", "\033[33m", "\033[2m", "\033[0m") if tty else ("",) * 4
    print(f"\ndouble = same key re-pressed < {args.gap_ms:g} ms after its release; "
          f"short = held < {args.short_ms:g} ms")
    print("Type on the mechanical switches (normal typing, or one key at a time). Ctrl+C for the report.\n")

    an, scan = Analyzer(args.gap_ms), {}
    t0 = time.clock_gettime(clock)
    end = time.monotonic() + args.duration if args.duration else None
    restore = quiet_tty()
    try:
        while fds and (end is None or time.monotonic() < end):
            ready, _, _ = select.select(list(fds), [], [], 0.25)
            for fd in ready:
                try:
                    data = os.read(fd, EVENT.size * 64)
                except OSError:
                    print(f"{fds.pop(fd)} disconnected")
                    continue
                dev = fds[fd]
                for off in range(0, len(data) - EVENT.size + 1, EVENT.size):
                    sec, usec, typ, code, val = EVENT.unpack_from(data, off)
                    t = sec + usec / 1e6 - t0
                    if typ == EV_MSC and code == MSC_SCAN:
                        scan[fd] = val
                        continue
                    if typ != EV_KEY:
                        continue
                    row = [f"{t:.6f}", dev, code, keyname(code), f"{scan.get(fd, 0):#x}", val, "", "", ""]
                    if val == 2:
                        an.repeats += 1
                    elif val == 1:
                        gap = an.press(t, dev, code)
                        row[7] = f"{gap:.3f}" if gap is not None else ""
                    else:
                        s = an.release(t, dev, code)
                        if s:
                            _, _, key, hold, gap, dbl = s
                            row[6] = f"{hold:.3f}"
                            row[7] = f"{gap:.3f}" if gap is not None else ""
                            g = f"{gap:9.2f} ms" if gap is not None else "        - ms"
                            if dbl:
                                c, row[8] = RED, "DOUBLE"
                                prev = an.doubles[-1][3]
                                tag = (f"  <<< DOUBLE: re-pressed {gap:.2f} ms after release"
                                       + (f" (previous press held {prev:.2f} ms)" if prev is not None else ""))
                            elif hold < args.short_ms:
                                c, row[8], tag = YEL, "SHORT", "  short press"
                            else:
                                c, tag = DIM, ""
                            print(f"{c}[{t:9.3f}s] {dev:<9} {keyname(key):<7} hold {hold:8.2f} ms   gap {g}{tag}{RST}")
                    if w:
                        w.writerow(row)
    except KeyboardInterrupt:
        pass
    finally:
        restore()
        if out:
            out.close()
    report(an, time.clock_gettime(clock) - t0, csv_path)


def analyze(paths, gap_ms):
    """Re-run the report (and filter replay) on logs from earlier runs."""
    for path in paths:
        an, last = Analyzer(gap_ms), 0.0
        with open(path, newline="") as f:
            for row in csv.DictReader(f):
                t, dev, code = float(row["t_s"]), row["device"], int(row["code"])
                val = int(row["value(1=down,0=up,2=repeat)"])
                if val == 2:
                    an.repeats += 1
                elif val == 1:
                    an.press(t, dev, code)
                else:
                    an.release(t, dev, code)
                last = t
        print(f"\n##### {path}")
        report(an, last, None)


def selftest():
    a, A, B = Analyzer(gap_ms=30), 30, 48
    a.press(0.000, "d", A); a.release(0.080, "d", A)   # real press, held 80 ms
    a.press(0.083, "d", A); a.release(0.086, "d", A)   # phantom: 3 ms gap, 3 ms hold
    a.press(1.000, "d", B); a.release(1.100, "d", B)
    a.press(1.300, "d", B); a.release(1.400, "d", B)   # genuine double-tap: 200 ms gap
    a.press(2.000, "x", B); a.press(2.0005, "y", B)    # one key on two interfaces
    assert len(a.doubles) == 1, a.doubles
    _, _, key, hb, gap, ha = a.doubles[0]
    assert (key, round(hb), round(gap), round(ha)) == (A, 80, 3, 3), a.doubles[0]
    assert len(a.dups) == 1 and a.lost_release == 0
    assert keyname(30) == "A" and keyname(58) == "CAPS" and keyname(43) == "\\"

    flick = [(0, 1), (0.5, 0), (0.8, 1), (80, 0)]                     # key D: flicker at actuation
    assert eager(flick, 5) == [(0, 1), (80, 0)]
    assert defer(flick, 2) == [(2.8, 1), (82, 0)]
    assert score(intervals(flick), real_presses(flick))[:2] == (1, 0)          # stock: one phantom
    burst = [(0, 1), (0.4, 0), (25, 1), (26.5, 0), (50, 1), (1400, 0)]  # key G: slow-press burst
    assert score(intervals(eager(burst, 5)), real_presses(burst))[:2] == (2, 0)  # lockout can't
    assert score(intervals(defer(burst, 2)), real_presses(burst)) == (0, 0, 2)   # stability can
    taps = [(0, 1), (85, 0), (105, 1), (190, 0)]                      # fast real taps, 20 ms apart
    assert score(intervals(eager(taps, 30)), real_presses(taps)) == (0, 0, 10)   # kept, 10 ms late
    assert score(intervals(eager(taps, 5)), real_presses(taps)) == (0, 0, 0)
    drop = [(0, 1), (40, 0), (40.6, 1), (90, 0)]                      # harsh tap: dropout mid-hold
    assert score(intervals(eager(drop, 5)), real_presses(drop))[:2] == (1, 0)  # lockout over by then
    assert asym(drop, 8) == [(0, 1), (98, 0)]                         # release waits, dropout gone
    assert score(intervals(asym(taps, 8)), real_presses(taps)) == (0, 0, 0)    # fast taps kept
    print("selftest ok")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--vendor", default="3151", help="USB vendor id (default 3151 = MonsGeek)")
    ap.add_argument("--gap-ms", type=float, default=30,
                    help="re-press within this many ms of release counts as a double (default 30)")
    ap.add_argument("--short-ms", type=float, default=15, help="highlight presses held under this (default 15)")
    ap.add_argument("--duration", type=float, help="stop after N seconds (default: until Ctrl+C)")
    ap.add_argument("--csv", help="event log path (default chatter-<time>.csv)")
    ap.add_argument("--no-csv", action="store_true", help="don't write the event log")
    ap.add_argument("--analyze", nargs="+", metavar="CSV", help="report on logs from earlier runs")
    ap.add_argument("--selftest", action="store_true", help="check the analysis logic and exit")
    a = ap.parse_args()
    selftest() if a.selftest else analyze(a.analyze, a.gap_ms) if a.analyze else run(a)
