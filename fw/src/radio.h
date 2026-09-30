/*
 * radio.h - link to the PAN1080 radio (2.4G dongle and Bluetooth), reproducing what stock v306
 * sends it over SPI3 (docs/hw-map.md, "Radio link"). The PAN1080 runs its own firmware and keeps
 * its own pairings; the keyboard only tells it which mode to be in and hands it HID reports.
 *
 * Frames, both directions: [cmd][n][n payload bytes][sum of the payload], clocked in one
 * full-duplex transfer rounded up to a multiple of 4 bytes, PA15 low around it. Sent by us:
 *   0x09  poll, 64 zero bytes: the radio's pending reply comes back in the same transfer.
 *         Sent when the radio pulls PD2 low.
 *   0x81  HID report: payload [type][data]; type 1 keyboard [mods][6 keys], type 3 consumer LE16
 *   0x90  battery %        0x92 status request (repeated until a status comes back)
 *   0x93  set mode         0x94 connect / pair (op 1 connect, op 2 + name = Bluetooth pairing)
 * Replies: payload [type]...; type 0x10 is status: [0x10][host LEDs][state][mode].
 * Stock spaces control frames >= 10 ms apart and reports >= 250 us; we use 10 ms and 1 ms.
 */
#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>

#define RADIO_BT1  0u            /* stock's connection modes (byte 0x2000849c + 0x1a) */
#define RADIO_BT2  1u
#define RADIO_BT3  2u
#define RADIO_24G  5u
#define RADIO_USB  6u
#define RADIO_ST_CONNECTED 3u    /* status states (stock FUN_08015764): 0-2 not connected, */
#define RADIO_ST_PAIRING   4u    /* 3 connected, 4 pairing; 5 ends a 2.4G pairing (app_vec28) */
#define RADIO_ST_PAIRED    5u
#define RADIO_XFER_MAX  68u

typedef struct {
  uint8_t mode;                  /* what we asked for (RADIO_*) */
  uint8_t state, radio_mode, host_leds, status_seen;   /* from the last status reply */
  uint8_t pairing_24g;           /* 2.4G pairing in progress (ends connected, or after 30 s) */
  uint16_t tx, rx, bad;          /* frames sent, good replies, replies failing the checksum */
  uint8_t last[3][16];           /* newest first: raw replies to polls, for GET_RADIO */
} radio_t;

extern radio_t radio;

/* Build a frame in buf (RADIO_XFER_MAX bytes); returns the transfer length. Pure. */
unsigned radio_frame(uint8_t *buf, uint8_t cmd, const uint8_t *payload, uint8_t n);
/* Check and act on a poll reply; returns its type, or -1 if it fails the checksum. Pure. */
int radio_parse(const uint8_t *rx);
/* Queue work; the frames go out from radio_task. Pure. */
void radio_set_mode(uint8_t mode);
void radio_pair(void);                         /* Bluetooth: send the name; 2.4G: connect until state 5 */
void radio_keyboard(const uint8_t report[8]);  /* boot report: mods, 0, 6 keys */
void radio_consumer(uint16_t usage);
void radio_battery(uint8_t percent);

#ifndef HOST_TEST
void radio_init(uint8_t mode);
void radio_task(uint32_t now_ms);
#endif

#endif /* RADIO_H */
