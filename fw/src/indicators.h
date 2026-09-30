/*
 * indicators.h - stock v306's LED indicators, drawn over the lighting effect. Pure, host-tested.
 *
 *   Mode key (E R T = Bluetooth 1-3 blue, Y = 2.4G green, U = USB red; colours from stock's
 *   table at RAM 0x20008162, LEDs from 0x0801a238): solid 2 s after switching to USB or on
 *   connecting, slow blink (0.9 s) while a wireless mode is not connected, fast blink (0.1 s)
 *   while the radio is pairing. Radio state from its status reply: 0-2 not connected,
 *   3 connected, 4 pairing (stock FUN_08015764).
 *   2.4G pairing: everything dark except Esc white (stock FUN_08014b24).
 *   Esc: while Fn + Space is held, battery level: < 35 % red, < 55 blue, < 75 magenta, < 95
 *   yellow, else green; red while charging on USB; blinking red (0.4 s) below 21 % on battery
 *   (stock FUN_080148dc).
 *   Caps key white while the host's Caps Lock is on, Win key white while Win lock is on
 *   (stock FUN_08016910).
 */
#ifndef INDICATORS_H
#define INDICATORS_H

#include <stdint.h>
#include "leds.h"

typedef struct {
  uint8_t mode, radio_state, status_seen, pairing_24g;   /* radio.h */
  uint8_t caps, winlock, battery_key;
  uint8_t percent, usb, charging;                        /* power.h */
} ind_in_t;

void indicators(const ind_in_t *in, uint32_t now_ms, led_overlay_t *ov);

#endif /* INDICATORS_H */
