/*
 * keys.h - raw scan -> pressed keys -> boot keyboard report.
 *
 * Mechanical switches on this board pull their sense line from ~1400-2100 counts at rest down
 * to ~50-150 when closed (rawscan capture 2026-09-28; idle noise +-3). So a fixed threshold with
 * hysteresis is enough: pressed below KEY_PRESS_BELOW, released above KEY_RELEASE_ABOVE.
 * ponytail: fixed thresholds only suit mechanical switches; magnetic (TMR) switches need
 * per-key calibration and travel, add it when one goes in.
 */
#ifndef KEYS_H
#define KEYS_H

#include <stdint.h>
#include "board.h"
#include "debounce.h"

#define KEY_PRESS_BELOW    700u    /* defaults; the live values are in settings.h */
#define KEY_RELEASE_ABOVE  1000u
#define KEY_DEBOUNCE_MS    8u      /* release hold-off, see debounce.h */

/* keymap codes: HID keyboard usages, plus */
#define KC_NO   0x00u
#define KC_FN   0x01u              /* 0x01 is ErrorRollOver in HID; never a real key here */
/* firmware-internal codes, from the keyboard page's reserved range 0xA5-0xDF: */
#define KC_MUTE     0xA5u          /* consumer usages, sent on the consumer interface */
#define KC_VOLD     0xA6u
#define KC_VOLU     0xA7u
#define KC_CALC     0xA8u
#define KC_PLAY     0xA9u
#define KC_NEXT     0xAAu
#define KC_PREV     0xABu
#define KC_LED_TOG  0xB0u          /* lighting actions, handled on the keyboard (leds.h) */
#define KC_LED_BRU  0xB1u
#define KC_LED_BRD  0xB2u
#define KC_LED_FX   0xB3u
#define KC_BT1      0xB8u          /* connection mode (radio.h); hold a wireless one to pair */
#define KC_BT2      0xB9u
#define KC_BT3      0xBAu
#define KC_24G      0xBBu
#define KC_USB      0xBCu
#define KC_BATT     0xBDu          /* hold: battery level on Esc (indicators.h) */
#define KC_WINLOCK  0xBEu          /* toggle: Win keys do nothing */

typedef struct {
  uint8_t  raw_pressed[SCAN_SITES];  /* threshold state (with hysteresis) */
  uint8_t  armed[SCAN_SITES];        /* seen released at least once since boot */
  uint8_t  pressed[SCAN_SITES];      /* after debounce */
  uint8_t  actions;                  /* LED_ACT_* pressed and not yet taken */
  uint8_t  winlock;                  /* KC_WINLOCK toggles it; GUI modifiers dropped */
  debounce_t db;
} keys_t;

extern const uint8_t keymap_default[2][SCAN_SITES];  /* [0] base, [1] Fn held (KC_NO = base) */

void keys_init(keys_t *k);
/* One scan frame in; returns 1 if the debounced key set changed. */
int  keys_update(keys_t *k, const uint16_t *raw, uint32_t now_ms);
/* Build the 8-byte boot report (modifiers, 0, 6 keys; 6+ keys = ErrorRollOver). */
void keys_report(const keys_t *k, uint8_t report[8]);
/* HID consumer usage currently held (0 = none). */
uint16_t keys_consumer(const keys_t *k);
/* Lighting actions (LED_ACT_*) pressed since the last call. */
uint8_t keys_take_actions(keys_t *k);
/* A key held right now whose code is in lo..hi (e.g. KC_BT1..KC_USB), or 0. */
uint8_t keys_held(const keys_t *k, uint8_t lo, uint8_t hi);

#endif /* KEYS_H */
