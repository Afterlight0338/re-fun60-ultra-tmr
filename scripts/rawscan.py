#!/usr/bin/env python3
"""
rawscan.py - live raw sensor view for the open firmware (GET_RAW, opcode 0xF0).

Shows every scan site's ADC reading as a 14 column x 8 mux channel grid (site = col * 8 + ch,
fw/src/board.h) and logs each site that moves away from its resting value, so pressing one
key at a time tells you which site it is. Row r7 is the battery divider. Open firmware only;
stock has no 0xF0.

  nix-shell -p python3 --run 'sudo "$(command -v python3)" scripts/rawscan.py'
      --delta 150    counts a site must move to be reported (default 150)
      --once         print one frame and exit
      --power        print battery / USB / charging as the firmware sees it (v0.06+) and exit
      --radio        print the wireless link state and the last radio replies (v0.07+) and exit
      --keys         show the Fn-layer keymap in RAM, then live: keys held, Fn, radio mode and frames
      --mode 24g     switch to 2.4G (bt1 bt2 bt3 usb too) and watch the radio; --pair pairs; back to
                     USB afterwards (or on Ctrl+C) unless --keep  (v0.11+)
"""
import argparse, fcntl, glob, os, sys, time

HIDIOCSFEATURE = 0xC0414806          # _IOC(RW, 'H', 0x06, 65)
HIDIOCGFEATURE = 0xC0414807          # _IOC(RW, 'H', 0x07, 65)
FEA_GET_RAW, FEA_GET_POWER, FEA_GET_RADIO, FEA_SET_RADIO, PER_PAGE, COLS = 0xF0, 0xF8, 0xF9, 0xFA, 30, 14
MODES = {0: "Bluetooth 1", 1: "Bluetooth 2", 2: "Bluetooth 3", 5: "2.4G", 6: "USB"}
ROWS = 5                             # set from the site count the board reports (sites / COLS)


def find_vendor_hidraw():
    """The keyboard's vendor interface: usage page 0xFFFF, usage 2 (stock: IF2, ours: IF1)."""
    for ev in glob.glob("/sys/class/hidraw/hidraw*/device/uevent"):
        if "00003151:00005030" not in open(ev).read():
            continue
        if open(ev.replace("uevent", "report_descriptor"), "rb").read(5) == b"\x06\xff\xff\x09\x02":
            return "/dev/" + ev.split("/")[4]
    return None


def request(fd, payload):
    r = bytearray(65)                 # [report id 0] + 64-byte report
    r[1:1 + len(payload)] = bytes(payload)
    r[8] = (0xFF - sum(r[1:8])) & 0xFF   # Bit7 header checksum: bytes 0..7 sum to 0xFF
    for _ in range(10):               # the keyboard has one reply buffer: another program's
        fcntl.ioctl(fd, HIDIOCSFEATURE, r)   # request (e.g. the driver page) can land in between
        g = bytearray(65)
        fcntl.ioctl(fd, HIDIOCGFEATURE, g)
        g = g[1:] if g[0] == 0 and g[1] == payload[0] else g   # tolerate a leading report id
        if g[0] == payload[0] and (len(payload) < 2 or g[1] == payload[1]):
            break
    return g


def frame(fd):
    vals, n, page = [], None, 0
    while n is None or len(vals) < n:               # pages of 30 until the reported site count
        r = request(fd, [FEA_GET_RAW, page])
        if r[0] != FEA_GET_RAW or r[1] != page:
            sys.exit(f"no GET_RAW reply (got {bytes(r[:4]).hex()}): is the custom firmware running?")
        n = r[2]
        vals += [r[3 + 2 * i] | r[4 + 2 * i] << 8 for i in range(PER_PAGE)]
        page += 1
    global ROWS
    ROWS = n // COLS
    return vals[:n]


def grid(vals, base, delta):
    cols = len(vals) // ROWS
    out = ["      " + "".join(f"  c{c:<3}" for c in range(cols))]
    for row in range(ROWS):
        cells = []
        for c in range(cols):
            v, b = vals[c * ROWS + row], base[c * ROWS + row]
            mark = "\033[1;33m" if abs(v - b) >= delta else ""
            cells.append(f"{mark}{v:6d}\033[0m" if mark else f"{v:6d}")
        out.append(f"  r{row}  " + "".join(cells))
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--delta", type=int, default=150, help="ADC counts a site must move to be reported")
    ap.add_argument("--once", action="store_true", help="print one frame and exit")
    ap.add_argument("--power", action="store_true", help="print the firmware's battery state and exit")
    ap.add_argument("--radio", action="store_true", help="print the wireless link state and exit")
    ap.add_argument("--keys", action="store_true", help="show the keymap in RAM and what is held, live (--seconds, default 240)")
    ap.add_argument("--watch", type=float, metavar="S",
                    help="record S seconds, then report which sites moved (no live display)")
    ap.add_argument("--record", metavar="CSV", help="log every frame (seconds, all sites) to CSV")
    ap.add_argument("--seconds", type=float, help="--record / --keys length (default 240), --mode length (default 45)")
    ap.add_argument("--mode", choices=["bt1", "bt2", "bt3", "24g", "usb"],
                    help="switch the connection mode (as Fn + E R T Y U), watch the radio, then back to USB")
    ap.add_argument("--pair", action="store_true", help="with --mode: also start pairing (as holding the key 3 s)")
    ap.add_argument("--keep", action="store_true", help="with --mode: stay in that mode afterwards")
    a = ap.parse_args()
    if a.seconds is None:
        a.seconds = 45 if a.mode else 240   # in a wireless mode this keyboard types only over the air
    dev = find_vendor_hidraw()
    if not dev:
        sys.exit("keyboard vendor interface (3151:5030 input2) not found")
    try:
        fd = os.open(dev, os.O_RDWR)
    except PermissionError:
        sys.exit(f"no permission for {dev}; run with sudo (see --help)")
    if a.mode:
        code = {"bt1": 0, "bt2": 1, "bt3": 2, "24g": 5, "usb": 6}[a.mode]
        r = request(fd, [FEA_SET_RADIO, code, 1 if a.pair else 0])
        if r[0] != FEA_SET_RADIO:
            sys.exit("no SET_RADIO reply: needs open firmware v0.11 or newer")
        print(f"asked for {MODES[code]}{' + pairing' if a.pair else ''}; watching {a.seconds:g} s (Ctrl+C to stop)")
        last, t0 = None, time.monotonic()
        try:
            while time.monotonic() - t0 < a.seconds:
                g = request(fd, [FEA_GET_RADIO])
                now = (g[1], g[2], g[3], g[5], g[4])
                if now != last:
                    print(f"  {time.monotonic() - t0:6.2f} s  mode {MODES.get(g[1], g[1])}; radio: state {g[2]}, "
                          f"mode {MODES.get(g[3], g[3])}, status seen {g[5]}, host LEDs {g[4]:03b}; "
                          f"sent {g[8] | g[9] << 8}, good {g[10] | g[11] << 8}, bad {g[12] | g[13] << 8}; "
                          f"last reply {bytes(g[16:24]).hex(' ')}")
                    last = now
                time.sleep(0.05)
        except KeyboardInterrupt:
            pass
        if not a.keep and code != 6:
            request(fd, [FEA_SET_RADIO, 6, 0])
            print("back to USB")
        return
    if a.keys:
        def layer(l):
            codes = []
            for page in range(2):
                r = request(fd, [0xF2, l, page])
                codes += list(r[8:8 + r[3]])
            return codes
        base, fn = layer(0), layer(1)
        names = {8: "1", 20: "Win", 25: "E", 33: "R", 41: "T", 49: "Y", 54: "Space", 57: "U", 84: "Fn"}
        print("keymap in the keyboard's RAM (base / Fn layer):")
        print("  " + ", ".join(f"{n} {base[s]:02x}/{fn[s]:02x}" for s, n in names.items()))
        print(f"expected: Fn base 01, Fn layer E b8 R b9 T ba Y bb U bc Space bd Win be. Watching {a.seconds:g} s: "
              "hold Fn, press Y, E ... (Ctrl+C to stop)")
        last, t0 = None, time.monotonic()
        try:
            while time.monotonic() - t0 < a.seconds:
                s = request(fd, [0xF1])
                held = [i for i in range(s[1]) if s[8 + i // 8] >> (i % 8) & 1]
                g = request(fd, [FEA_GET_RADIO])
                now = (tuple(held), s[2], g[1], g[8] | g[9] << 8)
                if now != last:
                    print(f"  {time.monotonic() - t0:6.2f} s  held {[names.get(i, i) for i in held]}  Fn {s[2]}  "
                          f"mode {MODES.get(g[1], g[1])}  frames sent {now[3]}")
                    last = now
                time.sleep(0.02)
        except KeyboardInterrupt:
            pass
        return
    if a.radio:
        r = request(fd, [FEA_GET_RADIO])
        if r[0] != FEA_GET_RADIO:
            sys.exit("no GET_RADIO reply: needs open firmware v0.07 or newer")
        u16 = lambda i: r[i] | r[i + 1] << 8
        print(f"mode {MODES.get(r[1], r[1])}; radio says: state {r[2]}, mode {MODES.get(r[3], r[3])}, "
              f"host LEDs {r[4]:03b}, status seen {'yes' if r[5] else 'no'}")
        print(f"frames sent {u16(8)}, good replies {u16(10)}, bad replies {u16(12)}")
        for i in range(3):
            print(f"  reply -{i}: {bytes(r[16 + 16 * i:32 + 16 * i]).hex(' ')}")
        return
    if a.power:
        r = request(fd, [FEA_GET_POWER])
        if r[0] != FEA_GET_POWER:
            sys.exit("no GET_POWER reply: needs open firmware v0.06 or newer")
        pct = f"{r[1]} %" if r[1] else "no battery"
        print(f"battery {pct} (raw {r[3] | r[4] << 8}), USB power {'yes' if r[2] & 1 else 'no'}, "
              f"charging {'yes' if r[2] & 2 else 'no'}, switch {'top (Mac)' if r[2] & 4 else 'middle or bottom'} (PB1)")
        return
    base = frame(fd)
    if a.record:
        t0, n = time.monotonic(), 0
        with open(a.record, "w") as f:
            f.write("t_s," + ",".join(f"s{i}" for i in range(len(base))) + "\n")
            while (t := time.monotonic() - t0) < a.seconds:
                f.write(f"{t:.4f}," + ",".join(map(str, frame(fd))) + "\n")
                n += 1
        print(f"{n} frames in {a.seconds:g} s ({n / a.seconds:.0f}/s) -> {a.record}")
        return
    if a.once:
        print(grid(base, base, 1 << 16))
        return
    if a.watch:
        lo, hi, n, end = list(base), list(base), 0, time.time() + a.watch
        while time.time() < end:
            v = frame(fd)
            lo = [min(x, y) for x, y in zip(lo, v)]
            hi = [max(x, y) for x, y in zip(hi, v)]
            n += 1
        print(f"{n} frames in {a.watch:g} s; resting values:")
        print(grid(base, base, 1 << 16))
        print(f"all sites: min {min(lo)}, max {max(hi)}, distinct resting values {len(set(base))}")
        for i in sorted(range(len(base)), key=lambda i: lo[i] - hi[i])[:12]:
            print(f"site {i:2d} (c{i // ROWS}, r{i % ROWS}): rest {base[i]:5d}  min {lo[i]:5d}  max {hi[i]:5d}  "
                  f"range {hi[i] - lo[i]}")
        return
    active = set()
    print("\033[2J", end="")
    try:
        while True:
            vals = frame(fd)
            moved = {i for i, (v, b) in enumerate(zip(vals, base)) if abs(v - b) >= a.delta}
            for i in sorted(moved - active):
                log = f"site {i:2d} (c{i // ROWS}, r{i % ROWS}): {base[i]} -> {vals[i]} ({vals[i] - base[i]:+d})"
                print(f"\033[{ROWS + 4};1H\033[2K{log}")
            active = moved
            print("\033[H" + grid(vals, base, a.delta), end="", flush=True)
            time.sleep(0.01)
    except KeyboardInterrupt:
        print(f"\033[{ROWS + 6};1H")


if __name__ == "__main__":
    main()
