/*
 * usb_hid.h - Artery usbd class handler for the two HID interfaces in usb_desc.c.
 */
#ifndef USB_HID_H
#define USB_HID_H

#include "usbd_core.h"
#include "proto.h"

extern usbd_class_handler hid_class_handler;
extern proto_state_t hid_proto;          /* vendor protocol state (main sets .raw) */

/* Queue an 8-byte boot keyboard report; 0 if sent, -1 if not configured or still busy. */
int hid_send_keyboard(usbd_core_type *udev, const uint8_t report[8]);
/* Queue a consumer usage (0 = released); 0 if sent, -1 if not configured or still busy. */
int hid_send_consumer(usbd_core_type *udev, uint16_t usage);
/* Last LED state the host sent (Num Lock bit 0, Caps Lock bit 1, Scroll Lock bit 2). */
uint8_t hid_host_leds(void);

#endif /* USB_HID_H */
