/*
 * leds.h - the 61 per-key RGB LEDs (WS2812-class on PA10 via SPI2 + DMA1), set up as stock v306
 * does (docs/hw-map.md). Effects are rendered by leds_frame() (pure, host-tested) and sent by
 * leds_task() whenever the previous frame has gone out.
 */
#ifndef LEDS_H
#define LEDS_H

#include <stdint.h>

#define LED_COUNT        61u
#define LED_FRAME_BYTES  (LED_COUNT * 24u)       /* one SPI byte per LED bit */
#define LED_PERIOD_MS    16u                     /* ~60 frames/s */

/* Everything from USB flows through this: 61 x 60 mA would be ~3.7 A at full white. Frames are
 * scaled so the LEDs draw at most ~LED_BUDGET_MA (WS2812 ~20 mA per channel at 255).
 * ponytail: estimate from the typical datasheet current, not measured on this board. */
#define LED_BUDGET_MA    300u

enum { LED_STATIC, LED_BREATHE, LED_RAINBOW, LED_EFFECTS };

typedef struct {
  uint8_t on, effect, level;                     /* level 0..LED_LEVELS-1 */
  uint8_t r, g, b;                               /* colour for Static and Breathe */
} led_state_t;
#define LED_LEVELS 5u

/* actions (bitmask), from the Fn layer (keys.h) */
#define LED_ACT_TOGGLE  0x01u
#define LED_ACT_BRI_UP  0x02u
#define LED_ACT_BRI_DN  0x04u
#define LED_ACT_EFFECT  0x08u

/* Chain order, from stock's indicator code (mode keys E R T Y U = LEDs 24 23 22 21 20, Caps 28,
 * Win 54): a snake, Esc = 0 .. Backspace 13, then \ 14 back to Tab 27, Caps 28 .. Enter 40,
 * RShift 41 back to LShift 52, LCtrl 53 .. RCtrl 60. */
#define LED_ESC   0u
#define LED_CAPS  28u
#define LED_LWIN  54u

/* Indicators drawn over the effect (indicators.h), at stock's indicator brightness. */
typedef struct {
  uint8_t blank;                     /* effect off, only the pixels below (2.4G pairing) */
  uint8_t n;
  struct { uint8_t led, r, g, b; } px[6];
} led_overlay_t;

void leds_apply(led_state_t *s, uint8_t actions);
/* Render state s at time t, plus overlay ov (may be NULL), into out[LED_FRAME_BYTES];
 * returns the estimated current in mA. */
uint32_t leds_frame(const led_state_t *s, uint32_t t_ms, const led_overlay_t *ov, uint8_t *out);

#ifndef HOST_TEST
void leds_init(void);
void leds_task(led_state_t *s, const led_overlay_t *ov, uint32_t now_ms);
#endif

#endif /* LEDS_H */
