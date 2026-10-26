#!/usr/bin/env python3
"""backup.py - dump the keyboard's settings to a JSON file before you flash anything.
  nix-shell -p python3 --run 'python3 scripts/hwtest/backup.py [dir]'
Reads only. FUN60 Studio's Settings page can import the keymap part; the rest is for reference."""
import json, sys, time
from fun60 import *

fd = open_kb(); i = info(fd); L = keymap(fd)
d = {"info": i, "base": list(L[0]), "fn": list(L[1]), "cfg": list(cfg(fd))}
if i["proto"] >= 6:
    d["keycfg"] = [[b for p in range(2) for b in (lambda u: u[8:8 + u[3]])(req(fd, OP["GET_KEYCFG"], [f, p]))] for f in range(3)]
if i["proto"] >= 7:
    d["adv"] = [list(u[8 + 12 * k:20 + 12 * k]) for first in range(0, 16, 4) for u in [req(fd, OP["GET_ADV"], [first])] for k in range(u[2])]
if i["proto"] >= 8:
    d["ledmap"] = [b for p in range(4) for b in (lambda u: list(u[8:8 + 3 * u[2]]))(req(fd, OP["GET_LEDMAP"], [p]))]
if i["proto"] >= 9:
    d["macros"] = [[b for p in range(2) for b in (lambda u: list(u[8:8 + 2 * u[3]]))(req(fd, OP["GET_MACRO"], [m, p]))] for m in range(8)]
out = f"{sys.argv[1] if len(sys.argv) > 1 else '.'}/fun60-backup-{time.strftime('%Y%m%d-%H%M%S')}-v{i['ver']}.json"
json.dump(d, open(out, "w")); print("backup ->", out)
