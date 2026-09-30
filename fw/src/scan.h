/*
 * scan.h - board power-up and the TMR sensor matrix scan (column shift register + row mux
 * into ADC1 channel 2), mirroring stock v306 step for step. See docs/hw-map.md.
 */
#ifndef SCAN_H
#define SCAN_H

#include <stdint.h>
#include "board.h"

/* Pins, power rails (PC14 -> 20 ms -> PC5 -> 100 ms -> PA7) and the ADC. Takes ~125 ms. */
void scan_init(void);

/* Read every site once into raw[SCAN_SITES] (12-bit ADC counts). */
void scan_frame(uint16_t *raw);

/* One reading of the battery divider (mux channel 7), for power.c. */
uint16_t scan_battery(void);

#endif /* SCAN_H */
