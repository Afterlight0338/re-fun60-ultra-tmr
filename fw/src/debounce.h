/*
 * debounce.h - Per-key eager-press / deferred-release debounce (QMK asym_eager_defer_pk).
 *
 * - Press: reported the instant the engine sees it (0 ms press latency).
 * - Release: reported only after the key has read released for `debounce_ms` without
 *   interruption; any re-press inside that window cancels it.
 * - debounce_ms = 0: pure pass-through.
 *
 * Why this shape (scripts/chatter_test.py logs, 2026-09-28, mechanical switches on the
 * FUN60 Ultra TMR): harsh taps make a held key drop out for 0.1-6.4 ms at any point of the
 * hold, not just after the press edge, so a lockout after each edge (the previous eager
 * design) let 21 of 163 through on one log. Holding the release for 8 ms removed all of
 * them in replay, with no added press latency and no real tap lost (fastest real re-press
 * seen: ~15 ms after release).
 *
 * ponytail: one global debounce_ms, so magnetic/rapid-trigger keys also get releases
 * debounce_ms late; make it per-key once each key's switch type (mechanical vs magnetic)
 * is known.
 */
#ifndef DEBOUNCE_H
#define DEBOUNCE_H

#include <stdint.h>
#include "board.h"

typedef struct {
  uint32_t release_since_ms[SCAN_SITES];   /* when a held key first read released   */
  uint8_t  state[SCAN_SITES];              /* DB_RELEASED / DB_PRESSED / DB_RELEASING */
} debounce_t;

/* Initialise debounce state for all keys to released. */
void debounce_init(debounce_t *d);

/* Process one frame of raw key-press states.
 * raw_pressed: SCAN_SITES entries (0 or 1), threshold states from keys.c.
 * now_ms: current monotonic timestamp in milliseconds (wraparound-safe).
 * debounce_ms: release hold-off in milliseconds (st->cfg[0x09]).
 * debounced_pressed: destination, SCAN_SITES entries (0 or 1).
 */
void debounce_process(debounce_t *d, const uint8_t *raw_pressed, uint32_t now_ms,
                      uint8_t debounce_ms, uint8_t *debounced_pressed);

#endif /* DEBOUNCE_H */
