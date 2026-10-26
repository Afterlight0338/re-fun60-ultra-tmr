/*
 * board.h - FUN60 Ultra TMR (dev_id 2307) layout, from stock firmware v306 and measurement.
 * Evidence for every pin is in README section 12; the key positions come from rawscan captures.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

/* 14 columns (shift register) x 8 mux channels. Stock only reads channels 0-4 (its rows 1-5)
 * plus channel 6 on column 6 (the spacebar); we read all 8 so every switch can be found.
 * ponytail: all 8 channels costs ~2x scan time; trim to the used ones once the map is final. */
#define SCAN_COLS   14u
#define SCAN_CH     8u
#define SCAN_SITES  (SCAN_COLS * SCAN_CH)     /* site = col * SCAN_CH + ch */

/* identity reported to ry-flash (GET_INFOR): same dev_id as stock so the flasher's variant
 * check still guards restores; our own version number. */
#define FW_DEV_ID     2307u
#define FW_VER_MAJOR  0u
#define FW_VER_MINOR  17u

/* busy-wait delays on the DWT cycle counter (main.c) */
void delay_us(uint32_t us);
void delay_ms(uint32_t ms);

#endif /* BOARD_H */
