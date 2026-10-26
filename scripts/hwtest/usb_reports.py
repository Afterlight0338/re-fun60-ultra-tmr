#!/usr/bin/env python3
"""usb_reports.py - check what the keyboard really sends over USB (firmware v0.16 or newer), with no
one at the keyboard: keys are pressed from the PC (DEBUG_PRESS) and the reports read from hidraw.
  nix-shell -p python3 --run 'python3 scripts/hwtest/usb_reports.py'
Only harmless codes reach the desktop: 0x02 / 0x03 (Linux maps them to nothing) and F19 / F24
(no keysym on a US layout). Everything is changed in RAM only and put back at the end; nothing is
saved. The consumer check sends Volume Up then Volume Down."""
import os, select, time
from fun60 import *

A, S, D, F = 1 * 8 + 2, 2 * 8 + 2, 3 * 8 + 2, 4 * 8 + 2
CODES = {A: 0x02, S: 0x03, D: 0x6E, F: 0x73}
fails = 0

def check(ok, msg):
    global fails
    print(("  ok    " if ok else "  FAIL  ") + msg); fails += not ok

def drain(fd, secs=0.25):
    out, end = [], time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.02)
        if r: out.append(bytes(os.read(fd, 64)))
    return out

def press(fd, site, on): req(fd, OP["DEBUG_PRESS"], [site, on])
def tap(fd, site): press(fd, site, 1); time.sleep(0.05); press(fd, site, 0)
def nk(rep, c): return rep[0] == 2 and rep[2 + (c >> 3)] >> (c & 7) & 1

fd = open_kb(); i = info(fd); print(i)
assert i["proto"] >= 9, "needs firmware v0.16 or newer"
k0, k2 = os.open(hidraw_for(0), os.O_RDONLY), os.open(hidraw_for(2), os.O_RDONLY)
L = keymap(fd); saved = [bytearray(L[0]), bytearray(L[1])]; c0 = cfg(fd)
try:
    for s, c in CODES.items(): L[0][s] = c
    set_keymap(fd, L); drain(k0); drain(k2)
    c = bytearray(c0); c[12] = 1; set_cfg(fd, c)                          # NKRO on

    for s in CODES: press(fd, s, 1)
    time.sleep(0.15); r2 = drain(k2); r0 = drain(k0)
    check(r2 and all(nk(r2[-1], c) for c in CODES.values()) and not r0, "NKRO: 4 keys on interface 2, nothing on the boot keyboard")
    for s in CODES: press(fd, s, 0)
    time.sleep(0.15); r2 = drain(k2)
    check(r2 and not any(r2[-1][1:]), "NKRO: all released")

    c[12] = 0; set_cfg(fd, c); drain(k2)                                    # NKRO off: 6KRO
    press(fd, A, 1); time.sleep(0.15); r0 = drain(k0)
    check(r0 and r0[-1][2] == 0x02, "6KRO on interface 0 with NKRO off")
    press(fd, A, 0); time.sleep(0.1); drain(k0); drain(k2)
    c[12] = 1; set_cfg(fd, c)

    req(fd, OP["SET_ADV"], [0, 1], bytes([1, A, S, 0] + [0] * 8))          # SOCD A/S, last wins
    press(fd, A, 1); time.sleep(0.05); press(fd, S, 1); time.sleep(0.15); r2 = drain(k2)
    check(nk(r2[-1], 0x03) and not nk(r2[-1], 0x02), "SOCD: S pressed after A wins")
    press(fd, S, 0); time.sleep(0.15); r2 = drain(k2)
    check(nk(r2[-1], 0x02), "SOCD: letting S go brings A back")
    press(fd, A, 0); time.sleep(0.1); drain(k2)

    steps = [(0x02, 3), (0, 10), (0x6E, 1), (0x6E, 2)]                       # tap, wait 100 ms, F19 down, up
    req(fd, OP["SET_MACRO"], [7, 0, len(steps)], bytes(b for s in steps for b in s))
    req(fd, OP["SET_ADV"], [0, 1], bytes([5, A, 0, 7] + [0] * 8))
    tap(fd, A); time.sleep(0.5); r2 = drain(k2)
    seen = [x for x in r2 if any(x[1:])]
    check(len(seen) == 2 and nk(seen[0], 0x02) and nk(seen[1], 0x6E) and not any(r2[-1][1:]), "macro: tap, wait, F19 down and up, nothing left down")

    req(fd, OP["SET_ADV"], [0, 1], bytes(12)); req(fd, OP["SET_MACRO"], [7, 0, len(steps)], bytes(2 * len(steps)))
    for code, usage in ((0xA7, 0xE9), (0xA6, 0xEA)):                        # Volume Up, then Down
        L[0][A] = code; set_keymap(fd, L); drain(k2)
        tap(fd, A); time.sleep(0.15); r2 = drain(k2)
        check(r2 and r2[0][:3] == bytes([1, usage, 0]) and r2[-1][:3] == bytes([1, 0, 0]), f"consumer report ID 1: usage {usage:#x} then released")
finally:
    for s in CODES: press(fd, s, 0)
    req(fd, OP["SET_ADV"], [0, 1], bytes(12)); set_keymap(fd, saved); set_cfg(fd, c0)
print(f"{'ALL OK' if not fails else f'{fails} FAILED'}, pressed now: {state(fd) or 'none'}")
