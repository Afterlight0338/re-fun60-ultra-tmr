/*
 * scan.c - board power-up and TMR matrix scan, mirroring stock v306.
 *
 * Columns: a serial shift register, data PC8, clock PC9, outputs enabled while PA8 is low.
 * Rows: an 8:1 analog mux (A0 PB9, A1 PB8, A2 PB7, enabled by PF7 low) into ADC1 ch2 (PA2);
 * stock reads channels 0-4 (+6 on column 6), we read all 8 (board.h).
 * Sequence and timing from keyscan_column @0x0800577c; pins and power from @0x080167b8;
 * ADC setup from @0x08012d60. docs/hw-map.md has the details.
 */
#include "at32f402_405.h"
#include "scan.h"

#define PIN(n) ((uint16_t)(1u << (n)))

static void out(gpio_type *port, uint16_t pins)
{
  gpio_init_type g;
  gpio_default_para_init(&g);
  g.gpio_pins = pins;
  g.gpio_mode = GPIO_MODE_OUTPUT;
  g.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  g.gpio_pull = GPIO_PULL_UP;                /* as stock */
  gpio_init(port, &g);
}

static void in_pullup(gpio_type *port, uint16_t pins)
{
  gpio_init_type g;
  gpio_default_para_init(&g);
  g.gpio_pins = pins;
  g.gpio_mode = GPIO_MODE_INPUT;
  g.gpio_pull = GPIO_PULL_UP;
  gpio_init(port, &g);
}

/* ~300 ns between column-logic edges. Back-to-back register writes (~10 ns) lost clocks now
 * and then (a whole column read as unpowered for one scan, ~2/s, worst on the last columns
 * that need the most clocks); stock's per-edge function calls are this slow or slower. */
static void settle(void)
{
  uint32_t s = DWT->CYCCNT;
  while (DWT->CYCCNT - s < 64u) { }
}

/* one shift-register clock: PC9 up (shifts PC8 in), data back to 0, PC9 down */
static void col_clock(void)
{
  GPIOC->scr = PIN(9);
  settle();
  GPIOC->clr = PIN(8);
  settle();
  GPIOC->clr = PIN(9);
  settle();
}

static void mux_select(unsigned ch)
{
  GPIOB->clr = PIN(9) | PIN(8) | PIN(7);
  GPIOB->scr = (uint16_t)((ch & 1 ? PIN(9) : 0) | (ch & 2 ? PIN(8) : 0) | (ch & 4 ? PIN(7) : 0));
}

static uint16_t adc_read(void)
{
  adc_ordinary_software_trigger_enable(ADC1, TRUE);
  while (adc_flag_get(ADC1, ADC_CCE_FLAG) == RESET) { }
  adc_flag_clear(ADC1, ADC_CCE_FLAG);
  return adc_ordinary_conversion_data_get(ADC1);
}

static void adc_setup(void)
{
  adc_base_config_type b;
  gpio_init_type g;

  crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK, TRUE);
  gpio_default_para_init(&g);
  g.gpio_pins = GPIO_PINS_2;                 /* PA2 = ADC1_IN2 */
  g.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init(GPIOA, &g);
  out(GPIOF, GPIO_PINS_7);                   /* mux enable: left low, as stock */
  GPIOF->clr = PIN(7);

  adc_clock_div_set(ADC_DIV_8);              /* 216 / 8 = 27 MHz (max 28) */
  adc_base_default_para_init(&b);
  b.sequence_mode = FALSE;
  b.repeat_mode = FALSE;
  b.data_align = ADC_RIGHT_ALIGNMENT;
  b.ordinary_channel_length = 1;
  adc_base_config(ADC1, &b);
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_2, 1, ADC_SAMPLETIME_28_5);
  /* OCSWTRG only starts a conversion when it is the selected trigger and triggers are on */
  adc_ordinary_conversion_trigger_set(ADC1, ADC12_ORDINARY_TRIG_SOFTWARE, TRUE);
  adc_enable(ADC1, TRUE);
  adc_calibration_init(ADC1);
  while (adc_calibration_init_status_get(ADC1)) { }
  adc_calibration_start(ADC1);
  while (adc_calibration_status_get(ADC1)) { }
  for (int i = 0; i < 1000; i++) (void)adc_read();   /* stock warms up with 1000 */
}

void scan_init(void)
{
  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_GPIOF_PERIPH_CLOCK, TRUE);

  adc_setup();

  in_pullup(GPIOB, GPIO_PINS_1);
  in_pullup(GPIOC, GPIO_PINS_13);
  in_pullup(GPIOA, GPIO_PINS_3);
  out(GPIOB, GPIO_PINS_13 | GPIO_PINS_9 | GPIO_PINS_8 | GPIO_PINS_7);
  out(GPIOC, GPIO_PINS_4 | GPIO_PINS_5 | GPIO_PINS_8 | GPIO_PINS_9);
  out(GPIOA, GPIO_PINS_7 | GPIO_PINS_8);
  GPIOC->scr = PIN(4) | PIN(8) | PIN(9);
  GPIOA->scr = PIN(8);
  GPIOB->scr = PIN(13);

  /* power rails, in stock's order and spacing */
  out(GPIOC, GPIO_PINS_14);
  GPIOC->scr = PIN(14);
  delay_ms(20);
  GPIOC->scr = PIN(5);
  delay_ms(100);
  GPIOA->scr = PIN(7);
}

/* battery divider on mux channel 7 (any column); stock waits 5 us for it, keys get 1 us */
uint16_t scan_battery(void)
{
  mux_select(7);
  delay_us(5);
  return adc_read();
}

void scan_frame(uint16_t *raw)
{
  for (unsigned c = 0; c < SCAN_COLS; c++) {
    /* walk a single 1 to output c, outputs off while shifting */
    GPIOC->scr = PIN(8);
    GPIOA->scr = PIN(8);
    mux_select(0);
    settle();
    for (unsigned i = 0; i <= c; i++) col_clock();
    GPIOA->clr = PIN(8);
    delay_us(5);

    for (unsigned ch = 0; ch < SCAN_CH; ch++) {
      mux_select(ch);
      delay_us(1);
      raw[c * SCAN_CH + ch] = adc_read();
    }

    /* outputs off, shift the 1 out of the chain (stock: 14 - next column, 14 on wrap) */
    GPIOA->scr = PIN(8);
    mux_select(0);
    settle();
    for (unsigned i = 0; i < (c + 1 < SCAN_COLS ? SCAN_COLS - (c + 1) : SCAN_COLS); i++) col_clock();
  }
}
