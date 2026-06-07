/*
 * Dongle USB HID mouse — cpuapp (application core).
 *
 * Receives 7-byte mouse reports from the radio core (cpurad ESB PRX) over the
 * cpuapp<->cpurad ICBMSG IPC link and submits them to the USB host as HID
 * mouse input reports, so the dongle enumerates in Windows Device Manager as a
 * HID-compliant mouse and moves the host cursor.
 *
 * Data flow:
 *   remote PTX --ESB--> cpurad PRX --IPC(dongle_mouse)--> cpuapp --USB HID--> PC
 *
 * The 7-byte report layout (buttons[2] LE + X[2] LE + Y[2] LE + wheel[1]) is
 * the single canonical format end to end (see include/dongle_ipc.h and
 * usb/usb_hid_report.h).
 */

#include <string.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "usb/usb_hid.h"
#include "dongle_ipc.h"

LOG_MODULE_REGISTER(dongle, LOG_LEVEL_INF);

/* ---- cpurad IPC link (ICBMSG, single named endpoint) ---- */

static K_SEM_DEFINE(radio_ipc_bound_sem, 0, 1);
static struct ipc_ept radio_ep;
static atomic_t reports_rx;
static atomic_t reports_dropped;

static void radio_ipc_bound(void *priv)
{
	ARG_UNUSED(priv);
	k_sem_give(&radio_ipc_bound_sem);
	LOG_INF("radio IPC endpoint bound");
}

static void radio_ipc_recv(const void *data, size_t len, void *priv)
{
	ARG_UNUSED(priv);

	if (len < 1) {
		return;
	}

	const uint8_t *raw = data;

	if (raw[0] != DONGLE_IPC_MOUSE_REPORT) {
		LOG_WRN("radio unknown IPC type=0x%02x len=%zu", raw[0], len);
		return;
	}

	if (len < sizeof(struct dongle_ipc_mouse_report)) {
		LOG_WRN("short mouse report len=%zu", len);
		return;
	}

	struct dongle_ipc_mouse_report msg;

	memcpy(&msg, data, sizeof(msg));

	uint16_t buttons = sys_get_le16(&msg.report[0]);
	int16_t  dx      = (int16_t)sys_get_le16(&msg.report[2]);
	int16_t  dy      = (int16_t)sys_get_le16(&msg.report[4]);
	int8_t   wheel   = (int8_t)msg.report[6];

	int ret = dongle_usb_submit_mouse_report(dx, dy, buttons, wheel);

	if (ret == 0) {
		atomic_inc(&reports_rx);
	} else {
		/* -EACCES until the host enumerates the HID interface (no cable
		 * / driver not bound yet) — expected, counted as a drop.
		 */
		atomic_inc(&reports_dropped);
		if (ret != -EACCES) {
			LOG_WRN("HID submit error: %d", ret);
		}
	}
}

static int radio_ipc_init(void)
{
	const struct device *ipc_dev =
		DEVICE_DT_GET(DT_NODELABEL(cpuapp_cpurad_ipc));

	static struct ipc_ept_cfg cfg = {
		.name = DONGLE_IPC_EPT_NAME,
		.cb = {
			.bound    = radio_ipc_bound,
			.received = radio_ipc_recv,
		},
	};

	if (!device_is_ready(ipc_dev)) {
		LOG_ERR("cpurad ICBMSG instance not ready");
		return -ENODEV;
	}

	int ret = ipc_service_open_instance(ipc_dev);

	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("ipc_service_open_instance: %d", ret);
		return ret;
	}

	ret = ipc_service_register_endpoint(ipc_dev, &radio_ep, &cfg);
	if (ret < 0) {
		LOG_ERR("ipc_service_register_endpoint: %d", ret);
		return ret;
	}

	ret = k_sem_take(&radio_ipc_bound_sem, K_SECONDS(10));
	if (ret < 0) {
		LOG_WRN("radio IPC bind timeout (cpurad link down)");
		return ret;
	}

	return 0;
}

int main(void)
{
	int err;

	LOG_INF("Dongle USB HID mouse starting (cpuapp)");
	printk("Dongle USB HID mouse starting (printk/RTT)\n");

	/* Bring up USB HID first so the host can enumerate the mouse early. */
	err = dongle_usb_init();
	if (err) {
		LOG_ERR("dongle_usb_init: %d", err);
		return 0;
	}

	err = dongle_usb_enable();
	if (err) {
		LOG_WRN("dongle_usb_enable: %d", err);
	}

	/* Bring up the cpurad IPC link that feeds mouse reports. */
	err = radio_ipc_init();
	if (err) {
		LOG_WRN("radio_ipc_init: %d (no mouse data from radio core)",
			err);
	}

	uint32_t prev = 0;

	while (1) {
		k_sleep(K_SECONDS(1));

		uint32_t now = atomic_get(&reports_rx);

		LOG_INF("alive: hid_tx=%u (+%u/s) dropped=%u",
			now, now - prev,
			(unsigned int)atomic_get(&reports_dropped));
		prev = now;
	}

	return 0;
}
