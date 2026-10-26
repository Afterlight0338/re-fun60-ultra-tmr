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
    if (q[1] == 'R' && q[2] == 'E' && q[3] == 'S' && q[4] == 'T') st->restart = 1;
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
    settings.top_dz = q[17]; settings.bottom_dz = q[18]; settings.rt_continuous = q[19];
    settings.nkro = q[20];
    settings.led.speed = q[21];
    settings_sanitize(&settings);
    /* fall through - reply with what was actually stored */
  case OP_GET_CFG:
    r[8] = settings.led.on; r[9] = settings.led.effect; r[10] = settings.led.level;
    r[11] = settings.led.r; r[12] = settings.led.g; r[13] = settings.led.b;
    r[14] = settings.debounce_ms;
    r[15] = settings.press_pct; r[16] = settings.release_pct;
    r[17] = settings.top_dz; r[18] = settings.bottom_dz; r[19] = settings.rt_continuous;
    r[20] = settings.nkro;
    r[21] = settings.led.speed;
    break;

  case OP_GET_LEDMAP:
  case OP_SET_LEDMAP: {
    unsigned first = q[1] * LEDMAP_PER_PAGE, n = 0;
    if (first < LED_COUNT) {
      n = LED_COUNT - first < LEDMAP_PER_PAGE ? LED_COUNT - first : LEDMAP_PER_PAGE;
      if (q[0] == OP_SET_LEDMAP && q[2] < n) n = q[2];
      for (unsigned i = 0; i < n; i++)
        for (unsigned c = 0; c < 3; c++) {
          if (q[0] == OP_SET_LEDMAP) settings.led.custom[first + i][c] = q[8 + 3 * i + c];
          r[8 + 3 * i + c] = settings.led.custom[first + i][c];
        }
    }
    r[1] = q[1]; r[2] = (uint8_t)n;
    break;
  }

  case OP_GET_ADV:
  case OP_SET_ADV: {
    unsigned first = q[1], n = 0;
    if (first < ADV_MAX) {
      n = ADV_MAX - first < ADV_PER_PAGE ? ADV_MAX - first : ADV_PER_PAGE;
      if (q[0] == OP_SET_ADV) {
        if (q[2] < n) n = q[2];
        for (unsigned i = 0; i < n; i++) {
          const uint8_t *e = q + 8 + 12 * i;
          adv_t *a = &settings.adv[first + i];
          a->type = e[0]; a->a = e[1]; a->b = e[2]; a->p0 = e[3];
          for (unsigned j = 0; j < 4; j++) { a->code[j] = e[4 + j]; a->act[j] = e[8 + j]; }
        }
        settings_sanitize(&settings);
      }
      for (unsigned i = 0; i < n; i++) {
        const adv_t *a = &settings.adv[first + i];
        uint8_t *e = r + 8 + 12 * i;
        e[0] = a->type; e[1] = a->a; e[2] = a->b; e[3] = a->p0;
        for (unsigned j = 0; j < 4; j++) { e[4 + j] = a->code[j]; e[8 + j] = a->act[j]; }
      }
    }
    r[1] = q[1]; r[2] = (uint8_t)n;
    break;
  }

  case OP_DEBUG_PRESS:
    if (st->keys && q[1] < SCAN_SITES) st->keys->force[q[1]] = q[2] == 1;
    r[1] = q[1]; r[2] = q[2];
    break;

  case OP_GET_KEYCFG:
  case OP_SET_KEYCFG: {
    uint8_t *const field[KEYCFG_FIELDS] = { settings.act, settings.rt_down, settings.rt_up };
    unsigned f = q[1], first = q[2] * MAP_PER_PAGE, n = 0;
    if (f < KEYCFG_FIELDS && first < SCAN_SITES) {
      n = SCAN_SITES - first < MAP_PER_PAGE ? SCAN_SITES - first : MAP_PER_PAGE;
      if (q[0] == OP_SET_KEYCFG) {
        if (q[3] < n) n = q[3];
        for (unsigned i = 0; i < n; i++) field[f][first + i] = q[8 + i];
        settings_sanitize(&settings);
      }
      for (unsigned i = 0; i < n; i++) r[8 + i] = field[f][first + i];
    }
    r[1] = q[1]; r[2] = q[2]; r[3] = (uint8_t)n;
    break;
  }

  case OP_GET_TRAVEL:
    r[1] = q[1];
    r[2] = (uint8_t)SCAN_SITES;
    for (unsigned i = 0; st->keys && i < TRAVEL_PER_PAGE; i++) {
      unsigned site = q[1] * TRAVEL_PER_PAGE + i;
      if (site >= SCAN_SITES) break;
      uint16_t rest = st->keys->rest[site], bottom = keys_bottom(st->keys, site);
      r[8 + 4 * i] = (uint8_t)rest;   r[9 + 4 * i] = (uint8_t)(rest >> 8);
      r[10 + 4 * i] = (uint8_t)bottom; r[11 + 4 * i] = (uint8_t)(bottom >> 8);
      if (settings.span[site]) r[3] |= 1;
    }
    break;

  case OP_TRAVEL:
    if (!st->keys) break;
    if (q[1] == 1) r[1] = (uint8_t)keys_store_travel(st->keys);
    else if (q[1] == 2) for (unsigned i = 0; i < SCAN_SITES; i++) settings.span[i] = 0;
    else if (q[1] == 3) keys_relearn(st->keys);
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
