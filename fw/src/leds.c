/*
 * leds.c - effects, WS2812 encoding and the SPI2/DMA1 transport. See leds.h and docs/hw-map.md.
 */
#include "leds.h"

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

uint32_t leds_frame(const led_state_t *s, uint32_t t, const led_overlay_t *ov, uint8_t *out)
{
  static uint8_t rgb[LED_COUNT][3];
  uint32_t sum = 0, scale = s->on && !(ov && ov->blank) ? level_scale[s->level] : 0;

  if (s->effect == LED_BREATHE) {                 /* 4 s triangle, never fully dark */
    uint32_t p = t % 4000u, tri = p < 2000u ? p : 4000u - p;
    scale = scale * (32u + tri * 223u / 2000u) / 255u;
  }
  for (unsigned i = 0; i < LED_COUNT; i++) {
    uint8_t r = s->r, g = s->g, b = s->b;
    if (s->effect == LED_RAINBOW) hue_rgb((uint8_t)(led_x[i] * 256u / 64u + t / 16u), &r, &g, &b);
    rgb[i][0] = (uint8_t)(r * scale / 255u);
    rgb[i][1] = (uint8_t)(g * scale / 255u);
    rgb[i][2] = (uint8_t)(b * scale / 255u);
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
