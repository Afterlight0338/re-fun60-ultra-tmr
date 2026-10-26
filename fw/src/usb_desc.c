/*
 * usb_desc.c - USB descriptors and the Artery descriptor-handler callbacks.
 */
#include "usb_desc.h"
#include "board.h"

#define LB(x) ((uint8_t)((x) & 0xFF))
#define HB(x) ((uint8_t)(((x) >> 8) & 0xFF))

/* HID 1.11 appendix B.1 boot keyboard: modifiers, reserved, 6 keys in; 5 LEDs out */
const uint8_t usb_kbd_report_desc[KBD_REPORT_DESC_LEN] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,             /* Generic Desktop, Keyboard, Application */
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,             /*   Keyboard page, modifiers E0..E7 */
  0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,   /* 8 x 1 bit, Data,Var,Abs */
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,             /*   reserved byte, Const */
  0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,   /* 5 LEDs out */
  0x95, 0x01, 0x75, 0x03, 0x91, 0x01,             /*   LED padding */
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
  0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, /*   6 key codes, Data,Array */
  0xC0
};

/* byte-identical to stock's vendor interface: 64-byte feature report, no report ID */
const uint8_t usb_vendor_report_desc[VENDOR_REPORT_DESC_LEN] = {
  0x06, 0xFF, 0xFF, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x02,
  0x15, 0x80, 0x25, 0x7F, 0x95, 0x40, 0x75, 0x08, 0xB1, 0x02, 0xC0
};

/* report 1: one 16-bit consumer usage (0 = released): volume, mute, calculator, ...
 * report 2: NKRO keyboard, modifiers + one bit per usage 0x00-0x77 (every key on the board) */
const uint8_t usb_consumer_report_desc[CONSUMER_REPORT_DESC_LEN] = {
  0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, REPORT_ID_CONSUMER,   /* Consumer Control */
  0x15, 0x00, 0x26, 0xFF, 0x03,                   /*   logical 0..0x3FF */
  0x19, 0x00, 0x2A, 0xFF, 0x03,                   /*   usages 0..0x3FF */
  0x75, 0x10, 0x95, 0x01, 0x81, 0x00,             /*   1 x 16 bit, Data,Array */
  0xC0,
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, REPORT_ID_NKRO,       /* Keyboard */
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,             /*   modifiers E0..E7 */
  0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,     /* 8 x 1 bit, Data,Var,Abs */
  0x19, 0x00, 0x29, 0x77, 0x95, 0x78, 0x81, 0x02, /*   usages 0x00..0x77, 120 x 1 bit */
  0xC0
};

#define CONFIG_BODY(type)                                                              \
  0x09, (type), LB(CONFIG_DESC_LEN), HB(CONFIG_DESC_LEN), 0x03, 0x01, 0x00, 0x80, 0xFA, \
  /* interface 0: boot keyboard */                                                     \
  0x09, 0x04, IF_KBD, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,                              \
  0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, LB(KBD_REPORT_DESC_LEN), HB(KBD_REPORT_DESC_LEN), \
  0x07, 0x05, EP_KBD_IN, 0x03, KBD_REPORT_LEN, 0x00, 0x01,  /* interrupt, 125 us at HS */ \
  /* interface 1: vendor feature reports */                                           \
  0x09, 0x04, IF_VENDOR, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,                           \
  0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, LB(VENDOR_REPORT_DESC_LEN), HB(VENDOR_REPORT_DESC_LEN), \
  0x07, 0x05, EP_VENDOR_IN, 0x03, 0x40, 0x00, 0x01,                                    \
  /* interface 2: consumer control */                                                 \
  0x09, 0x04, IF_CONSUMER, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,                         \
  0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, LB(CONSUMER_REPORT_DESC_LEN), HB(CONSUMER_REPORT_DESC_LEN), \
  0x07, 0x05, EP_CONSUMER_IN, 0x03, IF2_EP_SIZE, 0x00, 0x01    /* 125 us at HS: NKRO keys */

const uint8_t usb_config_desc[CONFIG_DESC_LEN] = { CONFIG_BODY(0x02) };

#ifndef HOST_TEST
#include "usb_std.h"

static const uint8_t other_speed_desc[CONFIG_DESC_LEN] = { CONFIG_BODY(0x07) };

static const uint8_t device_desc[USB_DEVICE_DESC_LEN] = {
  USB_DEVICE_DESC_LEN, USB_DESCIPTOR_TYPE_DEVICE, 0x00, 0x02, 0x00, 0x00, 0x00, 64,
  LB(USB_VID), HB(USB_VID), LB(USB_PID), HB(USB_PID),
  FW_VER_MINOR, FW_VER_MAJOR, 0x01, 0x02, 0x00, 0x01    /* bcdDevice = our version */
};

static const uint8_t qualifier_desc[USB_DEVICE_QUALIFIER_DESC_LEN] = {
  USB_DEVICE_QUALIFIER_DESC_LEN, USB_DESCIPTOR_TYPE_DEVICE_QUALIFIER, 0x00, 0x02,
  0x00, 0x00, 0x00, 64, 0x01, 0x00
};

static const uint8_t lang_desc[4] = { 4, USB_DESCIPTOR_TYPE_STRING, 0x09, 0x04 };

static uint8_t str_buf[2 + 2 * 32] __attribute__((aligned(4)));

/* ASCII -> USB string descriptor in str_buf */
static usbd_desc_t *string_desc(const char *s)
{
  static usbd_desc_t d;
  unsigned n = 0;
  while (s[n] && n < 32) {
    str_buf[2 + 2 * n] = (uint8_t)s[n];
    str_buf[3 + 2 * n] = 0;
    n++;
  }
  str_buf[0] = (uint8_t)(2 + 2 * n);
  str_buf[1] = USB_DESCIPTOR_TYPE_STRING;
  d.length = str_buf[0];
  d.descriptor = str_buf;
  return &d;
}

#define DESC(name, arr) \
  static usbd_desc_t *name(void) { static usbd_desc_t d = { sizeof(arr), (uint8_t *)(arr) }; return &d; }
DESC(get_device, device_desc)
DESC(get_qualifier, qualifier_desc)
DESC(get_config, usb_config_desc)
DESC(get_other_speed, other_speed_desc)
DESC(get_lang, lang_desc)

static usbd_desc_t *get_manufacturer(void) { return string_desc("RY5088 open firmware"); }
static usbd_desc_t *get_product(void)      { return string_desc("FUN60 Ultra TMR"); }

usbd_desc_handler usb_desc_handler = {
  get_device, get_qualifier, get_config, get_other_speed, get_lang,
  get_manufacturer, get_product, get_product /* serial: index 0, never asked */,
  get_product /* interface string: unused */, get_product /* config string: unused */,
  get_config /* same layout at high speed */
};
#endif
