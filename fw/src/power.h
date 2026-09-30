/*
 * power.h - battery level, USB power and charger state, read the way stock v306 does
 * (FUN_080151f8, docs/hw-map.md): the battery divider sits on mux channel 7 of the key ADC,
 * sampled every 200 ms and averaged over 8; PC13 high = USB power present (measured: it stays
 * high in all three switch positions with the cable in; stock boots wired on it); PA3 low = the
 * charger IC is charging. Charging itself is done by that IC, not by firmware.
 */
#ifndef POWER_H
#define POWER_H

#include <stdint.h>

#define POWER_PERIOD_MS   200u
#define BATTERY_ABSENT    600u    /* raw below this: no battery (unplugged reads ~69) */

typedef struct {
  uint16_t raw;                   /* averaged ADC counts */
  uint8_t percent;                /* 0 = no battery, else 1..100 */
  uint8_t usb;                    /* PC13 high: USB power present */
  uint8_t charging;
  uint8_t sw;                     /* PB1 low: mode switch at the top (Mac), measured 2026-09-30 */
} power_t;

extern power_t power;

/* Stock's curve: 1200 -> 1 %, 1320 -> 20 %, 1595 -> 100 %, linear in between. Pure. */
uint8_t battery_percent(uint16_t raw);

#ifndef HOST_TEST
void power_task(uint32_t now_ms);
#endif

#endif /* POWER_H */
