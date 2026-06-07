/* USB HID mouse device — enumeration and report submission.
 * Adapted from tempo app/usb/usb_hid.h.
 */

#ifndef DONGLE_USB_HID_H_
#define DONGLE_USB_HID_H_

#include <stdint.h>

/**
 * @brief Initialize USB HID mouse device.
 *
 * Registers the HID report descriptor and callbacks, adds USB string
 * descriptors, configures FS/HS configurations, and calls usbd_init().
 *
 * @return 0 on success, negative errno on failure.
 */
int dongle_usb_init(void);

/**
 * @brief Enable USB device stack.
 *
 * Calls usbd_enable() which blocks until VBUS is ready or the timeout expires
 * (CONFIG_UDC_DWC2_USBHS_VBUS_READY_TIMEOUT).
 *
 * @return 0 on success, negative errno on failure.
 */
int dongle_usb_enable(void);

/**
 * @brief Submit a mouse HID report to the host.
 *
 * Formats and sends a mouse report using the current protocol (Report or Boot).
 * Report protocol: 7 bytes — buttons[2] + X[2] + Y[2] + wheel[1].
 * Boot protocol:   3 bytes — buttons[1] + X[1] + Y[1] (clamped to int8_t).
 *
 * @param dx      X-axis delta (signed 16-bit, relative).
 * @param dy      Y-axis delta (signed 16-bit, relative).
 * @param buttons Button bitmask (bit 0 = button 1 / LMB, up to bit 15).
 * @param wheel   Scroll wheel delta (signed 8-bit).
 *
 * @return 0 on success, -EACCES if the HID interface is not ready (host has
 *         not enumerated yet), negative errno otherwise.
 */
int dongle_usb_submit_mouse_report(int16_t dx, int16_t dy,
				   uint16_t buttons, int8_t wheel);

#endif /* DONGLE_USB_HID_H_ */
