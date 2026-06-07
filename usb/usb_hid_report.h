/* HID mouse report formatting (pure logic, no USB stack dependency).
 * Adapted from tempo app/usb/usb_hid_report.h.
 */

#ifndef DONGLE_USB_HID_REPORT_H_
#define DONGLE_USB_HID_REPORT_H_

#include <stdint.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define DONGLE_REPORT_SIZE      7  /* buttons[2] + X[2] + Y[2] + wheel[1] */
#define DONGLE_BOOT_REPORT_SIZE 3  /* buttons[1] + X[1] + Y[1] */

/**
 * @brief Format a report-protocol mouse report (7 bytes).
 *
 * Layout: buttons[2] (LE) + X[2] (LE) + Y[2] (LE) + wheel[1].
 *
 * @param buf     Output buffer, must be >= DONGLE_REPORT_SIZE bytes.
 * @param dx      X-axis delta (signed 16-bit).
 * @param dy      Y-axis delta (signed 16-bit).
 * @param buttons Button bitmask (bits 0-15).
 * @param wheel   Scroll wheel delta (signed 8-bit).
 * @return        Report size (DONGLE_REPORT_SIZE).
 */
static inline uint16_t dongle_format_report(uint8_t *buf,
					    int16_t dx, int16_t dy,
					    uint16_t buttons, int8_t wheel)
{
	sys_put_le16(buttons, &buf[0]);
	sys_put_le16((uint16_t)dx, &buf[2]);
	sys_put_le16((uint16_t)dy, &buf[4]);
	buf[6] = (uint8_t)wheel;
	return DONGLE_REPORT_SIZE;
}

/**
 * @brief Format a boot-protocol mouse report (3 bytes).
 *
 * Layout: buttons[1] (masked to bits 0-2) + X[1] (clamped) + Y[1] (clamped).
 * Only buttons 1-3 are boot-protocol compatible.
 *
 * @param buf     Output buffer, must be >= DONGLE_BOOT_REPORT_SIZE bytes.
 * @param dx      X-axis delta (clamped to [-127, 127]).
 * @param dy      Y-axis delta (clamped to [-127, 127]).
 * @param buttons Button bitmask (only bits 0-2 used).
 * @param wheel   Ignored in boot protocol.
 * @return        Report size (DONGLE_BOOT_REPORT_SIZE).
 */
static inline uint16_t dongle_format_boot_report(uint8_t *buf,
						 int16_t dx, int16_t dy,
						 uint16_t buttons, int8_t wheel)
{
	ARG_UNUSED(wheel);
	buf[0] = (uint8_t)(buttons & 0x07);
	buf[1] = (uint8_t)CLAMP(dx, -127, 127);
	buf[2] = (uint8_t)CLAMP(dy, -127, 127);
	return DONGLE_BOOT_REPORT_SIZE;
}

#endif /* DONGLE_USB_HID_REPORT_H_ */
