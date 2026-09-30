#!/usr/bin/env python3
"""
analyze_session.py - make sense of a `rawscan.py --record` capture.

  python3 scripts/analyze_session.py CAPTURE.csv [--threshold 100]

1. Resting level per site = median of the first 3 s (hands off).
2. A site is "pressed" while it sits more than --threshold counts from rest.
3. Prints every press (start, site, depth, duration), then the order in which sites were
   first pressed: with keys pressed one at a time in layout order, that order is the key map.
"""
import argparse, csv, statistics as S

ap = argparse.ArgumentParser()
ap.add_argument("csv")
ap.add_argument("--threshold", type=int, default=100)
ap.add_argument("--cols", type=int, default=14)
a = ap.parse_args()

t, frames = [], []
for row in csv.reader(open(a.csv)):
    if row[0] == "t_s" or len(row) < 2 or "" in row:
        continue
    try:
        t.append(float(row[0])); frames.append([int(x) for x in row[1:]])
    except ValueError:
        break                                   # half-written last line
n = len(frames[0]); rows = n // a.cols
rest = [S.median(f[s] for f, tt in zip(frames, t) if tt < 3.0) for s in range(n)]
live = [s for s in range(n) if rest[s] > 500]

# press intervals per site
events = []                                     # (start, end, site, peak signed deviation)
for s in live:
    start, peak = None, 0
    for tt, f in zip(t, frames):
        d = f[s] - rest[s]
        if abs(d) > a.threshold:
            if start is None: start, peak = tt, d
            elif abs(d) > abs(peak): peak = d
        elif start is not None:
            events.append((start, tt, s, peak)); start = None
events.sort()
# a whole column flickering in one frame is a scan glitch, not keys: drop events that start
# within one frame of 2+ others
frame_s = t[-1] / len(t)
events = [e for e in events if sum(abs(o[0] - e[0]) < 1.5 * frame_s for o in events) < 3]

print(f"{len(frames)} frames over {t[-1]:.1f} s ({len(frames) / t[-1]:.0f}/s), {len(live)} live sites, "
      f"threshold {a.threshold}")
print(f"{len(events)} press intervals; sign of deviation when pressed: "
      f"{sum(e[3] < 0 for e in events)} down, {sum(e[3] > 0 for e in events)} up")
for st, en, s, pk in events:
    print(f"  {st:8.3f}s  site {s:2d} (c{s // rows:2d} r{s % rows})  rest {rest[s]:6.0f}  peak {pk:+6.0f}  "
          f"held {1000 * (en - st):7.1f} ms")

order = []
for st, en, s, pk in events:
    if s not in order:
        order.append(s)
print("\nfirst-press order:", " ".join(str(s) for s in order))
