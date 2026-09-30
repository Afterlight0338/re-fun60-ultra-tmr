/*
 * debounce.c - Per-key eager-press / deferred-release debounce. See debounce.h.
 */
#include "debounce.h"

enum { DB_RELEASED = 0, DB_PRESSED, DB_RELEASING };

void debounce_init(debounce_t *d)
{
  for (unsigned i = 0; i < SCAN_SITES; i++) {
    d->release_since_ms[i] = 0;
    d->state[i] = DB_RELEASED;
  }
}

void debounce_process(debounce_t *d, const uint8_t *raw_pressed, uint32_t now_ms,
                      uint8_t debounce_ms, uint8_t *debounced_pressed)
{
  for (unsigned i = 0; i < SCAN_SITES; i++) {
    uint8_t raw = raw_pressed ? raw_pressed[i] : 0;

    if (raw) {
      d->state[i] = DB_PRESSED;               /* press at once; cancels a pending release */
    } else if (d->state[i] == DB_PRESSED) {
      d->state[i] = DB_RELEASING;
      d->release_since_ms[i] = now_ms;
    }
    /* unsigned subtraction stays correct across the 32-bit millisecond wrap */
    if (d->state[i] == DB_RELEASING &&
        (uint32_t)(now_ms - d->release_since_ms[i]) >= debounce_ms)
      d->state[i] = DB_RELEASED;

    if (debounced_pressed) debounced_pressed[i] = (d->state[i] != DB_RELEASED);
  }
}
