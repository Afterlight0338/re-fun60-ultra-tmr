/*
 * settings.c - defaults, range checks and the flash copy. See settings.h.
 */
#include "settings.h"
#include "keys.h"
#include "radio.h"

settings_t settings;

void settings_defaults(settings_t *s)
{
  for (unsigned l = 0; l < 2; l++)
    for (unsigned i = 0; i < SCAN_SITES; i++) s->keymap[l][i] = keymap_default[l][i];
  s->led = (led_state_t){ .on = 1, .effect = LED_RAINBOW, .level = 2, .r = 255, .g = 255, .b = 255 };
  s->debounce_ms = KEY_DEBOUNCE_MS;
  s->press_pct = KEY_PRESS_PCT;
  s->release_pct = KEY_RELEASE_PCT;
  s->conn_mode = RADIO_BT1;               /* last wireless mode; stock starts on Bluetooth 1 */
  s->top_dz = s->bottom_dz = 0;
  s->rt_continuous = 0;
  for (unsigned i = 0; i < SCAN_SITES; i++) {
    s->act[i] = s->rt_down[i] = s->rt_up[i] = 0;
    s->span[i] = 0;
  }
}

void settings_sanitize(settings_t *s)
{
  s->led.on = s->led.on ? 1 : 0;
  if (s->led.effect >= LED_EFFECTS) s->led.effect = LED_STATIC;
  if (s->led.level >= LED_LEVELS) s->led.level = LED_LEVELS - 1;
  if (s->debounce_ms > 50) s->debounce_ms = 50;
  if (s->press_pct < 10) s->press_pct = 10;               /* lower and noise alone could press */
  if (s->press_pct > 95) s->press_pct = 95;
  if (s->release_pct + 5 > s->press_pct) s->release_pct = s->press_pct - 5;
  if (s->conn_mode > RADIO_BT3 && s->conn_mode != RADIO_24G) s->conn_mode = RADIO_BT1;
  if (s->top_dz > 30) s->top_dz = 30;
  if (s->bottom_dz > 30) s->bottom_dz = 30;
  s->rt_continuous = s->rt_continuous ? 1 : 0;
  for (unsigned i = 0; i < SCAN_SITES; i++) {
    if (s->act[i] && s->act[i] < 5) s->act[i] = 5;        /* 0 = global; under 5 % noise could press */
    if (s->act[i] > 100) s->act[i] = 100;
    if (s->rt_down[i] > 50) s->rt_down[i] = 50;
    if (s->rt_up[i] > 50) s->rt_up[i] = 50;
    if (s->span[i] && (s->span[i] < KEY_MIN_TRAVEL || s->span[i] > KEY_REST_MAX)) s->span[i] = 0;
  }
}

#ifndef HOST_TEST
#include <string.h>
#include "at32f402_405.h"

typedef struct {
  uint32_t magic;
  uint16_t version, size;
  settings_t s;
  uint32_t sum;
} stored_t;

static uint32_t checksum(const void *p, unsigned n)   /* FNV-1a */
{
  const uint8_t *b = p;
  uint32_t h = 0x811C9DC5u;
  while (n--) h = (h ^ *b++) * 0x01000193u;
  return h;
}

void settings_load(void)
{
  const stored_t *f = (const stored_t *)SETTINGS_ADDR;
  if (f->magic == SETTINGS_MAGIC && f->version == SETTINGS_VERSION && f->size == sizeof(settings_t) &&
      f->sum == checksum(&f->s, sizeof f->s)) {
    memcpy(&settings, &f->s, sizeof settings);
    settings_sanitize(&settings);
  } else {
    settings_defaults(&settings);
  }
}

int settings_save(void)
{
  static stored_t img __attribute__((aligned(4)));
  const uint32_t *w = (const uint32_t *)&img;
  int ok;

  img.magic = SETTINGS_MAGIC;
  img.version = SETTINGS_VERSION;
  img.size = sizeof(settings_t);
  memcpy(&img.s, &settings, sizeof settings);
  img.sum = checksum(&img.s, sizeof img.s);

  flash_unlock();
  ok = flash_sector_erase(SETTINGS_ADDR) == FLASH_OPERATE_DONE;
  for (unsigned i = 0; ok && i < (sizeof img + 3u) / 4u; i++)
    ok = flash_word_program(SETTINGS_ADDR + 4u * i, w[i]) == FLASH_OPERATE_DONE;
  flash_lock();
  return ok && memcmp((const void *)SETTINGS_ADDR, &img, sizeof img) == 0 ? 0 : -1;
}
#endif
