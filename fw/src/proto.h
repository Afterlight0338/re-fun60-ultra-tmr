/*
 * proto.h - vendor feature-report protocol (64-byte reports on the vendor HID interface,
 * usage page 0xFFFF / usage 2). Requests carry the stock "Bit7" header checksum: bytes 0..7
 * sum to 0xFF. Opcode in byte 0, arguments in 1..6, payload from byte 8. The reply is read
 * back with GET_REPORT, opcode echoed in byte 0. A request that fails the checksum is answered
 * with all zeros. Pure logic, host-tested (test/test_host.c).
 *
 *   0x8F GET_INFOR          -> [1..2] dev_id LE, [3] protocol version, [7] ver minor, [8] major
 *   0xC5 ISP_PREPARE        -> accepted, nothing to do (ry-flash sends it before 0x7F)
 *   0x7F 55 AA 55 AA        -> reboot into the factory bootloader (3151:502A)
 *   0x7F 'R' 'E' 'S' 'T'    -> plain restart, settings kept (v6)
 *   0xF0 GET_RAW  [page]    -> [1] page, [2] sites, [3..62] 30 x u16 LE raw ADC
 *   0xF1 GET_STATE          -> [1] sites, [2] Fn held, [8..] pressed bitmap (site s = bit s)
 *   0xF2 GET_MAP  [layer][page]        -> [1] layer, [2] page, [3] n, [8..] n keycodes
 *   0xF3 SET_MAP  [layer][page][n], [8..] n keycodes (pages of 56 sites)
 *   0xF4 GET_CFG            -> [8..] on, effect, level, r, g, b, hold-off ms,
 *                              press %, release % of travel (v5; v4 and older sent
 *                              press_below LE16, release_above LE16 in ADC counts),
 *                              [17] top dead zone %, [18] bottom dead zone %,
 *                              [19] rapid trigger continuous                         (v6)
 *                              [20] NKRO on USB                                      (v7)
 *                              [21] effect speed 0-4 (effects 3-6 are new in v8)     (v8)
 *                              [22] active profile 0-3 (read only; PROFILE switches) (v10)
 *   0xF5 SET_CFG  [8..] same layout; values are range-checked
 *   0xF6 SAVE               -> [1] 1 if written to flash
 *   0xF7 DEFAULTS           -> settings back to defaults (in RAM; SAVE keeps them)
 *   0xF8 GET_POWER          -> [1] battery % (0 = none), [2] bit0 USB power,
 *                              bit1 charging,
 *                              bit2 PB1 low (switch at the top: Mac),
 *                              [3..4] battery raw ADC LE16                      (protocol v2)
 *   0xF9 GET_RADIO          -> [1] our mode, [2] radio state, [3] radio's mode, [4] host LEDs,
 *                              [5] status seen, [8..13] frames sent, good, bad (LE16 each),
 *                              [16..63] last 3 raw poll replies, 16 bytes each   (protocol v3)
 *   0xFA SET_RADIO [mode][pair] -> switch mode as the Fn keys do (0-2 Bluetooth, 5 2.4G, 6 USB),
 *                              pair = 1 also starts pairing; replies like GET_RADIO   (v4)
 *   0xFB GET_KEYCFG [field][page]      -> like GET_MAP, per-key values (v6):
 *                              field 0 actuation % (0 = global), 1 rapid trigger down %
 *                              (0 = off), 2 rapid trigger up % (0 = same as down)
 *   0xFC SET_KEYCFG [field][page][n], [8..] n values (range-checked, reply = stored)
 *   0xFD GET_TRAVEL [page]  -> [1] page, [2] sites, [8..] 14 sites x (rest LE16, bottom LE16,
 *                              as the firmware uses them), [3] bit0 = page has saved travel  (v6)
 *   0xFE TRAVEL [cmd]       -> 1: store each key's travel seen since power-on ([1] = keys
 *                              stored; SAVE writes it), 2: clear saved travel, 3: forget what
 *                              was learned since power-on                               (v6)
 *   0xE1 GET_ADV  [first]   -> [1] first, [2] n (up to 4), [8..] n advanced keys x 12 bytes:
 *                              type, a, b, p0, code[4], act[4] (keys.h)                  (v7)
 *   0xE2 SET_ADV  [first][n], [8..] n entries; range-checked, reply = stored
 *   0xE3 GET_LEDMAP [page]  -> [1] page, [2] n, [8..] n x r g b: Custom colours, 18 LEDs a page in
 *                              chain order (leds.h)                                     (v8)
 *   0xE4 SET_LEDMAP [page][n], [8..] n x r g b
 *   0xE5 GET_MACRO [m][page] -> [1] m, [2] page, [3] n, [8..] n steps x (code, arg), 16 a page
 *                              (keys.h); advanced key type 5 plays one                   (v9)
 *   0xE6 SET_MACRO [m][page][n], [8..] n steps
 *   0xE7 PROFILE [n]        -> switch to profile n (0-3), done by the main loop (it saves to
 *                              flash); [1] = profile before the switch                   (v10)
 *   0xEF DEBUG_PRESS [site][on] -> holds a key down as if pressed (on = 1) or lets it go;
 *                              for testing the reports without touching the keyboard   (v7)
 */
#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>
#include "keys.h"

#define PROTO_VERSION      10u
#define PROTO_REPORT_LEN   64u
#define OP_GET_INFOR       0x8Fu
#define OP_ISP_PREPARE     0xC5u
#define OP_ENTER_BOOT      0x7Fu
#define OP_GET_RAW         0xF0u
#define OP_GET_STATE       0xF1u
#define OP_GET_MAP         0xF2u
#define OP_SET_MAP         0xF3u
#define OP_GET_CFG         0xF4u
#define OP_SET_CFG         0xF5u
#define OP_SAVE            0xF6u
#define OP_DEFAULTS        0xF7u
#define OP_GET_POWER       0xF8u
#define OP_GET_RADIO       0xF9u
#define OP_SET_RADIO       0xFAu
#define OP_GET_KEYCFG      0xFBu
#define OP_SET_KEYCFG      0xFCu
#define OP_GET_TRAVEL      0xFDu
#define OP_TRAVEL          0xFEu
#define OP_GET_ADV         0xE1u
#define OP_SET_ADV         0xE2u
#define OP_GET_LEDMAP      0xE3u
#define OP_SET_LEDMAP      0xE4u
#define LEDMAP_PER_PAGE    18u
#define OP_GET_MACRO       0xE5u
#define OP_SET_MACRO       0xE6u
#define MACRO_PER_PAGE     16u
#define OP_PROFILE         0xE7u
#define OP_DEBUG_PRESS     0xEFu
#define ADV_PER_PAGE       4u
#define KEYCFG_FIELDS      3u
#define TRAVEL_PER_PAGE    14u
#define PROTO_NO_MODE      0xFFu
#define RAW_PER_PAGE       30u
#define MAP_PER_PAGE       56u

typedef struct {
  const volatile uint16_t *raw;       /* live scan frame (SCAN_SITES); NULL reads as 0xFFFF */
  const volatile uint8_t *pressed;    /* debounced key state (SCAN_SITES); NULL = none */
  keys_t *keys;                       /* travel learning (GET_TRAVEL / TRAVEL); NULL = none */
  int (*save)(void);                  /* write settings to flash, 0 = ok; NULL = can't */
  volatile uint8_t enter_bootloader;  /* set by 0x7F 55 AA 55 AA; main loop acts on it */
  volatile uint8_t restart;           /* set by 0x7F R E S T */
  volatile uint8_t host_seen;         /* any valid request: a host can reach us (main.c: proven) */
  volatile uint8_t set_mode, pair;    /* SET_RADIO: mode to switch to (PROTO_NO_MODE = none), pair */
  volatile uint8_t set_profile;       /* PROFILE: profile to switch to (PROTO_NO_MODE = none) */
} proto_state_t;

/* Handle one request in place: report holds the request, then the response. */
void proto_dispatch(proto_state_t *st, uint8_t *report);

#endif /* PROTO_H */
