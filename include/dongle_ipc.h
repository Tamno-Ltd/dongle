/*
 * Dongle cpuapp <-> cpurad IPC contract (ICBMSG, single endpoint).
 *
 * Single source of truth for the inter-core mouse-report message, shared by
 * the cpuapp USB-HID app (src/main.c) and the cpurad ESB PRX app
 * (radio/src/main.c). Type-byte demux on one named endpoint.
 *
 * Direction (mirrors tempo's app/ipc/ipc_radio.h convention):
 *   0x20-0x2F  cpurad -> cpuapp
 */

#ifndef DONGLE_IPC_H_
#define DONGLE_IPC_H_

#include <stdint.h>

/* Endpoint name — must match byte-for-byte on both cores. */
#define DONGLE_IPC_EPT_NAME		"dongle_mouse"

/* cpurad -> cpuapp message types. */
#define DONGLE_IPC_MOUSE_REPORT		0x20

/*
 * Canonical mouse-report payload size: the 7-byte USB HID mouse report layout
 * used end to end (ESB on-air -> IPC -> USB HID). The remote transmitter sends
 * exactly these 7 bytes; cpurad forwards them unmodified; cpuapp decodes them
 * and submits a HID report. See usb/usb_hid_report.h for the matching encoder.
 *
 *   bytes 0-1  buttons  (uint16, little-endian, bit 0 = button 1 / LMB)
 *   bytes 2-3  X delta  (int16,  little-endian, relative)
 *   bytes 4-5  Y delta  (int16,  little-endian, relative)
 *   byte  6    wheel    (int8,   relative)
 */
#define DONGLE_MOUSE_REPORT_SIZE	7

/* cpurad -> cpuapp: one HID mouse report received over ESB. */
struct dongle_ipc_mouse_report {
	uint8_t type;				/* DONGLE_IPC_MOUSE_REPORT */
	uint8_t len;				/* valid bytes in report[] (= 7) */
	uint8_t report[DONGLE_MOUSE_REPORT_SIZE];
};  /* 9 bytes */

#endif /* DONGLE_IPC_H_ */
