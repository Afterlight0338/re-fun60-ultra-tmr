/*
 * keys.h - raw scan -> pressed keys -> boot keyboard report.
 *
 * Every site calibrates itself, the way stock v306 does (calibration @0x0800d580, per-key
 * travel @0x0800de68): rest = the highest reading seen, bottom = the lowest, but never less
 * than KEY_MIN_TRAVEL below rest (stock's default bottom is rest - 400, and it only takes a
 * measured bottom that is more than 400 below rest). Travel is (rest - reading) as a percent of
 * (rest - bottom); pressed at press_pct, released below release_pct. Readings fall on press for
 * both switch kinds (stock's travel math assumes it), so mechanical and magnetic (TMR) switches
 * can be mixed:
 * - mechanical: ~1400-2100 at rest, ~50-150 closed (rawscan 2026-09-28), so 0 or 100 percent
 * - magnetic: travel is taken as linear in counts (stock maps it through a per-switch curve
 *   table to 0.01 mm; we don't)
 * A site only counts once its rest is inside stock's KEY_REST_MIN..KEY_REST_MAX window, so a
 * key held since boot, an empty socket (reads 0-2) or a dead sensor never types.
 * Per key (settings.h, v0.13): its own actuation point, rapid trigger (after the first actuation
 * a key releases as soon as it rises rt_up percent from its deepest point and presses again
 * when it falls rt_down from its highest, anywhere below the release point, or anywhere below
 * the top dead zone with rt_continuous), and a saved travel span: rest is still learned live,
 * bottom = rest - span, so a calibrated key is exact from the first press and a key held
 * since boot still reads 0 % until it is let go.
 * Dead zones: travel inside top_dz reads 0 %, inside bottom_dz reads 100 %.
 * ponytail: rest only ever rises, so a magnetic sensor drifting down (temperature) reads as a
 * little travel; add stock-style idle re-averaging if a key ever triggers on its own.
 */
#ifndef KEYS_H
#define KEYS_H

#include <stdint.h>
#include "board.h"
#include "debounce.h"

#define KEY_PRESS_PCT      50u     /* defaults; the live values are in settings.h */
#define KEY_RELEASE_PCT    35u
#define KEY_MIN_TRAVEL     400u    /* counts; stock's default and minimum calibrated travel */
#define KEY_REST_MIN       1000u   /* stock ignores a key whose rest is outside this */
#define KEY_REST_MAX       3300u
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
  uint16_t rest[SCAN_SITES];         /* highest reading seen (key up) */
  uint16_t bottom[SCAN_SITES];       /* lowest reading seen (key down) */
  uint8_t  pressed[SCAN_SITES];      /* after debounce */
  uint8_t  pct[SCAN_SITES];          /* travel now, percent, dead zones applied */
  uint8_t  ext[SCAN_SITES];          /* rapid trigger: deepest point while down, highest while up */
  uint8_t  rt_live[SCAN_SITES];      /* rapid trigger engaged (actuated since the last reset) */
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
/* Effective bottom of site i (learned, or rest - saved span), as keys_update uses it. */
uint16_t keys_bottom(const keys_t *k, unsigned i);
/* Save the travel each key has shown since power-on (bottomed out at least KEY_MIN_TRAVEL
 * below rest) into settings.span; returns how many keys were stored. SAVE writes it to flash. */
unsigned keys_store_travel(const keys_t *k);
/* Forget what was learned since power-on (rest and bottom start again from the next frame). */
void keys_relearn(keys_t *k);
/* A key held right now whose code is in lo..hi (e.g. KC_BT1..KC_USB), or 0. */
uint8_t keys_held(const keys_t *k, uint8_t lo, uint8_t hi);

#endif /* KEYS_H */
