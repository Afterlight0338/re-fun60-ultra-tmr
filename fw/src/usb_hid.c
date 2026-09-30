/*
 * usb_hid.c - class handler for the boot keyboard (interface 0), the vendor feature-report
 * interface (interface 1) and consumer control (interface 2). Modelled on Artery's custom_hid class (usbd_class/custom_hid).
 */
#include "usb_hid.h"
#include "usb_desc.h"
#include "usb_std.h"

#define ALIGN4 __attribute__((aligned(4)))

proto_state_t hid_proto;

static struct {
  uint8_t protocol, idle, alt, set_itf;
  volatile uint8_t kbd_busy, cons_busy;
  uint8_t kbd[KBD_REPORT_LEN] ALIGN4;
  uint8_t cons[CONSUMER_REPORT_LEN] ALIGN4;
  uint8_t led[4] ALIGN4;
  uint8_t feature[PROTO_REPORT_LEN] ALIGN4;
} hid;

static usb_sts_type init_handler(void *udev)
{
  usbd_ept_open(udev, EP_KBD_IN, EPT_INT_TYPE, KBD_REPORT_LEN);
  usbd_ept_open(udev, EP_VENDOR_IN, EPT_INT_TYPE, PROTO_REPORT_LEN);
  usbd_ept_open(udev, EP_CONSUMER_IN, EPT_INT_TYPE, CONSUMER_REPORT_LEN);
  hid.kbd_busy = hid.cons_busy = 0;
  return USB_OK;
}

static usb_sts_type clear_handler(void *udev)
{
  usbd_ept_close(udev, EP_KBD_IN);
  usbd_ept_close(udev, EP_VENDOR_IN);
  usbd_ept_close(udev, EP_CONSUMER_IN);
  return USB_OK;
}

static usb_sts_type setup_handler(void *udev, usb_setup_type *setup)
{
  uint8_t itf = (uint8_t)setup->wIndex;
  uint16_t len;

  switch (setup->bmRequestType & USB_REQ_TYPE_RESERVED) {
  case USB_REQ_TYPE_CLASS:
    switch (setup->bRequest) {
    case HID_REQ_SET_REPORT:                       /* data stage lands in ept0_rx_handler */
      hid.set_itf = itf;
      if (itf == IF_VENDOR) usbd_ctrl_recv(udev, hid.feature, PROTO_REPORT_LEN);
      else                  usbd_ctrl_recv(udev, hid.led, 1);
      break;
    case HID_REQ_GET_REPORT:
      if (itf == IF_VENDOR)        usbd_ctrl_send(udev, hid.feature, MIN(PROTO_REPORT_LEN, setup->wLength));
      else if (itf == IF_CONSUMER) usbd_ctrl_send(udev, hid.cons, MIN(CONSUMER_REPORT_LEN, setup->wLength));
      else                         usbd_ctrl_send(udev, hid.kbd, MIN(KBD_REPORT_LEN, setup->wLength));
      break;
    case HID_REQ_SET_IDLE:     hid.idle = (uint8_t)(setup->wValue >> 8);  break;
    case HID_REQ_GET_IDLE:     usbd_ctrl_send(udev, &hid.idle, 1);        break;
    case HID_REQ_SET_PROTOCOL: hid.protocol = (uint8_t)setup->wValue;     break;
    case HID_REQ_GET_PROTOCOL: usbd_ctrl_send(udev, &hid.protocol, 1);    break;
    default:                   usbd_ctrl_unsupport(udev);                 break;
    }
    break;

  case USB_REQ_TYPE_STANDARD:
    switch (setup->bRequest) {
    case USB_STD_REQ_GET_DESCRIPTOR:
      if (setup->wValue >> 8 == HID_REPORT_DESC) {
        const uint8_t *d = itf == IF_VENDOR ? usb_vendor_report_desc
                         : itf == IF_CONSUMER ? usb_consumer_report_desc : usb_kbd_report_desc;
        len = itf == IF_VENDOR ? VENDOR_REPORT_DESC_LEN
            : itf == IF_CONSUMER ? CONSUMER_REPORT_DESC_LEN : KBD_REPORT_DESC_LEN;
        usbd_ctrl_send(udev, (uint8_t *)d, MIN(len, setup->wLength));
      } else if (setup->wValue >> 8 == HID_DESCRIPTOR_TYPE && itf <= IF_CONSUMER) {
        usbd_ctrl_send(udev, (uint8_t *)usb_config_desc + HID_DESC_OFFSET(itf), MIN(9, setup->wLength));
      } else {
        usbd_ctrl_unsupport(udev);
      }
      break;
    case USB_STD_REQ_GET_INTERFACE: usbd_ctrl_send(udev, &hid.alt, 1);        break;
    case USB_STD_REQ_SET_INTERFACE: hid.alt = (uint8_t)setup->wValue;         break;
    case USB_STD_REQ_CLEAR_FEATURE:
    case USB_STD_REQ_SET_FEATURE:                                             break;
    default:                        usbd_ctrl_unsupport(udev);                break;
    }
    break;

  default:
    usbd_ctrl_unsupport(udev);
    break;
  }
  return USB_OK;
}

static usb_sts_type ept0_tx_handler(void *udev) { (void)udev; return USB_OK; }

static usb_sts_type ept0_rx_handler(void *udev)
{
  (void)udev;
  if (hid.set_itf == IF_VENDOR) proto_dispatch(&hid_proto, hid.feature);  /* answer via GET_REPORT */
  return USB_OK;
}

static usb_sts_type in_handler(void *udev, uint8_t ept_num)
{
  (void)udev;
  if ((ept_num & 0x7F) == (EP_KBD_IN & 0x7F)) hid.kbd_busy = 0;
  if ((ept_num & 0x7F) == (EP_CONSUMER_IN & 0x7F)) hid.cons_busy = 0;
  return USB_OK;
}

static usb_sts_type out_handler(void *udev, uint8_t ept_num) { (void)udev; (void)ept_num; return USB_OK; }
static usb_sts_type sof_handler(void *udev) { (void)udev; return USB_OK; }
static usb_sts_type event_handler(void *udev, usbd_event_type e) { (void)udev; (void)e; return USB_OK; }

usbd_class_handler hid_class_handler = {
  init_handler, clear_handler, setup_handler, ept0_tx_handler, ept0_rx_handler,
  in_handler, out_handler, sof_handler, event_handler, &hid
};

int hid_send_keyboard(usbd_core_type *udev, const uint8_t report[8])
{
  if (usbd_connect_state_get(udev) != USB_CONN_STATE_CONFIGURED || hid.kbd_busy) return -1;
  for (unsigned i = 0; i < KBD_REPORT_LEN; i++) hid.kbd[i] = report[i];
  hid.kbd_busy = 1;
  usbd_ept_send(udev, EP_KBD_IN, hid.kbd, KBD_REPORT_LEN);
  return 0;
}

int hid_send_consumer(usbd_core_type *udev, uint16_t usage)
{
  if (usbd_connect_state_get(udev) != USB_CONN_STATE_CONFIGURED || hid.cons_busy) return -1;
  hid.cons[0] = (uint8_t)usage;
  hid.cons[1] = (uint8_t)(usage >> 8);
  hid.cons_busy = 1;
  usbd_ept_send(udev, EP_CONSUMER_IN, hid.cons, CONSUMER_REPORT_LEN);
  return 0;
}

uint8_t hid_host_leds(void) { return hid.led[0]; }   /* keyboard LED output report: bit 1 Caps */
