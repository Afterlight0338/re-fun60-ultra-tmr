/*
 * settings.h - everything the driver can change, in RAM, with defaults and a copy in flash.
 *
 * Stored in the 2 KB page at SETTINGS_ADDR, inside the app region below the failsafe proof
 * page. Written only on request (driver "Save"), with a magic, a layout version and a
 * checksum; anything that fails them loads the defaults.
 * Profiles (v0.17): PROFILES more pages below it hold one copy each. Save writes the live page and
 * the active profile's; switching saves the live settings into their profile, loads the other
 * (or keeps the current ones as its start, if it was never saved) and saves. Calibrated travel
 * and the wireless mode belong to the keyboard, not a profile, so they carry over.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include "board.h"
#include "leds.h"
#include "keys.h"

#define SETTINGS_ADDR     0x08027000u
#define PROFILES          4u
#define PROFILE_ADDR(n)   (0x08025000u + (n) * 0x800u)
#define SETTINGS_MAGIC    0x53363046u    /* "F06S" */
#define SETTINGS_VERSION  9u             /* 2: + conn_mode (v0.07); 3: new Fn keys (v0.08); 4: % thresholds (v0.12);
                                          * 5: per-key actuation, rapid trigger, dead zones, saved travel (v0.13);
                                          * 6: advanced keys, NKRO (v0.14); 7: effects, speed, key colours (v0.15);
                                          * 8: macros (v0.16); 9: profiles (v0.17) */

typedef struct {
  uint8_t  keymap[2][SCAN_SITES];        /* [0] base, [1] Fn (KC_NO = base key) */
  led_state_t led;
  uint8_t  debounce_ms;                  /* release hold-off, debounce.h */
  uint8_t  press_pct, release_pct;       /* travel thresholds in percent, keys.h */
  uint8_t  conn_mode;                    /* last wireless mode (RADIO_BT1..3, RADIO_24G), Fn keys */
  uint8_t  top_dz, bottom_dz;            /* dead zones, percent of travel: the top reads 0 %, the bottom 100 % */
  uint8_t  rt_continuous;                /* rapid trigger stays live up to the top dead zone, not just past
                                          * the actuation point */
  uint8_t  act[SCAN_SITES];              /* per-key actuation point in percent, 0 = press_pct */
  uint8_t  rt_down[SCAN_SITES];          /* rapid trigger: percent down that presses again, 0 = off */
  uint8_t  rt_up[SCAN_SITES];            /* percent up that releases, 0 = same as rt_down */
  uint16_t span[SCAN_SITES];             /* saved travel (rest - bottom) in counts, 0 = learn it */
  adv_t    adv[ADV_MAX];                 /* advanced keys, keys.h */
  uint8_t  nkro;                         /* USB: every key at once (interface 2), else 6 keys */
  macro_step_t macro[MACROS][MACRO_STEPS]; /* keys.h */
  uint8_t  profile;                      /* 0..PROFILES-1, the one these settings belong to */
} settings_t;

extern settings_t settings;

void settings_defaults(settings_t *s);
void settings_sanitize(settings_t *s);   /* clamp values from the host into safe ranges */

#ifndef HOST_TEST
void settings_load(void);                /* flash copy if valid, else defaults */
int  settings_save(void);                /* 0 on success */
int  settings_profile(unsigned n);       /* switch to profile n (see above); 0 on success */
#endif

#endif /* SETTINGS_H */
