/*
 * settings.h - everything the driver can change, in RAM, with defaults and a copy in flash.
 *
 * Stored in the 2 KB page at SETTINGS_ADDR, inside the app region below the failsafe proof
 * page. Written only on request (driver "Save"), with a magic, a layout version and a
 * checksum; anything that fails them loads the defaults.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include "board.h"
#include "leds.h"

#define SETTINGS_ADDR     0x08027000u
#define SETTINGS_MAGIC    0x53363046u    /* "F06S" */
#define SETTINGS_VERSION  3u             /* 2: + conn_mode (v0.07); 3: new Fn keys (v0.08) */

typedef struct {
  uint8_t  keymap[2][SCAN_SITES];        /* [0] base, [1] Fn (KC_NO = base key) */
  led_state_t led;
  uint8_t  debounce_ms;                  /* release hold-off, debounce.h */
  uint16_t press_below, release_above;   /* ADC thresholds, keys.h */
  uint8_t  conn_mode;                    /* last wireless mode (RADIO_BT1..3, RADIO_24G), Fn keys */
} settings_t;

extern settings_t settings;

void settings_defaults(settings_t *s);
void settings_sanitize(settings_t *s);   /* clamp values from the host into safe ranges */

#ifndef HOST_TEST
void settings_load(void);                /* flash copy if valid, else defaults */
int  settings_save(void);                /* 0 on success */
#endif

#endif /* SETTINGS_H */
