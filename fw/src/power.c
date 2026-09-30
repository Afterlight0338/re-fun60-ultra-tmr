/*
 * power.c - see power.h.
 */
#include "power.h"

power_t power;

uint8_t battery_percent(uint16_t raw)
{
  if (raw < BATTERY_ABSENT) return 0;
  if (raw <= 1200u) return 1;
  if (raw <= 1320u) return (uint8_t)(1u + (raw - 1200u) * 19u / 120u);
  if (raw < 1595u) return (uint8_t)(20u + (raw - 1320u) * 80u / 275u);
  return 100;
}

#ifndef HOST_TEST
#include "at32f402_405.h"
#include "scan.h"

void power_task(uint32_t now)
{
  static uint16_t ring[8];
  static uint32_t last;
  static unsigned n;
  uint32_t sum = 0;

  if (n && now - last < POWER_PERIOD_MS) return;
  last = now;
  ring[n++ % 8u] = scan_battery();
  for (unsigned i = 0; i < 8u; i++) sum += ring[i];
  power.raw = (uint16_t)(n >= 8u ? sum / 8u : ring[(n - 1u) % 8u]);   /* first 1.6 s: latest */
  power.usb = gpio_input_data_bit_read(GPIOC, GPIO_PINS_13) == SET;
  power.charging = gpio_input_data_bit_read(GPIOA, GPIO_PINS_3) == RESET;   /* any position */
  power.sw = gpio_input_data_bit_read(GPIOB, GPIO_PINS_1) == RESET;
  power.percent = battery_percent(power.raw);
  if (power.charging && power.percent > 99u) power.percent = 99;     /* as stock */
}
#endif
