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
  s->led = (led_state_t){ .on = 1, .effect = LED_RAINBOW, .level = 2, .r = 255, .g = 255, .b = 255, .speed = 2 };
  for (unsigned i = 0; i < LED_COUNT; i++) s->led.custom[i][0] = s->led.custom[i][1] = s->led.custom[i][2] = 255;
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
  for (unsigned j = 0; j < ADV_MAX; j++) s->adv[j] = (adv_t){0};
  for (unsigned m = 0; m < MACROS; m++)
    for (unsigned i = 0; i < MACRO_STEPS; i++) s->macro[m][i] = (macro_step_t){0};
  s->nkro = 1;
}

void settings_sanitize(settings_t *s)
{
  s->led.on = s->led.on ? 1 : 0;
  if (s->led.effect >= LED_EFFECTS) s->led.effect = LED_STATIC;
  if (s->led.level >= LED_LEVELS) s->led.level = LED_LEVELS - 1;
  if (s->led.speed >= LED_SPEEDS) s->led.speed = LED_SPEEDS - 1;
  if (s->debounce_ms > 50) s->debounce_ms = 50;
  if (s->press_pct < 10) s->press_pct = 10;               /* lower and noise alone could press */
  if (s->press_pct > 95) s->press_pct = 95;
  if (s->release_pct + 5 > s->press_pct) s->release_pct = s->press_pct - 5;
  if (s->conn_mode > RADIO_BT3 && s->conn_mode != RADIO_24G) s->conn_mode = RADIO_BT1;
  if (s->profile >= PROFILES) s->profile = 0;
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
  for (unsigned j = 0; j < ADV_MAX; j++) {
    adv_t *a = &s->adv[j];
    if (a->type > ADV_MACRO || (a->type == ADV_MACRO && a->p0 >= MACROS) || a->a >= SCAN_SITES || (a->type == ADV_SOCD && (a->b >= SCAN_SITES || a->b == a->a)))
      *a = (adv_t){0};
    if (a->type == ADV_SOCD && a->p0 > 3) a->p0 = 0;
    if (a->type == ADV_DKS && a->p0 > 100) a->p0 = 0;
    if (a->type == ADV_MACRO) a->b = a->b ? 1 : 0;
  }
  for (unsigned m = 0; m < MACROS; m++)
    for (unsigned i = 0; i < MACRO_STEPS; i++)
      if (s->macro[m][i].code && (s->macro[m][i].arg < 1 || s->macro[m][i].arg > 3)) s->macro[m][i].arg = 3;
  s->nkro = s->nkro ? 1 : 0;
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

static int valid(const stored_t *f)
{
  return f->magic == SETTINGS_MAGIC && f->version == SETTINGS_VERSION && f->size == sizeof(settings_t) &&
         f->sum == checksum(&f->s, sizeof f->s);
}

void settings_load(void)
{
  const stored_t *f = (const stored_t *)SETTINGS_ADDR;
  if (valid(f)) {
    memcpy(&settings, &f->s, sizeof settings);
    settings_sanitize(&settings);
  } else {
    settings_defaults(&settings);
  }
}

static int write_page(uint32_t addr)
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
  ok = flash_sector_erase(addr) == FLASH_OPERATE_DONE;
  for (unsigned i = 0; ok && i < (sizeof img + 3u) / 4u; i++)
    ok = flash_word_program(addr + 4u * i, w[i]) == FLASH_OPERATE_DONE;
  flash_lock();
  return ok && memcmp((const void *)addr, &img, sizeof img) == 0 ? 0 : -1;
}

int settings_save(void)
{
  return write_page(PROFILE_ADDR(settings.profile)) | write_page(SETTINGS_ADDR);
}

int settings_profile(unsigned n)
{
  if (n >= PROFILES) return -1;
  if (n == settings.profile) return 0;
  if (write_page(PROFILE_ADDR(settings.profile))) return -1;   /* keep the one we leave */
  const stored_t *f = (const stored_t *)PROFILE_ADDR(n);
  uint8_t conn = settings.conn_mode;
  static uint16_t span[SCAN_SITES];
  memcpy(span, settings.span, sizeof span);
  if (valid(f)) { memcpy(&settings, &f->s, sizeof settings); settings_sanitize(&settings); }
  settings.conn_mode = conn;                                   /* the keyboard's, not the profile's */
  memcpy(settings.span, span, sizeof span);
  settings.profile = (uint8_t)n;
  return settings_save();
}
#endif
