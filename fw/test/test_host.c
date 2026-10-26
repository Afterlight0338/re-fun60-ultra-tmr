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
  req(r, (const uint8_t[]){OP_ENTER_BOOT, 'R', 'E', 'S', 'T'}, 5);
  proto_dispatch(&st, r);
  CHECK(st.restart, "0x7F R E S T requests a plain restart");

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

    /* per-key actuation (v0.13): G at 20 % of an 800-count travel */
    settings_defaults(&settings); keys_init(&k);
    SET_ALL(2400); t = 2000; STEP(); f[G_] = 1600; t = 2010; STEP(); f[G_] = 2400; t = 2020; STEP(); t = 2030; STEP();
    settings.act[G_] = 20;
    f[G_] = 2250; t = 2040; STEP();
    CHECK(rep[2] == 0, "act 20 %: 150 of 800 counts (18 %) is not a press");
    f[G_] = 2230; t = 2050; STEP();
    CHECK(rep[2] == 0x0A, "act 20 %: 170 counts (21 %) presses");
    f[G_] = 2340; t = 2060; STEP(); t = 2070; STEP();
    CHECK(rep[2] == 0x0A, "act 20 %: 7 % is above its release point (20 - 15 = 5 %)");
    f[G_] = 2370; t = 2080; STEP(); t = 2090; STEP();
    CHECK(rep[2] == 0, "act 20 %: 3 % releases");

    /* rapid trigger: 10 % down / 5 % up, the 8 ms hold-off doesn't apply */
    settings.act[G_] = 0; settings.rt_down[G_] = 10; settings.rt_up[G_] = 5;
    f[G_] = 2000; t = 2100; STEP();                                     /* 50 %: first actuation */
    CHECK(rep[2] == 0x0A, "RT: first press at the actuation point");
    f[G_] = 1760; t = 2101; STEP();                                     /* 80 % */
    f[G_] = 1790; t = 2102; STEP();                                     /* 76 %: up 4 */
    CHECK(rep[2] == 0x0A, "RT: rising 4 % from the deepest point stays down");
    f[G_] = 1810; t = 2103; STEP();                                     /* 73 %: up 7 */
    CHECK(rep[2] == 0, "RT: rising 7 % releases at once (no hold-off)");
    f[G_] = 1880; t = 2104; STEP();                                     /* 65 % */
    f[G_] = 1810; t = 2105; STEP();                                     /* 73 %: down 8 */
    CHECK(rep[2] == 0, "RT: falling 8 % from the highest point is not enough");
    f[G_] = 1790; t = 2106; STEP();                                     /* 76 %: down 11 */
    CHECK(rep[2] == 0x0A, "RT: falling 11 % presses again, below the actuation point too");
    f[G_] = 2230; t = 2107; STEP();                                     /* 21 % */
    f[G_] = 2140; t = 2108; STEP();                                     /* 32 %: down 11, above release 35 */
    CHECK(rep[2] == 0, "RT: above the release point it resets, 32 % doesn't press");
    settings.rt_continuous = 1;
    f[G_] = 1600; t = 2109; STEP(); f[G_] = 2230; t = 2110; STEP(); f[G_] = 2140; t = 2111; STEP();
    CHECK(rep[2] == 0x0A, "RT continuous: 21 % -> 32 % presses again");
    f[G_] = 2400; t = 2112; STEP(); f[G_] = 2310; t = 2113; STEP();      /* top, then 11 % */
    CHECK(rep[2] == 0, "RT continuous: back at the top resets, 11 % from rest is no press");

    /* dead zones */
    settings.rt_down[G_] = 0; settings.rt_up[G_] = 0; settings.rt_continuous = 0;
    settings.top_dz = 10; settings.bottom_dz = 10;
    f[G_] = 2330; t = 2120; STEP();
    CHECK(k.pct[G_] == 0, "top dead zone: 8 % reads 0 %");
    f[G_] = 1650; t = 2121; STEP();
    CHECK(k.pct[G_] == 100 && rep[2] == 0x0A, "bottom dead zone: 93 % reads 100 %");
    f[G_] = 2400; t = 2122; STEP(); t = 2140; STEP();
    settings.top_dz = settings.bottom_dz = 0;

    /* saved travel: known from the first press after power-on */
    CHECK(keys_store_travel(&k) >= 1 && settings.span[G_] == 800, "store travel: G's 800 counts");
    keys_init(&k);
    SET_ALL(2400); t = 3000; STEP();
    f[G_] = 2190; t = 3010; STEP();
    CHECK(rep[2] == 0, "saved 800-count travel: 210 counts (26 %) is no press after a reboot");
    f[G_] = 1990; t = 3020; STEP();
    CHECK(rep[2] == 0x0A, "saved travel: 410 counts (51 %) presses");
    keys_init(&k); SET_ALL(1600); f[G_] = 1700; t = 3100; STEP();       /* held since boot */
    CHECK(rep[2] == 0 && k.pct[G_] == 0, "a key held since boot reads 0 % even with saved travel");
    settings_defaults(&settings);

    /* replay of a real capture (fw/test/mag-2026-10-26.csv: magnetic Space, weighted down at
     * the start, lifted at 11.9 s, back down at 16.5 s; magnetic Fn pressed 3 times) */
    {
      FILE *csv = fopen("test/mag-2026-10-26.csv", "r");
      unsigned sp_down = 0, sp_up = 0, fn_down = 0, rows = 0, sp_first_ms = 0, ms, a, b;
      char line[64];
      const unsigned SPC_ = 6 * SCAN_CH + 6;
      keys_init(&k); SET_ALL(1600);
      if (csv && fgets(line, sizeof line, csv)) {
        while (fscanf(csv, "%u,%u,%u", &ms, &a, &b) == 3) {
          uint8_t sp = k.pressed[SPC_], fn = k.pressed[FN_];
          f[SPC_] = (uint16_t)a; f[FN_] = (uint16_t)b; t = 5000 + ms; STEP(); rows++;
          if (!sp && k.pressed[SPC_]) { sp_down++; if (!sp_first_ms) sp_first_ms = ms; }
          if (sp && !k.pressed[SPC_]) sp_up++;
          fn_down += !fn && k.pressed[FN_];
        }
        fclose(csv);
      }
      CHECK(rows > 9000, "replay: capture read");
      CHECK(sp_down == 1 && sp_up == 0 && sp_first_ms > 16000 && sp_first_ms < 17000,
            "replay: Space held at boot never types, presses once when it goes back down");
      CHECK(fn_down == 3, "replay: Fn pressed 3 times, as done");
    }
    SET_ALL(1600); keys_init(&k);
#undef SET_ALL
#undef STEP
  }

  printf("[3b] NKRO and advanced keys (v0.14)\n");
  {
    static keys_t k;
    static uint16_t f[SCAN_SITES];
    uint8_t rep[8], nk[NKRO_LEN];
    const unsigned A_ = 1 * SCAN_CH + 2, D_ = 3 * SCAN_CH + 2, Q_ = 1 * SCAN_CH + 1, CAPS_ = 0 * SCAN_CH + 2,
                   W_ = 2 * SCAN_CH + 1, E_ = 3 * SCAN_CH + 1;
    uint32_t t = 0;
#define SET_ALL(v) for (unsigned i = 0; i < SCAN_SITES; i++) f[i] = (v)
#define STEP() (keys_update(&k, f, t), keys_report(&k, rep), keys_report_nkro(&k, nk))
#define NK(c) (nk[1 + ((c) >> 3)] >> ((c) & 7) & 1)
    settings_defaults(&settings); keys_init(&k);
    SET_ALL(1600); t = 10; STEP();

    CHECK(usb_consumer_report_desc[24] == 0xC0 && usb_consumer_report_desc[CONSUMER_REPORT_DESC_LEN - 1] == 0xC0 &&
          usb_consumer_report_desc[7] == 1 && usb_consumer_report_desc[32] == 2,
          "interface 2: consumer (ID 1) and NKRO keyboard (ID 2) collections, closed");
    for (unsigned c = 1; c <= 10; c++) f[c * SCAN_CH + 1] = 80;          /* Q..P: 10 keys */
    f[0 * SCAN_CH + 3] = 80;                                             /* + Left Shift */
    t = 20; STEP();
    unsigned n = 0;
    for (unsigned c = 0; c < 0x78; c++) n += NK(c);
    CHECK(n == 10 && NK(0x14) && NK(0x13) && nk[0] == 0x02, "NKRO: 10 letters and Shift all in the report");
    CHECK(rep[2] == 0x01, "6KRO report of the same: ErrorRollOver");
    SET_ALL(1600); t = 40; STEP(); t = 60; STEP();
    CHECK(nk[0] == 0 && !NK(0x14), "NKRO: all released");

    /* SOCD A/D, last input wins */
    settings.adv[0] = (adv_t){ .type = ADV_SOCD, .a = A_, .b = D_, .p0 = 0 };
    f[A_] = 80; t = 100; STEP();
    f[D_] = 80; t = 110; STEP();
    CHECK(NK(0x07) && !NK(0x04), "SOCD last: A then D, only D");
    f[D_] = 1600; t = 120; STEP(); t = 130; STEP();
    CHECK(NK(0x04) && !NK(0x07), "SOCD last: letting D go brings A back");
    f[D_] = 80; t = 140; STEP();
    CHECK(NK(0x07) && !NK(0x04), "SOCD last: D again wins again");
    settings.adv[0].p0 = 3; t = 150; STEP();
    CHECK(!NK(0x07) && !NK(0x04), "SOCD neutral: both down, neither");
    settings.adv[0].p0 = 1; t = 160; STEP();
    CHECK(NK(0x04) && !NK(0x07) && rep[2] == 0x04, "SOCD A wins (and the 6KRO report agrees)");
    SET_ALL(1600); t = 170; STEP(); t = 190; STEP();
    settings.adv[0] = (adv_t){0};

    /* mod-tap on Caps: tap = Escape, hold = Left Ctrl */
    settings.adv[1] = (adv_t){ .type = ADV_MODTAP, .a = CAPS_, .p0 = 20, .code = { 0x29, 0xE0 } };
    f[CAPS_] = 80; t = 200; STEP();
    CHECK(nk[0] == 0 && !NK(0x39) && !NK(0x29), "mod-tap: nothing while undecided, never Caps Lock");
    f[CAPS_] = 1600; t = 250; STEP(); t = 258; STEP();                    /* after the 8 ms hold-off */
    CHECK(NK(0x29), "mod-tap: released in 50 ms = Escape tapped");
    t = 277; STEP();
    CHECK(NK(0x29), "mod-tap: the tap stays down for 20 ms (one report on any link)");
    t = 278; STEP();
    CHECK(!NK(0x29), "mod-tap: then comes up");
    f[CAPS_] = 80; t = 300; STEP(); t = 499; STEP();
    CHECK(nk[0] == 0, "mod-tap: still undecided at 199 ms");
    t = 500; STEP();
    CHECK(nk[0] == 0x01 && !NK(0x29), "mod-tap: held 200 ms = Left Ctrl, no Escape");
    f[CAPS_] = 1600; t = 600; STEP(); t = 620; STEP();
    CHECK(nk[0] == 0 && !NK(0x29), "mod-tap: Ctrl released with the key, no Escape after a hold");
    f[CAPS_] = 80; t = 700; STEP(); f[Q_] = 80; t = 710; STEP();
    CHECK(nk[0] == 0x01 && NK(0x14), "mod-tap: another key down while undecided = Ctrl + Q at once");
    SET_ALL(1600); t = 720; STEP(); t = 740; STEP();
    CHECK(nk[0] == 0 && !NK(0x29), "mod-tap: all up, no stray Escape");

    /* toggle on W */
    settings.adv[2] = (adv_t){ .type = ADV_TOGGLE, .a = W_, .code = { 0x1A } };
    f[W_] = 80; t = 800; STEP(); f[W_] = 1600; t = 850; STEP(); t = 900; STEP();
    CHECK(NK(0x1A), "toggle: a tap latches W down");
    f[W_] = 80; t = 1000; STEP();
    CHECK(!NK(0x1A), "toggle: the next press lets it go");
    f[W_] = 1600; t = 1050; STEP(); t = 1070; STEP();
    f[W_] = 80; t = 1100; STEP(); f[W_] = 1600; t = 1500; STEP(); t = 1520; STEP();
    CHECK(!NK(0x1A), "toggle: a long press acts like a normal key");

    /* DKS on E (magnetic travel): F down at press, G tapped at bottom, F up off the bottom, H tapped at release */
    settings.adv[3] = (adv_t){ .type = ADV_DKS, .a = E_, .p0 = 90,
                               .code = { 0x09, 0x0A, 0x0B }, .act = { 0x01 | 0x2 << 4, 0x3 << 2, 0x3 << 6 } };
    keys_init(&k); SET_ALL(2400); t = 2000; STEP();
    f[E_] = 1600; t = 2010; STEP(); f[E_] = 2400; t = 2020; STEP(); t = 2040; STEP();   /* learn 800 */
    f[E_] = 2000; t = 2100; STEP();
    CHECK(NK(0x09) && !NK(0x08), "DKS: F down at the actuation point, E itself never sent");
    f[E_] = 1650; t = 2110; STEP();
    CHECK(NK(0x09) && NK(0x0A), "DKS: G tapped at the bottom (94 %)");
    f[E_] = 1800; t = 2140; STEP();
    CHECK(!NK(0x09) && !NK(0x0A), "DKS: off the bottom: F up, G's tap over");
    f[E_] = 2400; t = 2150; STEP(); t = 2158; STEP();
    CHECK(NK(0x0B), "DKS: H tapped at release");
    t = 2200; STEP();
    CHECK(!NK(0x0B) && nk[0] == 0, "DKS: nothing left down");

    /* macros: "hi" typed by taps, a wait, Shift held over a tap, then the end */
    settings_defaults(&settings); keys_init(&k); SET_ALL(1600); t = 2500; STEP();
    const macro_step_t m0[] = { {0x0B, 3}, {0x0C, 3}, {0, 5}, {0xE1, 1}, {0x04, 3}, {0xE1, 2} };
    for (unsigned i = 0; i < 6; i++) settings.macro[0][i] = m0[i];
    settings.adv[0] = (adv_t){ .type = ADV_MACRO, .a = A_, .p0 = 0 };
    f[A_] = 80; t = 2600; STEP();
    CHECK(NK(0x0B) && !NK(0x0C) && !NK(0x04), "macro: H tapped first, A itself not sent");
    t = 2640; STEP();
    CHECK(!NK(0x0B) && NK(0x0C), "macro: 40 ms later I");
    f[A_] = 1600; t = 2680; STEP();
    CHECK(!NK(0x0C) && nk[0] == 0, "macro: then the 50 ms wait, nothing down (and letting go doesn't stop it)");
    t = 2730; STEP();
    CHECK(nk[0] == 0x02, "macro: Shift down after the wait");
    t = 2750; STEP();
    CHECK(nk[0] == 0x02 && NK(0x04), "macro: Shift + A");
    t = 2790; STEP(); t = 2810; STEP();
    CHECK(nk[0] == 0 && !NK(0x04) && !k.play[0].on, "macro: Shift up, finished");
    settings.macro[0][5] = (macro_step_t){0};                    /* now ends with Shift still down */
    f[A_] = 80; t = 2900; STEP(); f[A_] = 1600; for (t = 2910; t <= 3300; t += 10) STEP();
    CHECK(nk[0] == 0 && !k.play[0].on, "macro: whatever it left down comes up at the end");
    settings.adv[0].b = 1;                                       /* repeat while held */
    unsigned runs = 0, was = 0;
    f[A_] = 80;
    for (t = 3300; t < 3800; t += 5) { STEP(); runs += NK(0x0B) && !was; was = NK(0x0B); }
    f[A_] = 1600;
    for (; t < 4200; t += 5) STEP();
    CHECK(runs >= 2 && !k.play[0].on && nk[0] == 0, "macro: repeats while held, stops after the key is let go");

    /* debug press from the host */
    settings_defaults(&settings); keys_init(&k); SET_ALL(1600); t = 3000; STEP();
    k.force[A_] = 1; t = 3010; STEP();
    CHECK(NK(0x04) && rep[2] == 0x04, "debug press: A down without a reading");
    k.force[A_] = 0; t = 3020; STEP(); t = 3040; STEP();
    CHECK(!NK(0x04), "debug press: let go");
#undef NK
#undef SET_ALL
#undef STEP
  }

  printf("[4] LEDs: encoding and power cap\n");
  {
    static uint8_t out[LED_FRAME_BYTES];
    led_state_t s = { 0, LED_STATIC, 4, 255, 255, 255, 2, {{0}} };
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
    CHECK(s.level == 3 && s.effect == LED_SPECTRUM, "brightness up + next effect");
    s.effect = LED_EFFECTS - 1; leds_apply(&s, LED_ACT_EFFECT);
    CHECK(s.effect == LED_STATIC, "next effect wraps to static");

    /* v0.15 effects; LED n's green byte decoded from the frame */
#define G8(n) ({ unsigned v_ = 0; for (unsigned b_ = 0; b_ < 8; b_++) v_ = v_ << 1 | (out[(n) * 24 + b_] == 0xF0); v_; })
    static uint8_t pressed[SCAN_SITES];
    CHECK(leds_of_site(0) == 0 && leds_of_site(3 * SCAN_CH + 1) == 24 && leds_of_site(0 * SCAN_CH + 2) == 28 &&
          leds_of_site(2 * SCAN_CH + 4) == 54 && leds_of_site(6 * SCAN_CH + 6) == 56 && leds_of_site(12 * SCAN_CH + 4) == 60 &&
          leds_of_site(1 * SCAN_CH + 3) == -1, "LED map: Esc 0, E 24, Caps 28, Win 54, Space 56, RCtrl 60 (stock's), none on an empty site");
    led_state_t e = { 1, LED_SPECTRUM, 2, 0, 0, 0, 2, {{0}} };
    leds_frame(&e, 1000, NULL, out);
    CHECK(memcmp(out, out + 24 * 40, 24) == 0, "spectrum: the whole board one colour");
    e.effect = LED_REACTIVE; e.r = e.g = e.b = 255;
    leds_keys(pressed, 2000);
    leds_frame(&e, 2000, NULL, out);
    CHECK(G8(30) == 0, "reactive: dark before any press");
    pressed[2 * SCAN_CH + 2] = 1; leds_keys(pressed, 2010); pressed[2 * SCAN_CH + 2] = 0; leds_keys(pressed, 2020);   /* S */
    leds_frame(&e, 2020, NULL, out);
    unsigned lit0 = G8(30);
    leds_frame(&e, 2500, NULL, out);
    unsigned lit1 = G8(30);
    leds_frame(&e, 3100, NULL, out);
    CHECK(lit0 > 70 && lit1 > 0 && lit1 < lit0 && G8(30) == 0 && G8(31) == 0, "reactive: S lights, fades, goes dark; D never lit");
    e.effect = LED_RIPPLE;
    leds_frame(&e, 2010, NULL, out);
    unsigned at0 = G8(30), far0 = G8(34);
    leds_frame(&e, 2210, NULL, out);
    CHECK(at0 > 0 && far0 == 0 && G8(34) > 0, "ripple: starts on S, reaches H (4 keys away) 200 ms later");
    e.effect = LED_CUSTOM; e.custom[5][1] = 200;
    leds_frame(&e, 0, NULL, out);
    CHECK(G8(5) > 0 && G8(6) == 0, "custom: only the LED given a colour lights");
#undef G8
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

    /* protocol v6: per-key values, dead zones, saved travel */
    settings_defaults(&settings);
    req(r, (const uint8_t[]){OP_SET_KEYCFG, 1, 0, MAP_PER_PAGE}, 4);
    r[8 + G_] = 70; r[8 + 0] = 12;                                       /* G, Esc */
    proto_dispatch(&st, r);
    CHECK(settings.rt_down[G_] == 50 && settings.rt_down[0] == 12 && r[8 + G_] == 50,
          "SET_KEYCFG rapid trigger: 70 % clamped to 50, reply shows it");
    req(r, (const uint8_t[]){OP_SET_KEYCFG, 0, 0, MAP_PER_PAGE}, 4);
    r[8 + G_] = 2;
    proto_dispatch(&st, r);
    CHECK(settings.act[G_] == 5, "SET_KEYCFG actuation: 2 % raised to 5 (0 means global)");
    req(r, (const uint8_t[]){OP_GET_KEYCFG, 1, 1}, 3);
    proto_dispatch(&st, r);
    CHECK(r[0] == OP_GET_KEYCFG && r[3] == SCAN_SITES - MAP_PER_PAGE, "GET_KEYCFG last page is the remainder");
    req(r, (const uint8_t[]){OP_GET_KEYCFG, 3, 0}, 3);
    proto_dispatch(&st, r);
    CHECK(r[3] == 0, "GET_KEYCFG: a field that doesn't exist returns nothing");

    req(r, (const uint8_t[]){OP_SET_CFG}, 1);
    const uint8_t cfg6[] = { 1, 2, 2, 255, 255, 255, 8, 50, 35, 40, 6, 1 };
    memcpy(r + 8, cfg6, sizeof cfg6);
    proto_dispatch(&st, r);
    CHECK(settings.top_dz == 30 && settings.bottom_dz == 6 && settings.rt_continuous == 1 && r[17] == 30,
          "SET_CFG v6: dead zones (top clamped to 30 %) and continuous rapid trigger");

    static keys_t kk;
    static uint16_t fr[SCAN_SITES];
    keys_init(&kk);
    for (unsigned i = 0; i < SCAN_SITES; i++) fr[i] = 1800;
    keys_update(&kk, fr, 0);
    fr[G_] = 1200; keys_update(&kk, fr, 1);
    st.keys = &kk;
    req(r, (const uint8_t[]){OP_TRAVEL, 1}, 2);
    proto_dispatch(&st, r);
    CHECK(r[1] == 1 && settings.span[G_] == 600, "TRAVEL 1: stores the one key bottomed out (600 counts)");
    req(r, (const uint8_t[]){OP_GET_TRAVEL, G_ / TRAVEL_PER_PAGE}, 2);
    proto_dispatch(&st, r);
    unsigned o = 8 + 4 * (G_ % TRAVEL_PER_PAGE);
    CHECK((r[o] | r[o + 1] << 8) == 1800 && (r[o + 2] | r[o + 3] << 8) == 1200 && (r[3] & 1),
          "GET_TRAVEL: G rest 1800, bottom 1200, page has saved travel");
    req(r, (const uint8_t[]){OP_TRAVEL, 2}, 2);
    proto_dispatch(&st, r);
    CHECK(settings.span[G_] == 0, "TRAVEL 2: saved travel cleared");
    req(r, (const uint8_t[]){OP_TRAVEL, 3}, 2);
    proto_dispatch(&st, r);
    CHECK(kk.rest[G_] == 0 && kk.bottom[G_] == 0xFFFF, "TRAVEL 3: learning starts over");
    settings_defaults(&settings);

    /* protocol v7: advanced keys, NKRO flag, debug press */
    req(r, (const uint8_t[]){OP_SET_ADV, 2, 2}, 3);
    const uint8_t e2[24] = { ADV_SOCD, 9, 25, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                             ADV_MODTAP, 120, 0, 20, 0x29, 0xE0, 0, 0, 0, 0, 0, 0 };   /* site 120: no such key */
    memcpy(r + 8, e2, sizeof e2);
    proto_dispatch(&st, r);
    CHECK(settings.adv[2].type == ADV_SOCD && settings.adv[2].a == 9 && settings.adv[2].b == 25 &&
          settings.adv[3].type == ADV_NONE && r[2] == 2 && r[20] == ADV_NONE,
          "SET_ADV: SOCD stored, an entry on a site that doesn't exist dropped");
    req(r, (const uint8_t[]){OP_GET_ADV, 14}, 2);
    proto_dispatch(&st, r);
    CHECK(r[0] == OP_GET_ADV && r[1] == 14 && r[2] == 2, "GET_ADV from 14: the last 2 entries");
    req(r, (const uint8_t[]){OP_GET_CFG}, 1);
    proto_dispatch(&st, r);
    CHECK(r[20] == 1, "GET_CFG: NKRO on by default");
    req(r, (const uint8_t[]){OP_DEBUG_PRESS, G_, 1}, 3);
    proto_dispatch(&st, r);
    CHECK(kk.force[G_] == 1, "DEBUG_PRESS holds a key");
    req(r, (const uint8_t[]){OP_DEBUG_PRESS, G_, 0}, 3);
    proto_dispatch(&st, r);
    CHECK(kk.force[G_] == 0, "DEBUG_PRESS lets it go");
    req(r, (const uint8_t[]){OP_SET_MACRO, 7, 1, 2}, 4);
    r[8] = 0x04; r[9] = 1; r[10] = 0x04; r[11] = 9;              /* A down, A with a bad arg */
    proto_dispatch(&st, r);
    CHECK(settings.macro[7][16].code == 4 && settings.macro[7][16].arg == 1 && settings.macro[7][17].arg == 3 && r[11] == 3,
          "SET_MACRO 7 page 1: stored at step 16, a bad arg becomes a tap");
    req(r, (const uint8_t[]){OP_GET_MACRO, 8, 0}, 3);
    proto_dispatch(&st, r);
    CHECK(r[3] == 0, "GET_MACRO: macro 8 doesn't exist");
    settings_defaults(&settings);
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
