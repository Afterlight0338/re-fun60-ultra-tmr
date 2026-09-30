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
 *   0xF0 GET_RAW  [page]    -> [1] page, [2] sites, [3..62] 30 x u16 LE raw ADC
 *   0xF1 GET_STATE          -> [1] sites, [2] Fn held, [8..] pressed bitmap (site s = bit s)
 *   0xF2 GET_MAP  [layer][page]        -> [1] layer, [2] page, [3] n, [8..] n keycodes
 *   0xF3 SET_MAP  [layer][page][n], [8..] n keycodes (pages of 56 sites)
 *   0xF4 GET_CFG            -> [8..] on, effect, level, r, g, b, hold-off ms,
 *                              press_below LE16, release_above LE16
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
 */
#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>

#define PROTO_VERSION      4u
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
#define PROTO_NO_MODE      0xFFu
#define RAW_PER_PAGE       30u
#define MAP_PER_PAGE       56u

typedef struct {
  const volatile uint16_t *raw;       /* live scan frame (SCAN_SITES); NULL reads as 0xFFFF */
  const volatile uint8_t *pressed;    /* debounced key state (SCAN_SITES); NULL = none */
  int (*save)(void);                  /* write settings to flash, 0 = ok; NULL = can't */
  volatile uint8_t enter_bootloader;  /* set by 0x7F 55 AA 55 AA; main loop acts on it */
  volatile uint8_t host_seen;         /* any valid request: a host can reach us (main.c: proven) */
  volatile uint8_t set_mode, pair;    /* SET_RADIO: mode to switch to (PROTO_NO_MODE = none), pair */
} proto_state_t;

/* Handle one request in place: report holds the request, then the response. */
void proto_dispatch(proto_state_t *st, uint8_t *report);

#endif /* PROTO_H */
