/*
 * radio.c - see radio.h. Evidence: stock v306 FUN_08016344 (pins, SPI3), FUN_08015c80 (what is
 * sent, in which order), app_vec28 (reply check and parse), FUN_0800cfb0 / case 0xe of the key
 * handler (mode keys and pairing).
 */
#include "radio.h"
#include "board.h"

radio_t radio = { .mode = RADIO_USB };

static uint8_t want_mode, want_status, want_pair;
static uint8_t kbd[9], kbd_due, consumer[3], consumer_due, battery, battery_due;

unsigned radio_frame(uint8_t *buf, uint8_t cmd, const uint8_t *payload, uint8_t n)
{
  unsigned len = ((unsigned)n + 3u + 3u) & ~3u;   /* cmd, n, payload, sum; rounded up to 4 */
  uint8_t sum = 0;
  for (unsigned i = 0; i < RADIO_XFER_MAX; i++) buf[i] = 0;
  buf[0] = cmd;
  buf[1] = n;
  for (unsigned i = 0; i < n; i++) { buf[2 + i] = payload[i]; sum = (uint8_t)(sum + payload[i]); }
  buf[2 + n] = sum;
  return len;
}

int radio_parse(const uint8_t *rx)
{
  uint8_t n = rx[1], sum = 0;
  for (int i = 2; i > 0; i--)                    /* keep the last few replies for GET_RADIO */
    for (unsigned j = 0; j < 16; j++) radio.last[i][j] = radio.last[i - 1][j];
  for (unsigned j = 0; j < 16; j++) radio.last[0][j] = rx[j];

  if (n == 0 || n > RADIO_XFER_MAX - 3u) { radio.bad++; return -1; }
  for (unsigned i = 0; i < n; i++) sum = (uint8_t)(sum + rx[2 + i]);
  if (sum != rx[2 + n]) { radio.bad++; return -1; }
  radio.rx++;

  switch (rx[2]) {
  case 0x10:                                     /* status */
    want_status = 0;
    radio.status_seen = 1;
    radio.host_leds = rx[3] & 7u;
    radio.state = rx[4];
    radio.radio_mode = rx[5];
    if (radio.radio_mode != radio.mode) want_mode = 1;
    if (radio.pairing_24g) {
      if (radio.state == RADIO_ST_PAIRED) radio.pairing_24g = 0;           /* as stock */
      else want_pair = 1;
    }
    break;
  case 0x12:                                     /* stock: stop asking for status / mode */
    want_status = want_mode = 0;
    break;
  case 0x66:
  case 0xFF:                                     /* stock: ask for status again */
    want_status = 1;
    break;
  default:                                       /* 0x00 / 0x11: host data (driver over the air), later */
    break;
  }
  return rx[2];
}

void radio_set_mode(uint8_t mode)
{
  radio.mode = mode;
  radio.pairing_24g = 0;
  want_pair = 0;
  want_mode = 1;
}

void radio_pair(void)
{
  if (radio.mode == RADIO_24G) radio.pairing_24g = 1;
  else if (radio.mode <= RADIO_BT3) want_pair = 1;
}

void radio_keyboard(const uint8_t r[8])
{
  kbd[0] = 1;                                    /* type 1: mods + 6 keys (stock's 0x20008050) */
  kbd[1] = r[0];
  for (unsigned i = 0; i < 6; i++) kbd[2 + i] = r[2 + i];
  kbd_due = 1;
}

void radio_consumer(uint16_t usage)
{
  consumer[0] = 3;
  consumer[1] = (uint8_t)usage;
  consumer[2] = (uint8_t)(usage >> 8);
  consumer_due = 1;
}

void radio_battery(uint8_t percent)
{
  battery = percent;
  battery_due = 1;
}

#ifndef HOST_TEST
#include "at32f402_405.h"

#define CONTROL_GAP_MS  10u
#define REPORT_GAP_MS   1u      /* ponytail: stock allows 250 us; 1 ms caps us at 1000 reports/s */

static void pin(gpio_type *port, uint16_t pins, gpio_mode_type mode, gpio_pull_type pull)
{
  gpio_init_type g;
  gpio_default_para_init(&g);
  g.gpio_pins = pins;
  g.gpio_mode = mode;
  g.gpio_pull = pull;
  g.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  g.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(port, &g);
}

void radio_init(uint8_t mode)
{
  spi_init_type s;

  crm_periph_clock_enable(CRM_SPI3_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOD_PERIPH_CLOCK, TRUE);
  pin(GPIOD, GPIO_PINS_2, GPIO_MODE_INPUT, GPIO_PULL_UP);          /* radio wants attention: low */
  GPIOA->scr = GPIO_PINS_15;
  pin(GPIOA, GPIO_PINS_15, GPIO_MODE_OUTPUT, GPIO_PULL_UP);        /* chip select, idle high */
  pin(GPIOB, GPIO_PINS_3 | GPIO_PINS_4 | GPIO_PINS_5, GPIO_MODE_MUX, GPIO_PULL_NONE);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE3, GPIO_MUX_6);       /* SPI3 SCK, MISO, MOSI */
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE4, GPIO_MUX_6);
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE5, GPIO_MUX_6);

  spi_default_para_init(&s);
  s.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
  s.master_slave_mode = SPI_MODE_MASTER;
  s.mclk_freq_division = SPI_MCLK_DIV_16;         /* 108 MHz / 16 = 6.75 MHz, as stock */
  s.first_bit_transmission = SPI_FIRST_BIT_MSB;
  s.frame_bit_num = SPI_FRAME_8BIT;
  s.clock_polarity = SPI_CLOCK_POLARITY_LOW;
  s.clock_phase = SPI_CLOCK_PHASE_2EDGE;
  s.cs_mode_selection = SPI_CS_SOFTWARE_MODE;
  spi_init(SPI3, &s);
  spi_enable(SPI3, TRUE);

  GPIOA->clr = GPIO_PINS_15;                      /* stock: 300 us chip-select pulse at start */
  delay_us(300);
  GPIOA->scr = GPIO_PINS_15;

  radio_set_mode(mode);                           /* stock main: tell the radio the mode at boot */
}

/* ponytail: polled, ~85 us for the longest frame; stock uses DMA1 ch2/ch3 */
static void xfer(const uint8_t *out, uint8_t *in, unsigned n)
{
  GPIOA->clr = GPIO_PINS_15;
  delay_us(1);
  for (unsigned i = 0; i < n; i++) {
    while (spi_i2s_flag_get(SPI3, SPI_I2S_TDBE_FLAG) == RESET) { }
    spi_i2s_data_transmit(SPI3, out[i]);
    while (spi_i2s_flag_get(SPI3, SPI_I2S_RDBF_FLAG) == RESET) { }
    in[i] = (uint8_t)spi_i2s_data_receive(SPI3);
  }
  while (spi_i2s_flag_get(SPI3, SPI_I2S_BF_FLAG) == SET) { }
  GPIOA->scr = GPIO_PINS_15;
  radio.tx++;
}

void radio_task(uint32_t now)
{
  static uint32_t last;
  static const uint8_t zeros[64];
  uint8_t out[RADIO_XFER_MAX], in[RADIO_XFER_MAX], p[33];
  unsigned n;

  static uint32_t pair_since;
  if (!radio.pairing_24g) pair_since = now;
  else if (now - pair_since > 30000u) radio.pairing_24g = 0;   /* stock gives 2.4G pairing 30 s */

  if (now - last < REPORT_GAP_MS) return;
  if (gpio_input_data_bit_read(GPIOD, GPIO_PINS_2) == RESET) {    /* the radio has a reply */
    n = radio_frame(out, 0x09, zeros, 64);
    xfer(out, in, n);
    radio_parse(in);
    last = now;
    return;
  }
  if (now - last >= CONTROL_GAP_MS) {
    if (want_mode) {
      want_mode = 0;
      want_status = 1;
      p[0] = radio.mode;
      n = radio_frame(out, 0x93, p, 1);
    } else if (want_status) {
      p[0] = 0;
      n = radio_frame(out, 0x92, p, 1);
    } else if (want_pair) {
      want_pair = 0;
      if (radio.mode <= RADIO_BT3) {              /* Bluetooth: pair, advertising "FUN60 Ultra-N" */
        static const char name[] = "FUN60 Ultra-";
        for (unsigned i = 0; i < sizeof p; i++) p[i] = 0;
        p[0] = 2;
        p[1] = 13;
        for (unsigned i = 0; i < 12; i++) p[2 + i] = (uint8_t)name[i];
        p[14] = (uint8_t)('1' + radio.mode);
        n = radio_frame(out, 0x94, p, 33);
      } else {
        p[0] = radio.state == RADIO_ST_PAIRING ? 4 : 1;   /* 2.4G: connect (stock: op 4 in state 4) */
        n = radio_frame(out, 0x94, p, 1);
      }
      want_status = 1;
    } else {
      n = 0;
    }
    if (n) { xfer(out, in, n); last = now; return; }
  }
  if (radio.mode == RADIO_USB) return;
  if (kbd_due) { kbd_due = 0; n = radio_frame(out, 0x81, kbd, 8); }
  else if (consumer_due) { consumer_due = 0; n = radio_frame(out, 0x81, consumer, 3); }
  else if (battery_due) { battery_due = 0; n = radio_frame(out, 0x90, &battery, 1); }
  else return;
  xfer(out, in, n);
  last = now;
}
#endif
