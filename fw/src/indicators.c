/*
 * indicators.c - see indicators.h.
 */
#include "indicators.h"
#include "radio.h"

enum { SHOW_NONE, SHOW_SOLID, SHOW_FAST, SHOW_SLOW };   /* stock's 0x2000849c + 0x22 */

static void px(led_overlay_t *ov, uint8_t led, uint8_t r, uint8_t g, uint8_t b)
{
  if (ov->n >= sizeof ov->px / sizeof ov->px[0]) return;
  ov->px[ov->n].led = led;
  ov->px[ov->n].r = r;
  ov->px[ov->n].g = g;
  ov->px[ov->n].b = b;
  ov->n++;
}

static uint8_t mode_led(uint8_t mode)
{
  static const uint8_t led[] = { 24, 23, 22, 0xFF, 0xFF, 21, 20 };   /* E R T - - Y U */
  return mode <= RADIO_USB ? led[mode] : 0xFF;
}

void indicators(const ind_in_t *in, uint32_t now, led_overlay_t *ov)
{
  static uint8_t last_mode = 0xFF, want;
  static uint32_t since;
  int wireless = in->mode != RADIO_USB;

  ov->blank = 0;
  ov->n = 0;

  /* Esc: battery key, then charging / low battery on top (stock draws them in this order) */
  if (in->battery_key && in->percent) {
    uint8_t p = in->percent;
    if (p < 35)      px(ov, LED_ESC, 255, 0, 0);
    else if (p < 55) px(ov, LED_ESC, 0, 0, 255);
    else if (p < 75) px(ov, LED_ESC, 255, 0, 255);
    else if (p < 95) px(ov, LED_ESC, 255, 255, 0);
    else             px(ov, LED_ESC, 0, 255, 0);
  }
  if (in->usb && in->charging) px(ov, LED_ESC, 255, 0, 0);
  else if (!in->usb && in->percent && in->percent < 21 && (now / 400u) % 2u == 0) px(ov, LED_ESC, 255, 0, 0);

  if (in->caps) px(ov, LED_CAPS, 255, 255, 255);
  if (in->winlock) px(ov, LED_LWIN, 255, 255, 255);

  /* mode key: what to show follows the mode and, in wireless, the radio's state */
  uint8_t next = want;
  if (in->mode != last_mode) next = wireless ? SHOW_SLOW : SHOW_SOLID;
  else if (wireless && in->status_seen)
    next = in->radio_state == 3 ? SHOW_SOLID : in->radio_state == 4 ? SHOW_FAST
         : in->radio_state <= 2 ? SHOW_SLOW : want;
  if (next != want || in->mode != last_mode) { want = next; since = now; }
  last_mode = in->mode;

  uint32_t age = now - since;
  int on = want == SHOW_SOLID ? age < 2000u
         : want == SHOW_FAST  ? (age / 100u) % 2u == 0
         : want == SHOW_SLOW  ? (age / 900u) % 2u == 0 : 0;
  uint8_t led = mode_led(in->mode);
  if (on && led != 0xFF)
    px(ov, led, in->mode <= RADIO_BT3 ? 0 : in->mode == RADIO_USB ? 255 : 0,
                in->mode == RADIO_24G ? 255 : 0, in->mode <= RADIO_BT3 ? 255 : 0);

  if (in->pairing_24g) {                          /* stock: all dark, Esc white */
    ov->blank = 1;
    ov->n = 0;
    px(ov, LED_ESC, 255, 255, 255);
  }
}
