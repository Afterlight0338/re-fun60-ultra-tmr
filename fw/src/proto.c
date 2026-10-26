/*
 * proto.c - vendor feature-report protocol. See proto.h.
 */
#include "proto.h"
#include "board.h"
#include "keys.h"
#include "settings.h"
#include "power.h"
#include "radio.h"

static int checksum_ok(const uint8_t *r)
{
  uint8_t s = 0;
  for (unsigned i = 0; i < 8; i++) s = (uint8_t)(s + r[i]);
  return s == 0xFF;
}

void proto_dispatch(proto_state_t *st, uint8_t *r)
{
  uint8_t q[PROTO_REPORT_LEN];                    /* the request; r becomes the reply */
  for (unsigned i = 0; i < PROTO_REPORT_LEN; i++) { q[i] = r[i]; r[i] = 0; }
  if (!checksum_ok(q)) return;
  st->host_seen = 1;
  r[0] = q[0];

  switch (q[0]) {
  case OP_GET_INFOR:
    r[1] = (uint8_t)FW_DEV_ID;
    r[2] = (uint8_t)(FW_DEV_ID >> 8);
    r[3] = PROTO_VERSION;
    r[7] = FW_VER_MINOR;
    r[8] = FW_VER_MAJOR;
    break;

  case OP_ENTER_BOOT:
    if (q[1] == 0x55 && q[2] == 0xAA && q[3] == 0x55 && q[4] == 0xAA) st->enter_bootloader = 1;
    break;

  case OP_GET_RAW:
    r[1] = q[1];
    r[2] = (uint8_t)SCAN_SITES;
    for (unsigned i = 0; i < RAW_PER_PAGE; i++) {
      unsigned site = q[1] * RAW_PER_PAGE + i;
      uint16_t v = (st->raw && site < SCAN_SITES) ? st->raw[site] : 0xFFFFu;
      r[3 + 2 * i] = (uint8_t)v;
      r[4 + 2 * i] = (uint8_t)(v >> 8);
    }
    break;

  case OP_GET_STATE:
    r[1] = (uint8_t)SCAN_SITES;
    for (unsigned s = 0; st->pressed && s < SCAN_SITES; s++) {
      if (!st->pressed[s]) continue;
      r[8 + s / 8] |= (uint8_t)(1u << (s % 8));
      if (settings.keymap[0][s] == KC_FN) r[2] = 1;
    }
    break;

  case OP_GET_MAP:
  case OP_SET_MAP: {
    unsigned layer = q[1], first = q[2] * MAP_PER_PAGE, n = 0;
    if (layer < 2 && first < SCAN_SITES) {
      n = SCAN_SITES - first < MAP_PER_PAGE ? SCAN_SITES - first : MAP_PER_PAGE;
      if (q[0] == OP_SET_MAP && q[3] < n) n = q[3];
      for (unsigned i = 0; i < n; i++) {
        if (q[0] == OP_SET_MAP) settings.keymap[layer][first + i] = q[8 + i];
        r[8 + i] = settings.keymap[layer][first + i];
      }
    }
    r[1] = q[1]; r[2] = q[2]; r[3] = (uint8_t)n;
    break;
  }

  case OP_SET_CFG:
    settings.led.on = q[8]; settings.led.effect = q[9]; settings.led.level = q[10];
    settings.led.r = q[11]; settings.led.g = q[12]; settings.led.b = q[13];
    settings.debounce_ms = q[14];
    settings.press_pct = q[15];
    settings.release_pct = q[16];
    settings_sanitize(&settings);
    /* fall through - reply with what was actually stored */
  case OP_GET_CFG:
    r[8] = settings.led.on; r[9] = settings.led.effect; r[10] = settings.led.level;
    r[11] = settings.led.r; r[12] = settings.led.g; r[13] = settings.led.b;
    r[14] = settings.debounce_ms;
    r[15] = settings.press_pct; r[16] = settings.release_pct;
    break;

  case OP_SAVE:
    r[1] = st->save && st->save() == 0;
    break;

  case OP_DEFAULTS:
    settings_defaults(&settings);
    break;

  case OP_GET_POWER:
    r[1] = power.percent;
    r[2] = (uint8_t)(power.usb | power.charging << 1 | power.sw << 2);
    r[3] = (uint8_t)power.raw;
    r[4] = (uint8_t)(power.raw >> 8);
    break;

  case OP_SET_RADIO:
    if (q[1] <= RADIO_BT3 || q[1] == RADIO_24G || q[1] == RADIO_USB) {
      st->set_mode = q[1];
      st->pair = q[2] == 1;
    }
    /* fall through - reply with the state as it is now; the switch happens in the main loop */
  case OP_GET_RADIO: {
    const uint16_t n[3] = { radio.tx, radio.rx, radio.bad };
    r[1] = radio.mode; r[2] = radio.state; r[3] = radio.radio_mode;
    r[4] = radio.host_leds; r[5] = radio.status_seen;
    for (unsigned i = 0; i < 3; i++) { r[8 + 2 * i] = (uint8_t)n[i]; r[9 + 2 * i] = (uint8_t)(n[i] >> 8); }
    for (unsigned i = 0; i < 48; i++) r[16 + i] = radio.last[i / 16][i % 16];
    break;
  }

  default:                               /* includes OP_ISP_PREPARE: accepted, no-op */
    break;
  }
}
