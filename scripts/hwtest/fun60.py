"""fun60.py - the open firmware's vendor protocol over hidraw, for the scripts in this folder.
Same framing as driver/app.html and rawscan.py (fw/src/proto.h has every opcode)."""
import fcntl, glob, os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import rawscan

SITES, MAP_PAGE = 112, 56
OP = dict(INFO=0x8F, RAW=0xF0, STATE=0xF1, GET_MAP=0xF2, SET_MAP=0xF3, GET_CFG=0xF4, SET_CFG=0xF5,
          SAVE=0xF6, GET_KEYCFG=0xFB, GET_ADV=0xE1, SET_ADV=0xE2, GET_LEDMAP=0xE3, GET_MACRO=0xE5,
          SET_MACRO=0xE6, DEBUG_PRESS=0xEF)


def open_kb(wait=15):
    end = time.time() + wait
    while time.time() < end:
        p = rawscan.find_vendor_hidraw()
        if p:
            try: return os.open(p, os.O_RDWR)
            except OSError: pass
        time.sleep(0.3)
    raise SystemExit("no keyboard with the open firmware found")


def hidraw_for(itf):
    """hidraw node of the keyboard's USB interface itf (0 boot keyboard, 1 vendor, 2 consumer + NKRO)"""
    for ev in glob.glob("/sys/class/hidraw/hidraw*/device/uevent"):
        if "00003151:00005030" in open(ev).read() and os.path.realpath(ev).split("/")[-3].endswith(f":1.{itf}"):
            return "/dev/" + ev.split("/")[4]


def req(fd, op, args=(), payload=b""):
    r = bytearray(64); r[0] = op; r[1:1 + len(args)] = bytes(args)
    r[7] = (0xFF - sum(r[0:7])) & 0xFF
    r[8:8 + len(payload)] = payload
    buf = bytearray(65); buf[1:] = r
    fcntl.ioctl(fd, rawscan.HIDIOCSFEATURE, buf)
    g = bytearray(65); fcntl.ioctl(fd, rawscan.HIDIOCGFEATURE, g)
    g = g[1:] if g[0] == 0 and g[1] == op else g
    assert g[0] == op, f"no reply to {op:#x}: {bytes(g[:4]).hex()}"
    return g


def info(fd):
    u = req(fd, OP["INFO"]); return dict(dev=u[1] | u[2] << 8, proto=u[3], ver=f"{u[8]}.{u[7]:02d}")

def state(fd):
    u = req(fd, OP["STATE"]); return {s for s in range(SITES) if u[8 + (s >> 3)] & (1 << (s & 7))}

def keymap(fd):
    L = [bytearray(SITES), bytearray(SITES)]
    for l in range(2):
        for p in range(2):
            u = req(fd, OP["GET_MAP"], [l, p]); n = min(MAP_PAGE, SITES - p * MAP_PAGE)
            L[l][p * MAP_PAGE:p * MAP_PAGE + n] = u[8:8 + n]
    return L

def set_keymap(fd, L):
    for l in range(2):
        for p in range(2):
            c = bytes(L[l][p * MAP_PAGE:(p + 1) * MAP_PAGE]); req(fd, OP["SET_MAP"], [l, p, len(c)], c)

def cfg(fd):
    return bytearray(req(fd, OP["GET_CFG"])[8:24])

def set_cfg(fd, c):
    return bytearray(req(fd, OP["SET_CFG"], [], bytes(c))[8:24])
