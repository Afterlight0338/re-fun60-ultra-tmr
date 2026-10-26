/*
 * leds.c - effects, WS2812 encoding and the SPI2/DMA1 transport. See leds.h and README section 12.
 */
#include "leds.h"
#include "board.h"

static const uint8_t level_scale[LED_LEVELS] = { 16, 40, 80, 140, 255 };

void leds_apply(led_state_t *s, uint8_t a)
{
  if (a & LED_ACT_TOGGLE) s->on = !s->on;
  if ((a & LED_ACT_BRI_UP) && s->level + 1u < LED_LEVELS) s->level++;
  if ((a & LED_ACT_BRI_DN) && s->level > 0) s->level--;
  if (a & LED_ACT_EFFECT) s->effect = (uint8_t)((s->effect + 1u) % LED_EFFECTS);
}

/* hue 0..255 -> full-saturation RGB */
static void hue_rgb(uint8_t h, uint8_t *r, uint8_t *g, uint8_t *b)
{
  uint8_t sector = h / 43, f = (uint8_t)((h - sector * 43u) * 6u);
  uint8_t up = f, down = (uint8_t)(255u - f);
  switch (sector) {
  case 0:  *r = 255;  *g = up;   *b = 0;    break;
  case 1:  *r = down; *g = 255;  *b = 0;    break;
  case 2:  *r = 0;    *g = 255;  *b = up;   break;
  case 3:  *r = 0;    *g = down; *b = 255;  break;
  case 4:  *r = up;   *g = 0;    *b = 255;  break;
  default: *r = 255;  *g = 0;    *b = down; break;
  }
}

static void put(uint8_t *out, uint8_t v)          /* one colour byte -> 8 SPI bytes, MSB first */
{
  for (unsigned bit = 0; bit < 8; bit++) out[bit] = (v & (0x80u >> bit)) ? 0xF0 : 0xC0;
}

/* key centre across the board per LED, in quarter keys (0..60), following the chain snake */
static const uint8_t led_x[LED_COUNT] = {
  2, 6, 10, 14, 18, 22, 26, 30, 34, 38, 42, 46, 50, 56,           /* Esc .. Backspace */
  57, 52, 48, 44, 40, 36, 32, 28, 24, 20, 16, 12, 8, 3,            /* \ .. Tab */
  4, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45, 49, 56,                /* Caps .. Enter */
  55, 47, 43, 39, 35, 31, 27, 23, 19, 15, 11, 5,                   /* RShift .. LShift */
  3, 8, 13, 28, 43, 48, 53, 58                                     /* LCtrl .. RCtrl */
};

/* row of each LED (0 = number row), and the scan site under it: the chain snakes left to right
 * on rows 0, 2, 4 and right to left on 1, 3 (keys.c's layout) */
static const uint8_t led_y[LED_COUNT] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0, 1,1,1,1,1,1,1,1,1,1,1,1,1,1, 2,2,2,2,2,2,2,2,2,2,2,2,2,
  3,3,3,3,3,3,3,3,3,3,3,3, 4,4,4,4,4,4,4,4
};
#define S_(col, ch) ((col) * SCAN_CH + (ch))
static const uint8_t led_site[LED_COUNT] = {
  S_(0,0), S_(1,0), S_(2,0), S_(3,0), S_(4,0), S_(5,0), S_(6,0), S_(7,0), S_(8,0), S_(9,0), S_(10,0), S_(11,0), S_(12,0), S_(13,0),
  S_(13,1), S_(12,1), S_(11,1), S_(10,1), S_(9,1), S_(8,1), S_(7,1), S_(6,1), S_(5,1), S_(4,1), S_(3,1), S_(2,1), S_(1,1), S_(0,1),
  S_(0,2), S_(1,2), S_(2,2), S_(3,2), S_(4,2), S_(5,2), S_(6,2), S_(7,2), S_(8,2), S_(9,2), S_(10,2), S_(11,2), S_(13,2),
  S_(12,3), S_(11,3), S_(10,3), S_(9,3), S_(8,3), S_(7,3), S_(6,3), S_(5,3), S_(4,3), S_(3,3), S_(2,3), S_(0,3),
  S_(0,4), S_(2,4), S_(3,4), S_(6,6), S_(9,4), S_(10,4), S_(11,4), S_(12,4)
};

int leds_of_site(unsigned site)
{
  for (unsigned i = 0; i < LED_COUNT; i++) if (led_site[i] == site) return (int)i;
  return -1;
}

/* presses for Reactive (per LED) and Ripple (the last few, with where they started) */
#define RIPPLES 8u
static uint32_t hit_ms[LED_COUNT];
static uint8_t hit_seen[LED_COUNT];
static struct { uint8_t led, on; uint32_t ms; } ripple[RIPPLES];
static uint8_t ripple_next;

void leds_keys(const uint8_t *pressed, uint32_t now)
{
  for (unsigned i = 0; i < LED_COUNT; i++) {
    uint8_t down = pressed[led_site[i]] != 0;
    if (down && !hit_seen[i]) {
      hit_ms[i] = now | 1u;                        /* 0 = never pressed */
      ripple[ripple_next].led = (uint8_t)i; ripple[ripple_next].ms = now; ripple[ripple_next].on = 1;
      ripple_next = (uint8_t)((ripple_next + 1u) % RIPPLES);
    }
    hit_seen[i] = down;
    if (down) hit_ms[i] = now | 1u;                /* Reactive: lit while held, fades after */
  }
}

/* 0..255 brightness of LED i under the animated effects at time t */
static uint32_t fade(uint32_t since, uint32_t len) { return since >= len ? 0 : 255u - since * 255u / len; }

uint32_t leds_frame(const led_state_t *s, uint32_t t, const led_overlay_t *ov, uint8_t *out)
{
  static uint8_t rgb[LED_COUNT][3];
  uint32_t sum = 0, scale = s->on && !(ov && ov->blank) ? level_scale[s->level] : 0;
  uint32_t sp = s->speed < LED_SPEEDS ? s->speed : 2u;     /* 0 slow .. 4 fast */
  uint32_t step = 32u - sp * 6u;                           /* ms per hue step: 32 .. 8 */

  if (s->effect == LED_BREATHE) {                 /* triangle, 6 s .. 2 s, never fully dark */
    uint32_t period = 6000u - sp * 1000u, p = t % period, half = period / 2u, tri = p < half ? p : period - p;
    scale = scale * (32u + tri * 223u / half) / 255u;
  }
  for (unsigned i = 0; i < LED_COUNT; i++) {
    uint8_t r = s->r, g = s->g, b = s->b;
    uint32_t lit = 255;
    if (s->effect == LED_RAINBOW) hue_rgb((uint8_t)(led_x[i] * 256u / 64u + t / step), &r, &g, &b);
    if (s->effect == LED_SPECTRUM) hue_rgb((uint8_t)(t / (step * 2u)), &r, &g, &b);
    if (s->effect == LED_CUSTOM) { r = s->custom[i][0]; g = s->custom[i][1]; b = s->custom[i][2]; }
    if (s->effect == LED_REACTIVE) lit = hit_ms[i] ? fade(t - hit_ms[i], 1200u - sp * 200u) : 0;
    if (s->effect == LED_RIPPLE) {
      lit = 0;
      for (unsigned k = 0; k < RIPPLES; k++) {
        if (!ripple[k].on) continue;
        uint32_t age = t - ripple[k].ms, radius = age * (4u + sp * 2u) / 100u;   /* quarter keys: 4-12 keys/s */
        if (radius > 64u) { ripple[k].on = 0; continue; }
        int dx = (int)led_x[i] - (int)led_x[ripple[k].led], dy = ((int)led_y[i] - (int)led_y[ripple[k].led]) * 4;
        uint32_t d = (uint32_t)(dx < 0 ? -dx : dx) + (uint32_t)(dy < 0 ? -dy : dy);     /* taxicab, quarter keys */
        uint32_t off = d > radius ? d - radius : radius - d;
        if (off < 4u) { uint32_t v = (4u - off) * 63u * (64u - radius) / 64u; if (v > lit) lit = v; }   /* max 252 */
      }
    }
    rgb[i][0] = (uint8_t)(r * scale / 255u * lit / 255u);
    rgb[i][1] = (uint8_t)(g * scale / 255u * lit / 255u);
    rgb[i][2] = (uint8_t)(b * scale / 255u * lit / 255u);
  }
  for (unsigned k = 0; ov && k < ov->n; k++) {    /* stock draws indicators at 100/150 */
    uint8_t i = ov->px[k].led;
    if (i >= LED_COUNT) continue;
    rgb[i][0] = (uint8_t)(ov->px[k].r * 2u / 3u);
    rgb[i][1] = (uint8_t)(ov->px[k].g * 2u / 3u);
    rgb[i][2] = (uint8_t)(ov->px[k].b * 2u / 3u);
  }
  for (unsigned i = 0; i < LED_COUNT; i++) sum += rgb[i][0] + rgb[i][1] + rgb[i][2];

  uint32_t ma = sum * 20u / 255u;                 /* ~20 mA per channel at 255 */
  for (unsigned i = 0; i < LED_COUNT; i++) {
    for (unsigned c = 0; c < 3; c++)
      if (ma > LED_BUDGET_MA) rgb[i][c] = (uint8_t)(rgb[i][c] * LED_BUDGET_MA / ma);
    put(out + i * 24u,      rgb[i][1]);           /* G */
    put(out + i * 24u + 8,  rgb[i][0]);           /* R */
    put(out + i * 24u + 16, rgb[i][2]);           /* B */
  }
  return ma > LED_BUDGET_MA ? LED_BUDGET_MA : ma;
}

#ifndef HOST_TEST
#include "at32f402_405.h"

static uint8_t frame[LED_FRAME_BYTES] __attribute__((aligned(4)));
static int sending;
static uint32_t last_ms;

void leds_init(void)
{
  gpio_init_type g;
  spi_init_type sp;
  dma_init_type d;

  crm_periph_clock_enable(CRM_SPI2_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK, TRUE);

  gpio_default_para_init(&g);                     /* PA10 = SPI2 MOSI (AF5) */
  g.gpio_pins = GPIO_PINS_10;
  g.gpio_mode = GPIO_MODE_MUX;
  g.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  g.gpio_pull = GPIO_PULL_UP;
  g.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init(GPIOA, &g);
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE10, GPIO_MUX_5);

  spi_default_para_init(&sp);
  sp.transmission_mode = SPI_TRANSMIT_HALF_DUPLEX_TX;
  sp.master_slave_mode = SPI_MODE_MASTER;
  sp.mclk_freq_division = SPI_MCLK_DIV_16;         /* 108 MHz / 16 = 6.75 MHz: 1.19 us per LED bit */
  sp.first_bit_transmission = SPI_FIRST_BIT_MSB;
  sp.frame_bit_num = SPI_FRAME_8BIT;
  sp.clock_polarity = SPI_CLOCK_POLARITY_LOW;
  sp.clock_phase = SPI_CLOCK_PHASE_2EDGE;
  sp.cs_mode_selection = SPI_CS_SOFTWARE_MODE;
  spi_init(SPI2, &sp);
  spi_hardware_cs_output_enable(SPI2, TRUE);       /* as stock */
  spi_i2s_dma_transmitter_enable(SPI2, TRUE);
  spi_enable(SPI2, TRUE);

  dmamux_enable(DMA1, TRUE);
  dma_reset(DMA1_CHANNEL1);
  dmamux_init(DMA1MUX_CHANNEL1, DMAMUX_DMAREQ_ID_SPI2_TX);
  dma_default_para_init(&d);
  d.peripheral_base_addr = (uint32_t)&SPI2->dt;
  d.memory_base_addr = (uint32_t)frame;
  d.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
  d.buffer_size = LED_FRAME_BYTES;
  d.peripheral_inc_enable = FALSE;
  d.memory_inc_enable = TRUE;
  d.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  d.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  d.loop_mode_enable = FALSE;
  d.priority = DMA_PRIORITY_HIGH;
  dma_init(DMA1_CHANNEL1, &d);
}

/* Every LED_PERIOD_MS, once the last frame is out: render and send the next. The line stays
 * low for the rest of the period (~14 ms), far longer than the WS2812 latch time. */
void leds_task(led_state_t *s, const led_overlay_t *ov, uint32_t now)
{
  if (now - last_ms < LED_PERIOD_MS) return;
  if (sending && dma_flag_get(DMA1_FDT1_FLAG) == RESET) return;
  last_ms = now;
  leds_frame(s, now, ov, frame);
  dma_channel_enable(DMA1_CHANNEL1, FALSE);
  dma_flag_clear(DMA1_FDT1_FLAG);
  dma_data_number_set(DMA1_CHANNEL1, LED_FRAME_BYTES);
  dma_channel_enable(DMA1_CHANNEL1, TRUE);
  sending = 1;
}
#endif
