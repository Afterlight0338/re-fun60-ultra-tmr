/*
 * main.c - open firmware for the MonsGeek FUN60 Ultra TMR (RY5088 / AT32F405, dev_id 2307).
 *
 * Powers the sensor matrix, scans it continuously, turns it into boot keyboard and consumer
 * (media) reports (keys.c: threshold + eager-press / deferred-release debounce + keymap), drives
 * the per-key LEDs (leds.c), and exposes the raw readings over the vendor HID interface
 * (proto.h, OP_GET_RAW) for mapping and tuning. Runs in the app slot behind the factory RY bootloader, which ry-flash
 * talks to; see docs/hw-map.md for where every hardware detail comes from.
 */
#include "at32f402_405.h"
#include "at32f402_405_clock.h"
#include "usb_conf.h"
#include "usb_core.h"
#include "usbd_int.h"
#include "board.h"
#include "scan.h"
#include "usb_desc.h"
#include "usb_hid.h"
#include "keys.h"
#include "leds.h"
#include "settings.h"
#include "power.h"
#include "radio.h"
#include "indicators.h"

otg_core_type otg_core_struct;
static uint16_t raw[SCAN_SITES];
static keys_t keys;

/* ---- delays on the DWT cycle counter (runs from reset, at any clock) ---- */
void delay_us(uint32_t us)
{
  uint32_t start = DWT->CYCCNT, n = us * (system_core_clock / 1000000u);
  while (DWT->CYCCNT - start < n) { }
}
void delay_ms(uint32_t ms)       { while (ms--) delay_us(1000); }
void usb_delay_us(uint32_t us)   { delay_us(us); }
void usb_delay_ms(uint32_t ms)   { delay_ms(ms); }

static uint32_t now_ms(void)     /* wraps after ~19.9 s of cycles; call often */
{
  static uint32_t last, ms, rem;
  uint32_t c = DWT->CYCCNT, per = system_core_clock / 1000u;
  rem += c - last;
  last = c;
  ms += rem / per;
  rem %= per;
  return ms;
}

/* ---- factory bootloader handoff ----
 * The RY bootloader stays in DFU (3151:502A) only while the word at 0x08004800 is
 * 0x55AA55AA; that sector holds nothing else. Same erase / program / verify / reset
 * sequence as stock v306 (@0x080129cc). */
#define BOOT_MAILBOX_ADDR   0x08004800u
#define BOOT_MAILBOX_MAGIC  0x55AA55AAu

static void enter_bootloader(void)
{
  delay_ms(20);                                   /* let the SET_REPORT status stage finish */
  flash_unlock();
  for (int tries = 0; tries < 10; tries++) {
    if (flash_sector_erase(BOOT_MAILBOX_ADDR) == FLASH_OPERATE_DONE &&
        flash_word_program(BOOT_MAILBOX_ADDR, BOOT_MAILBOX_MAGIC) == FLASH_OPERATE_DONE &&
        *(volatile uint32_t *)BOOT_MAILBOX_ADDR == BOOT_MAILBOX_MAGIC)
      break;
  }
  flash_lock();
  NVIC_SystemReset();
}

/* ---- failsafe: a bad build must never need the BOOT1 wire ----
 * Hang: the watchdog (own LICK clock) resets the chip; two watchdog resets in a row go to the
 * bootloader before any init runs. Only watchdog resets count (the factory bootloader leaves
 * the reset flags alone: its one CTRLSTS write, @0x08000af8, is LICKEN), so re-plugging never
 * trips it; 60 s of running clears it.
 * No host: until this image is "proven", no valid vendor request within FAILSAFE_HOST_MS goes
 * to the bootloader. Proven = a host talked to the vendor interface once (ry-flash's post-flash
 * GET_INFOR does it); the image's fingerprint is then kept in flash, so a working build acts
 * like a normal keyboard on a sleeping PC or a charger, and any new image starts unproven. */
#define FAILSAFE_MAGIC     0x5AFEB007u
#define FAILSAFE_WDT_BOOTS 2u
#define FAILSAFE_HOST_MS   10000u
#define HEALTHY_MS         60000u
#define PROVEN_ADDR        0x08027800u  /* last page below stock's settings (0x08028000); app.ld
                                         * keeps the image under it */

static volatile uint32_t fs_magic __attribute__((section(".noinit")));
static volatile uint32_t fs_boots __attribute__((section(".noinit")));

/* FNV-1a over the whole flashed image (code + initialised data) */
static uint32_t image_fingerprint(void)
{
  extern const uint8_t g_pfnVectors[];
  extern uint32_t _sidata, _sdata, _edata;
  const uint8_t *p = g_pfnVectors;
  const uint8_t *end = (const uint8_t *)&_sidata + ((uint8_t *)&_edata - (uint8_t *)&_sdata);
  uint32_t h = 0x811C9DC5u;
  while (p < end) h = (h ^ *p++) * 0x01000193u;
  return h == 0xFFFFFFFFu ? 0 : h;                /* never equal to erased flash */
}

static void mark_proven(uint32_t fp)
{
  flash_unlock();
  if (flash_sector_erase(PROVEN_ADDR) == FLASH_OPERATE_DONE) flash_word_program(PROVEN_ADDR, fp);
  flash_lock();
}

static void failsafe_boot(void)
{
  int wdt_reset = crm_flag_get(CRM_WDT_RESET_FLAG) == SET;
  crm_flag_clear(CRM_ALL_RESET_FLAG);             /* next boot sees only its own cause */
  if (fs_magic != FAILSAFE_MAGIC || !wdt_reset) { fs_magic = FAILSAFE_MAGIC; fs_boots = 0; }
  else if (++fs_boots >= FAILSAFE_WDT_BOOTS) { fs_boots = 0; enter_bootloader(); }
  wdt_register_write_enable(TRUE);               /* LICK 30-60 kHz: /64, 3000 = 3.2-6.4 s */
  wdt_divider_set(WDT_CLK_DIV_64);
  wdt_reload_value_set(3000 - 1);
  wdt_counter_reload();
  wdt_enable();
}

/* ---- hold Esc while plugging in: bootloader, no tools or case opening needed ----
 * Esc (site 0) must read pressed and "1" (site 8) at rest on 5 scans in a row, so a matrix
 * that reads all-low (rails not up, broken scan) can't send every boot to the bootloader. */
#define BOOTKEY_SITE   0u
#define BOOTKEY_REF    8u
/* fixed counts (v0.11's mechanical thresholds): no calibration exists yet at boot.
 * ponytail: a magnetic Esc may never read this low; then use the watchdog failsafe. */
#define BOOTKEY_PRESS_BELOW  700u
#define BOOTKEY_REST_ABOVE   1000u

static void bootkey_check(void)
{
  for (int i = 0; i < 5; i++) {
    scan_frame(raw);
    if (raw[BOOTKEY_SITE] >= BOOTKEY_PRESS_BELOW || raw[BOOTKEY_REF] <= BOOTKEY_REST_ABOVE)
      return;
  }
  enter_bootloader();
}

/* ---- connection mode: USB power, then the Fn keys (E R T Y U), as stock ----
 * At boot (stock FUN_08014ef4): cable in = USB, else the last wireless mode (settings.conn_mode,
 * Bluetooth 1 at first). Unplugging while on USB falls back to the last wireless mode; plugging
 * in keeps the current one, so wireless works while charging. Fn + E R T Y pick and remember a
 * wireless mode; Fn + U goes to USB, only with USB power (stock). Holding a wireless key
 * PAIR_HOLD_MS pairs (radio.h). The switch under Caps Lock is not read for this: its middle
 * position disconnects the battery, top / bottom only differ on PB1 (Mac / Windows). */
#define PAIR_HOLD_MS  3000u
#define LED_IDLE_MS   300000u         /* on battery: LEDs off after 5 min without a key */

static uint8_t conn_mode;

static void mode_follow_usb(void)
{
  static int usb = -1;
  if (power.usb == usb) return;
  if (usb < 0) conn_mode = power.usb ? RADIO_USB : settings.conn_mode;
  else if (!power.usb && conn_mode == RADIO_USB) conn_mode = settings.conn_mode;
  usb = power.usb;
}

/* Fn + U, or the host: USB only with USB power (stock). Wireless modes are remembered. */
static void mode_select(uint8_t m)
{
  if (m == RADIO_USB) {
    if (power.usb) conn_mode = RADIO_USB;
    return;
  }
  conn_mode = m;
  if (settings.conn_mode != m) {
    settings.conn_mode = m;
    settings_save();                               /* ~20 ms; mode changes are rare */
  }
}

static void mode_keys(uint32_t now)
{
  static const uint8_t mode_of[] = { RADIO_BT1, RADIO_BT2, RADIO_BT3, RADIO_24G, RADIO_USB };
  static uint8_t held, paired;
  static uint32_t since;
  uint8_t k = keys_held(&keys, KC_BT1, KC_USB);

  if (k != held) {
    held = k;
    since = now;
    paired = 0;
    if (k) mode_select(mode_of[k - KC_BT1]);
  } else if (k && k != KC_USB && !paired && now - since >= PAIR_HOLD_MS) {
    paired = 1;
    radio_pair();
  }
  if (hid_proto.set_mode != PROTO_NO_MODE) {       /* SET_RADIO from the host */
    mode_select(hid_proto.set_mode);
    hid_proto.set_mode = PROTO_NO_MODE;
    if (hid_proto.pair) { radio_set_mode(conn_mode); radio_pair(); }
    hid_proto.pair = 0;
  }
}

/* ---- USB ---- */
void OTG_IRQ_HANDLER(void) { usbd_irq_handler(&otg_core_struct); }

static void usb_start(void)
{
  crm_periph_clock_enable(OTG_CLOCK, TRUE);
  crm_pllu_output_set(TRUE);                      /* 48 MHz from PLLU, as Artery's example */
  while (crm_flag_get(CRM_PLLU_STABLE_FLAG) != SET) { }
  crm_usb_clock_source_select(CRM_USB_CLOCK_SOURCE_PLLU);
  nvic_irq_enable(OTG_IRQ, 0, 0);
  usbd_init(&otg_core_struct, USB_SPEED_CORE_ID, USB_ID, &hid_class_handler, &usb_desc_handler);
}

int main(void)
{
  int report_due = 0, consumer_due = 0, proven, battery_sent = -1, report_changed;
  uint32_t last_key_ms = 0;
  led_overlay_t overlay;
  uint16_t consumer = 0;
  uint32_t fingerprint;
  uint8_t report[KBD_REPORT_LEN], nkro[NKRO_LEN];
  static uint8_t sent_kbd[KBD_REPORT_LEN], sent_nkro[NKRO_LEN];   /* what the host has now (USB) */

  nvic_vector_table_set(NVIC_VECTTAB_FLASH, 0x5200);   /* as stock main @0x08014b90 */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  failsafe_boot();
  fingerprint = image_fingerprint();
  proven = *(volatile uint32_t *)PROVEN_ADDR == fingerprint;

  nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);
  system_clock_config();                           /* 216 MHz from the 12 MHz crystal */
  scan_init();                                     /* pins, power rails, ADC */
  leds_init();                                     /* after the rails: the LEDs run off them */
  settings_load();                                 /* driver settings saved in flash, or defaults */
  bootkey_check();
  keys_init(&keys);
  power_task(now_ms());                            /* USB power, battery */
  mode_follow_usb();
  radio_init(conn_mode);                           /* SPI3 to the PAN1080, tells it the mode */
  hid_proto.raw = raw;
  hid_proto.pressed = keys.pressed;
  hid_proto.keys = &keys;
  hid_proto.save = settings_save;
  hid_proto.set_mode = PROTO_NO_MODE;
  usb_start();

  while (1) {
    uint32_t now = now_ms();
    wdt_counter_reload();

    if (hid_proto.enter_bootloader) enter_bootloader();
    if (hid_proto.restart) { delay_ms(20); NVIC_SystemReset(); }   /* let the SET_REPORT finish */
    if (now >= HEALTHY_MS) fs_boots = 0;
    if (!proven) {
      if (hid_proto.host_seen) { mark_proven(fingerprint); proven = 1; }
      else if (now >= FAILSAFE_HOST_MS) enter_bootloader();
    }

    scan_frame(raw);
    report_changed = keys_update(&keys, raw, now);
    report_due |= report_changed;
    mode_keys(now);
    mode_follow_usb();
    if (conn_mode != radio.mode) radio_set_mode(conn_mode);
    int wireless = radio.mode != RADIO_USB;

    if (report_due) {
      keys_report(&keys, report);
      if (wireless) { radio_keyboard(report); report_due = 0; }
      else {
        /* USB: NKRO on interface 2 unless off or a BIOS asked for boot protocol; the interface
         * not in use is sent empty once, so switching never leaves a key down */
        int use_nkro = settings.nkro && hid_report_protocol(), kbd_new = 0, nkro_new = 0, ok = 1;
        keys_report_nkro(&keys, nkro);
        for (unsigned i = 0; i < KBD_REPORT_LEN; i++) { if (use_nkro) report[i] = 0; kbd_new |= report[i] != sent_kbd[i]; }
        for (unsigned i = 0; i < NKRO_LEN; i++) { if (!use_nkro) nkro[i] = 0; nkro_new |= nkro[i] != sent_nkro[i]; }
        if (kbd_new) {
          if (hid_send_keyboard(&otg_core_struct.dev, report) == 0)
            for (unsigned i = 0; i < KBD_REPORT_LEN; i++) sent_kbd[i] = report[i];
          else ok = 0;
        }
        if (nkro_new) {
          if (hid_send_nkro(&otg_core_struct.dev, nkro) == 0)
            for (unsigned i = 0; i < NKRO_LEN; i++) sent_nkro[i] = nkro[i];
          else ok = 0;
        }
        if (ok) report_due = 0;                                          /* else retry */
      }
    }

    uint16_t c = keys_consumer(&keys);
    if (c != consumer) { consumer = c; consumer_due = 1; }
    if (consumer_due) {
      if (wireless) { radio_consumer(consumer); consumer_due = 0; }
      else if (hid_send_consumer(&otg_core_struct.dev, consumer) == 0) consumer_due = 0;
    }

    power_task(now);
    if (power.percent != battery_sent) { battery_sent = power.percent; radio_battery(power.percent); }
    radio_task(now);

    leds_apply(&settings.led, keys_take_actions(&keys));
    ind_in_t in = {
      .mode = radio.mode, .radio_state = radio.state, .status_seen = radio.status_seen,
      .pairing_24g = radio.pairing_24g,
      .caps = ((wireless ? radio.host_leds : hid_host_leds()) >> 1) & 1u,
      .winlock = keys.winlock, .battery_key = keys_held(&keys, KC_BATT, KC_BATT) != 0,
      .percent = power.percent, .usb = power.usb, .charging = power.charging,
    };
    indicators(&in, now, &overlay);

    /* On battery, LED power (PB13, as stock cuts it before sleeping) goes off after
     * LED_IDLE_MS without a key; the next key brings it back.
     * ponytail: the MCU itself still runs flat out on battery; real sleep is the next step. */
    if (report_changed) last_key_ms = now;
    if (power.usb || now - last_key_ms < LED_IDLE_MS) {
      GPIOB->scr = GPIO_PINS_13;
      leds_task(&settings.led, &overlay, now);
    } else {
      GPIOB->clr = GPIO_PINS_13;
    }
  }
}
