/*
 * test_host.c - host tests for the pure parts: vendor protocol and USB descriptors.
 *   make test
 */
#include <stdio.h>
#include <string.h>
#include "proto.h"
#include "usb_desc.h"
#include "board.h"
#include "keys.h"
#include "leds.h"
#include "settings.h"
#include "power.h"
#include "radio.h"
#include "indicators.h"

static int fails;
#define CHECK(c, msg) do { int ok_ = (c); printf("  %s: %s\n", ok_ ? "ok  " : "FAIL", msg); fails += !ok_; } while (0)

/* request with the Bit7 header checksum, as ry-flash's hdr_bit7() builds it */
static void req(uint8_t *r, const uint8_t *payload, unsigned n)
{
  uint8_t s = 0;
  memset(r, 0, PROTO_REPORT_LEN);
  memcpy(r, payload, n);
  for (unsigned i = 0; i < 7; i++) s = (uint8_t)(s + r[i]);
  r[7] = (uint8_t)(0xFF - s);
}

static int save_ok(void) { return 0; }         /* stands in for settings_save() */

/* colour an overlay puts on led as 0xRRGGBB, or -1 if none (last one wins, as drawn) */
static long ind_lit(const led_overlay_t *ov, unsigned led)
{
  long c = -1;
  for (unsigned i = 0; i < ov->n; i++)
    if (ov->px[i].led == led) c = (long)ov->px[i].r << 16 | ov->px[i].g << 8 | ov->px[i].b;
  return c;
}

int main(void)
{
  proto_state_t st = {0};
  settings_defaults(&settings);
  uint8_t r[PROTO_REPORT_LEN];
  static uint16_t frame[SCAN_SITES];

  printf("[1] vendor protocol\n");
  req(r, (const uint8_t[]){OP_GET_INFOR}, 1);
  CHECK(!st.host_seen, "no host seen before the first request");
  proto_dispatch(&st, r);
  CHECK(st.host_seen, "a valid request proves a host can reach the firmware");
  st.host_seen = 0;
  CHECK(r[0] == 0x8F && (r[1] | r[2] << 8) == 2307, "GET_INFOR: dev_id 2307 where ry-flash reads it");
  CHECK(r[8] == FW_VER_MAJOR && r[7] == FW_VER_MINOR, "GET_INFOR: version in bytes 8 (major) and 7 (minor)");

  req(r, (const uint8_t[]){OP_ENTER_BOOT, 0x55, 0xAA, 0x55, 0x00}, 5);
  proto_dispatch(&st, r);
  CHECK(!st.enter_bootloader, "0x7F with a wrong magic byte is ignored");
  req(r, (const uint8_t[]){OP_ENTER_BOOT, 0x55, 0xAA, 0x55, 0xAA}, 5);
  r[7] ^= 1;
  st.host_seen = 0;
  proto_dispatch(&st, r);
  CHECK(!st.enter_bootloader && r[0] == 0, "bad checksum: ignored, answered with zeros");
  CHECK(!st.host_seen, "bad checksum does not count as a host (failsafe stays armed)");
  req(r, (const uint8_t[]){OP_ISP_PREPARE, 0x3A}, 2);
  proto_dispatch(&st, r);
  CHECK(!st.enter_bootloader, "0xC5 ISP_PREPARE accepted without side effects");
  req(r, (const uint8_t[]){OP_ENTER_BOOT, 0x55, 0xAA, 0x55, 0xAA}, 5);
  proto_dispatch(&st, r);
  CHECK(st.enter_bootloader, "0x7F 55 AA 55 AA requests the bootloader");

  for (unsigned i = 0; i < SCAN_SITES; i++) frame[i] = (uint16_t)(1000 + i);
  st.raw = frame;
  req(r, (const uint8_t[]){OP_GET_RAW, 2}, 2);
  proto_dispatch(&st, r);
  CHECK(r[0] == OP_GET_RAW && r[1] == 2 && r[2] == SCAN_SITES, "GET_RAW echoes op, page, site count");
  CHECK((r[3] | r[4] << 8) == 1060 && (r[3 + 2 * 9] | r[4 + 2 * 9] << 8) == 1069,
        "GET_RAW page 2 = sites 60.., little-endian");
  req(r, (const uint8_t[]){OP_GET_RAW, 3}, 2);
  proto_dispatch(&st, r);
  CHECK((r[3 + 2 * 21] | r[4 + 2 * 21] << 8) == 1000 + SCAN_SITES - 1 &&
        (r[3 + 2 * 22] | r[4 + 2 * 22] << 8) == 0xFFFF, "GET_RAW: last site on page 3, then 0xFFFF");

  printf("[2] USB descriptors (walked as a host would)\n");
  {
    const uint8_t *c = usb_config_desc;
    unsigned i = 0, itfs = 0, hid_with_in = 0, in_hid = 0, has_in = 0;
    CHECK((c[2] | c[3] << 8) == CONFIG_DESC_LEN && c[4] == 3, "config: wTotalLength and 3 interfaces");
    while (i + 1 < CONFIG_DESC_LEN && c[i] >= 2) {
      if (c[i + 1] == 0x04) {
        hid_with_in += in_hid && has_in;
        in_hid = c[i + 5] == 0x03; has_in = 0; itfs++;
      } else if (c[i + 1] == 0x05 && (c[i + 2] & 0x80) && (c[i + 3] & 3) == 3) {
        has_in = 1;
      }
      i += c[i];
    }
    hid_with_in += in_hid && has_in;
    CHECK(i == CONFIG_DESC_LEN, "descriptor chain ends exactly at wTotalLength");
    CHECK(itfs == 3 && hid_with_in == 3, "every HID interface has an interrupt IN (Linux usbhid needs one)");
    for (unsigned itf = 0; itf < 3; itf++) {
      const uint8_t *h = c + HID_DESC_OFFSET(itf);
      unsigned want = itf == 2 ? CONSUMER_REPORT_DESC_LEN : itf ? VENDOR_REPORT_DESC_LEN : KBD_REPORT_DESC_LEN;
      CHECK(h[1] == 0x21 && (unsigned)(h[7] | h[8] << 8) == want, itf == 2 ? "IF2 HID descriptor points at the consumer report" : itf ? "IF1 HID descriptor points at the vendor report"
                                                               : "IF0 HID descriptor points at the keyboard report");
    }
    CHECK(usb_kbd_report_desc[KBD_REPORT_DESC_LEN - 1] == 0xC0 &&
          usb_vendor_report_desc[VENDOR_REPORT_DESC_LEN - 1] == 0xC0, "report descriptors end with End Collection");
    CHECK(usb_vendor_report_desc[0] == 0x06 && usb_vendor_report_desc[1] == 0xFF &&
          usb_vendor_report_desc[2] == 0xFF && usb_vendor_report_desc[4] == 0x02,
          "vendor interface is usage page 0xFFFF / usage 2 (what ry-flash opens)");
  }

  printf("[3] keys: threshold, arming, debounce, keymap\n");
  {
    static keys_t k;
    static uint16_t f[SCAN_SITES];
    uint8_t rep[8];
    const unsigned G_ = 5 * SCAN_CH + 2, ESC_ = 0, N1_ = 1 * SCAN_CH, FN_ = 10 * SCAN_CH + 4,
                   LSFT_ = 0 * SCAN_CH + 3, NOKEY = 1 * SCAN_CH + 3;
    uint32_t t = 0;
#define SET_ALL(v) for (unsigned i = 0; i < SCAN_SITES; i++) f[i] = (v)
#define STEP() (keys_update(&k, f, t), keys_report(&k, rep))

    keys_init(&k);
    SET_ALL(1600);
    f[ESC_] = 80;                         /* held (or stuck) since power-on */
    STEP();
    CHECK(rep[2] == 0, "a key down since boot is not reported");
    f[G_] = 80; t = 10; STEP();
    CHECK(rep[2] == 0x0A && rep[3] == 0, "G pressed: reported at once as 0x0A");
    f[G_] = 800; t = 11; STEP();
    CHECK(rep[2] == 0x0A, "hysteresis: 800 counts keeps a pressed key down");
    f[G_] = 1600; t = 50; STEP(); f[G_] = 80; t = 53; STEP();
    CHECK(rep[2] == 0x0A, "3 ms mid-hold dropout (harsh tap) swallowed");
    f[G_] = 1600; t = 100; STEP(); t = 107; STEP();
    CHECK(rep[2] == 0x0A, "release held back for 8 ms");
    t = 108; STEP();
    CHECK(rep[2] == 0, "release reported after 8 ms");
    f[ESC_] = 1600; t = 110; STEP(); f[ESC_] = 80; t = 120; STEP();
    CHECK(rep[2] == 0x29, "the stuck-at-boot key counts once it has been released");
    f[ESC_] = 1600; t = 130; STEP(); t = 140; STEP();
    f[NOKEY] = 80; t = 150; STEP();
    CHECK(rep[2] == 0, "site with no switch (column 1, channel 3) never produces a key");
    f[FN_] = 80; f[N1_] = 80; t = 160; STEP();
    CHECK(rep[2] == 0x3A && rep[3] == 0, "Fn + 1 = F1, Fn itself not sent");
    f[FN_] = f[N1_] = 1600; t = 170; STEP(); t = 180; STEP();
    f[LSFT_] = 80; t = 190; STEP();
    CHECK(rep[0] == 0x02 && rep[2] == 0, "Left Shift is modifier bit 1");
    for (unsigned c = 1; c <= 7; c++) f[c * SCAN_CH + 1] = 80;   /* Q W E R T Y U */
    t = 200; STEP();
    CHECK(rep[2] == 0x01 && rep[7] == 0x01 && rep[0] == 0x02, "7 keys: ErrorRollOver, modifiers kept");
    SET_ALL(1600); t = 300; STEP(); t = 320; STEP();                    /* all released */
    f[FN_] = 80; f[11 * SCAN_CH + 3] = 80; t = 330; STEP();              /* Fn + / */
    CHECK(rep[2] == 0x52, "Fn + / = Up arrow");
    f[11 * SCAN_CH + 3] = 1600; f[9 * SCAN_CH + 4] = 80; t = 340; STEP(); t = 350; STEP();  /* Fn + RAlt */
    CHECK(rep[2] == 0x50 && rep[0] == 0, "Fn + RAlt = Left arrow, not the RAlt modifier");
    f[9 * SCAN_CH + 4] = 1600; f[10 * SCAN_CH + 3] = 80; t = 360; STEP(); t = 370; STEP();  /* Fn + . */
    CHECK(keys_consumer(&k) == 0x00E9 && rep[2] == 0, "Fn + . = Volume Up on the consumer interface only");
    f[10 * SCAN_CH + 3] = 1600; t = 380; STEP(); t = 390; STEP();
    CHECK(keys_consumer(&k) == 0, "consumer usage released with the key");
    (void)keys_take_actions(&k);
    f[13 * SCAN_CH + 2] = 80; t = 400; STEP();                           /* Fn + Enter */
    CHECK(keys_take_actions(&k) == LED_ACT_TOGGLE && keys_take_actions(&k) == 0,
          "Fn + Enter = one lighting toggle per press");
    CHECK(rep[2] == 0, "Fn + Enter sends no Enter");
    f[13 * SCAN_CH + 2] = 1600; t = 410; STEP(); t = 420; STEP();
    f[3 * SCAN_CH + 1] = 80; t = 430; STEP();                            /* Fn + E */
    CHECK(keys_held(&k, KC_BT1, KC_USB) == KC_BT1 && rep[2] == 0, "Fn + E = Bluetooth 1 key, nothing typed");
    f[3 * SCAN_CH + 1] = 1600; t = 440; STEP(); t = 450; STEP();
    f[2 * SCAN_CH + 4] = 80; t = 460; STEP();                            /* Fn + Win */
    f[2 * SCAN_CH + 4] = 1600; f[FN_] = 1600; t = 470; STEP(); t = 480; STEP();
    f[2 * SCAN_CH + 4] = 80; t = 490; STEP();                            /* Win alone */
    CHECK(k.winlock && rep[0] == 0, "Fn + Win locks the Win key");

    /* magnetic: rest 2400, never below ~1500; uncalibrated travel is stock's 400 counts */
    keys_init(&k); k.winlock = 0;
    SET_ALL(2400); t = 1000; STEP();
    f[G_] = 2250; t = 1010; STEP();
    CHECK(rep[2] == 0, "magnetic: 150 of 400 counts (37%) is not a press yet");
    f[G_] = 2190; t = 1020; STEP();
    CHECK(rep[2] == 0x0A, "magnetic: 210 counts (52%) presses before any calibration");
    f[G_] = 1600; t = 1030; STEP(); f[G_] = 2000; t = 1040; STEP();
    CHECK(rep[2] == 0x0A, "bottomed at 1600 (800 travel): 50% still held");
    f[G_] = 2150; t = 1050; STEP(); t = 1060; STEP();
    CHECK(rep[2] == 0, "released under 35% of the learned 800-count travel");
    f[G_] = 2190; t = 1070; STEP();
    CHECK(rep[2] == 0, "after calibration 210 counts (26%) is no longer a press");
    f[G_] = 1990; t = 1080; STEP();
    CHECK(rep[2] == 0x0A, "410 counts (51%) of the learned travel presses");
    SET_ALL(1600); keys_init(&k);
#undef SET_ALL
#undef STEP
  }

  printf("[4] LEDs: encoding and power cap\n");
  {
    static uint8_t out[LED_FRAME_BYTES];
    led_state_t s = { 0, LED_STATIC, 4, 255, 255, 255 };
    unsigned all_zero_bits = 1;
    leds_frame(&s, 0, NULL, out);
    for (unsigned i = 0; i < LED_FRAME_BYTES; i++) all_zero_bits &= out[i] == 0xC0;
    CHECK(all_zero_bits, "lights off: every LED bit is sent as 0 (0xC0)");

    s.on = 1; s.level = 0;                          /* white at 16 */
    uint32_t ma = leds_frame(&s, 0, NULL, out);
    const uint8_t sixteen[8] = { 0xC0, 0xC0, 0xC0, 0xF0, 0xC0, 0xC0, 0xC0, 0xC0 };
    CHECK(memcmp(out, sixteen, 8) == 0 && memcmp(out + 8, sixteen, 8) == 0,
          "value 16 -> 00010000 MSB first, 0xF0 for a 1 bit, G then R");
    CHECK(ma < LED_BUDGET_MA, "dim white stays under the current budget");

    s.level = LED_LEVELS - 1;                       /* full white would be ~3.7 A */
    ma = leds_frame(&s, 0, NULL, out);
    unsigned g = 0;
    for (unsigned b = 0; b < 8; b++) g = g << 1 | (out[b] == 0xF0);
    CHECK(ma == LED_BUDGET_MA && g < 60, "full white is scaled down to the current budget");

    s.effect = LED_RAINBOW; s.level = 2;
    leds_frame(&s, 0, NULL, out);
    CHECK(memcmp(out, out + 24 * 20, 24) != 0, "rainbow: LEDs 0 and 20 differ");
    leds_apply(&s, LED_ACT_BRI_UP | LED_ACT_EFFECT);
    CHECK(s.level == 3 && s.effect == LED_STATIC, "brightness up + next effect (wraps to static)");
  }

  printf("[5] driver protocol v1\n");
  {
    const unsigned G_ = 5 * SCAN_CH + 2, FN_ = 10 * SCAN_CH + 4;
    static uint8_t pressed[SCAN_SITES];

    settings_defaults(&settings);
    req(r, (const uint8_t[]){OP_GET_INFOR}, 1);
    proto_dispatch(&st, r);
    CHECK(r[3] == PROTO_VERSION, "GET_INFOR reports protocol version (driver checks it)");

    req(r, (const uint8_t[]){OP_GET_MAP, 0, 0}, 3);
    proto_dispatch(&st, r);
    CHECK(r[0] == OP_GET_MAP && r[3] == MAP_PER_PAGE && r[8 + G_] == 0x0A, "GET_MAP base page 0 holds G at its site");
    req(r, (const uint8_t[]){OP_GET_MAP, 1, 1}, 3);
    proto_dispatch(&st, r);
    CHECK(r[3] == SCAN_SITES - MAP_PER_PAGE, "GET_MAP last page is the remainder");

    uint8_t page[MAP_PER_PAGE];
    for (unsigned i = 0; i < MAP_PER_PAGE; i++) page[i] = settings.keymap[0][i];
    page[G_] = 0x0B;
    req(r, (const uint8_t[]){OP_SET_MAP, 0, 0, MAP_PER_PAGE}, 4);
    memcpy(r + 8, page, MAP_PER_PAGE);
    proto_dispatch(&st, r);
    CHECK(settings.keymap[0][G_] == 0x0B && r[8 + G_] == 0x0B, "SET_MAP remaps G to H and echoes it");
    req(r, (const uint8_t[]){OP_SET_MAP, 2, 0, MAP_PER_PAGE}, 4);
    proto_dispatch(&st, r);
    CHECK(r[3] == 0, "SET_MAP to a layer that doesn't exist changes nothing");

    req(r, (const uint8_t[]){OP_SET_CFG}, 1);
    const uint8_t cfg[] = { 1, 9, 7, 10, 20, 30, 99, 60, 58 };   /* press 60%, release 58% */
    memcpy(r + 8, cfg, sizeof cfg);
    proto_dispatch(&st, r);
    CHECK(settings.led.effect == LED_STATIC && settings.led.level == LED_LEVELS - 1 && settings.debounce_ms == 50,
          "SET_CFG clamps effect, brightness and hold-off from the host");
    CHECK(settings.press_pct == 60 && settings.release_pct == 55 && r[16] == 55,
          "release kept 5 points under press, reply shows what was stored");
    CHECK(settings.led.r == 10 && settings.led.g == 20 && settings.led.b == 30, "static colour stored");

    pressed[G_] = pressed[FN_] = 1;
    st.pressed = pressed;
    req(r, (const uint8_t[]){OP_GET_STATE}, 1);
    proto_dispatch(&st, r);
    CHECK((r[8 + G_ / 8] >> (G_ % 8) & 1) && (r[8 + FN_ / 8] >> (FN_ % 8) & 1) && r[2] == 1,
          "GET_STATE: G and Fn in the bitmap, Fn flag set");

    req(r, (const uint8_t[]){OP_SAVE}, 1);
    proto_dispatch(&st, r);
    CHECK(r[1] == 0, "SAVE without a flash writer reports failure");
    st.save = save_ok;
    req(r, (const uint8_t[]){OP_SAVE}, 1);
    proto_dispatch(&st, r);
    CHECK(r[1] == 1, "SAVE reports success when the flash write succeeds");

    req(r, (const uint8_t[]){OP_DEFAULTS}, 1);
    proto_dispatch(&st, r);
    CHECK(settings.keymap[0][G_] == 0x0A && settings.debounce_ms == 8 && settings.led.effect == LED_RAINBOW,
          "DEFAULTS restores the stock layout, 8 ms hold-off, rainbow");

    power = (power_t){.raw = 1400, .percent = 49, .usb = 1, .charging = 1};
    req(r, (const uint8_t[]){OP_GET_POWER}, 1);
    proto_dispatch(&st, r);
    CHECK(r[0] == OP_GET_POWER && r[1] == 49 && r[2] == 3 && (r[3] | r[4] << 8) == 1400,
          "GET_POWER: percent, USB + charging flags, raw");

    st.set_mode = PROTO_NO_MODE;
    req(r, (const uint8_t[]){OP_SET_RADIO, 5, 1}, 3);
    proto_dispatch(&st, r);
    CHECK(r[0] == OP_SET_RADIO && st.set_mode == 5 && st.pair == 1, "SET_RADIO 5 1: 2.4G + pair requested");
    st.set_mode = PROTO_NO_MODE;
    req(r, (const uint8_t[]){OP_SET_RADIO, 3, 0}, 3);
    proto_dispatch(&st, r);
    CHECK(st.set_mode == PROTO_NO_MODE, "SET_RADIO ignores modes stock doesn't have (3, 4)");
  }

  /* battery curve (stock v306): 1200 -> 1 %, 1320 -> 20 %, 1595 -> 100 % */
  CHECK(battery_percent(69) == 0, "battery unplugged (reads ~69) = 0 %, no battery");
  CHECK(battery_percent(1100) == 1 && battery_percent(1200) == 1, "below 1200: 1 %");
  CHECK(battery_percent(1320) == 20 && battery_percent(1595) == 100 && battery_percent(4095) == 100,
        "20 % at 1320, 100 % from 1595");
  {
    int mono = 1;
    for (unsigned v = BATTERY_ABSENT; v < 1700; v++)
      mono &= battery_percent((uint16_t)v) <= battery_percent((uint16_t)(v + 1));
    CHECK(mono, "battery curve never goes down as the voltage goes up");
  }

  /* indicators (stock v306 FUN_080148dc, FUN_08016910, FUN_08015764, FUN_08014b24) */
  {
    led_overlay_t ov;
    ind_in_t in = { .mode = RADIO_USB, .percent = 70, .usb = 1 };
#define LIT(led) ind_lit(&ov, led)
    indicators(&in, 1000, &ov);
    CHECK(LIT(20) == 0xFF0000, "boot in USB mode: U red");
    indicators(&in, 3100, &ov);
    CHECK(LIT(20) < 0, "the mode colour goes after 2 s");
    in.mode = RADIO_24G;
    indicators(&in, 4000, &ov);
    int on = LIT(21) == 0x00FF00;
    indicators(&in, 4950, &ov);
    CHECK(on && LIT(21) < 0 && LIT(20) < 0, "2.4G, not connected yet: Y green, slow blink");
    in.status_seen = 1; in.radio_state = 4;
    indicators(&in, 5000, &ov);
    on = LIT(21) >= 0;
    indicators(&in, 5150, &ov);
    CHECK(on && LIT(21) < 0, "radio pairing (state 4): fast blink");
    in.radio_state = 3;
    indicators(&in, 5200, &ov);
    CHECK(LIT(21) == 0x00FF00, "connected (state 3): solid");
    in.caps = 1; in.charging = 1;
    indicators(&in, 5300, &ov);
    CHECK(LIT(LED_CAPS) == 0xFFFFFF && LIT(LED_ESC) == 0xFF0000, "Caps Lock white, charging Esc red");
    in.caps = 0; in.charging = 0; in.usb = 0; in.battery_key = 1; in.percent = 50;
    indicators(&in, 5400, &ov);
    CHECK(LIT(LED_ESC) == 0x0000FF, "Fn + Space at 50 %: Esc blue");
    in.pairing_24g = 1;
    indicators(&in, 5500, &ov);
    CHECK(ov.blank && ov.n == 1 && LIT(LED_ESC) == 0xFFFFFF, "2.4G pairing: all dark, Esc white");
#undef LIT
  }

  /* radio link frames (stock v306 FUN_08015c80 / app_vec28) */
  {
    uint8_t f[RADIO_XFER_MAX], rx[RADIO_XFER_MAX] = {0};
    const uint8_t kb[8] = {1, 0x02, 0x04, 0x05, 0, 0, 0, 0};          /* type 1, LShift, A, B */
    unsigned n = radio_frame(f, 0x81, kb, 8);
    CHECK(n == 12 && f[0] == 0x81 && f[1] == 8 && f[2] == 1 && f[3] == 2 && f[4] == 4 &&
          f[10] == (uint8_t)(1 + 2 + 4 + 5) && f[11] == 0,
          "keyboard frame: 81 08 01 mods keys.. sum, 12-byte transfer (stock rounds to 4)");
    const uint8_t zeros[64] = {0};
    CHECK(radio_frame(f, 0x09, zeros, 64) == 68 && f[0] == 9 && f[66] == 0, "poll: 0x09 + 64 zeros, 68 bytes");
    const uint8_t mode5 = 5;
    CHECK(radio_frame(f, 0x93, &mode5, 1) == 4 && f[2] == 5 && f[3] == 5, "set mode: 93 01 05 05");

    radio_set_mode(RADIO_24G);
    rx[0] = 0x09; rx[1] = 4; rx[2] = 0x10; rx[3] = 0x02; rx[4] = 5; rx[5] = 5;
    rx[6] = (uint8_t)(0x10 + 0x02 + 5 + 5);
    CHECK(radio_parse(rx) == 0x10 && radio.state == 5 && radio.radio_mode == 5 && radio.host_leds == 2 &&
          radio.status_seen, "status reply: state, mode and Caps Lock parsed");
    rx[6] ^= 1;
    uint16_t bad = radio.bad;
    CHECK(radio_parse(rx) == -1 && radio.bad == bad + 1, "status reply with a bad checksum is dropped");
    memset(rx, 0xFF, sizeof rx);
    CHECK(radio_parse(rx) == -1 && radio.last[0][0] == 0xFF, "no radio (all 0xFF) is rejected but kept for GET_RADIO");
  }

  printf("\n%s (%d failure%s)\n", fails ? "TESTS FAILED" : "ALL TESTS PASSED", fails, fails == 1 ? "" : "s");
  return fails != 0;
}
