/*
 * usb_desc.h - USB descriptors: composite HID, interface 0 = boot keyboard (EP 0x81),
 * interface 1 = vendor feature reports (EP 0x82, usage page 0xFFFF / usage 2, as stock),
 * interface 2 = consumer control (EP 0x83: volume, mute, calculator, report ID 1) and the
 * NKRO keyboard (report ID 2: modifiers + a bit per usage 0x00-0x77; v0.14).
 */
#ifndef USB_DESC_H
#define USB_DESC_H

#include <stdint.h>

#define USB_VID            0x3151u   /* stock's, so ry-flash recognises the board */
#define USB_PID            0x5030u
#define EP_KBD_IN          0x81u
#define EP_VENDOR_IN       0x82u     /* never used: usbhid only binds HID interfaces with an IN */
#define EP_CONSUMER_IN     0x83u
#define KBD_REPORT_LEN     8u
#define CONSUMER_REPORT_LEN 3u       /* report ID 1, one 16-bit consumer usage */
#define NKRO_REPORT_LEN    17u       /* report ID 2, modifiers, 15-byte bitmap */
#define IF2_EP_SIZE        32u
#define REPORT_ID_CONSUMER 1u
#define REPORT_ID_NKRO     2u
#define IF_KBD             0u
#define IF_VENDOR          1u
#define IF_CONSUMER        2u

#define CONFIG_DESC_LEN    84u       /* 9 + 3 x (interface 9 + HID 9 + endpoint 7) */
#define KBD_REPORT_DESC_LEN    63u
#define VENDOR_REPORT_DESC_LEN 20u
#define CONSUMER_REPORT_DESC_LEN 58u
#define HID_DESC_OFFSET(itf)   (9u + 9u + (itf) * 25u)   /* HID descriptor inside the config */

extern const uint8_t usb_config_desc[CONFIG_DESC_LEN];
extern const uint8_t usb_kbd_report_desc[KBD_REPORT_DESC_LEN];
extern const uint8_t usb_vendor_report_desc[VENDOR_REPORT_DESC_LEN];
extern const uint8_t usb_consumer_report_desc[CONSUMER_REPORT_DESC_LEN];

#ifndef HOST_TEST
#include "usbd_core.h"
extern usbd_desc_handler usb_desc_handler;
#endif

#endif /* USB_DESC_H */
