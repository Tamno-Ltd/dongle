/*
 * Dongle ESB PRX receiver — radio core (cpurad) image of the dongle sysbuild.
 *
 * Receives Enhanced ShockBurst mouse packets and forwards each one to the
 * application core (cpuapp) over the cpuapp<->cpurad ICBMSG IPC link, where it
 * is turned into a USB HID mouse report. Pairs with an ESB transmitter (PTX)
 * that uses the same addresses and RF channel:
 *
 *   - tempo cpurad PTX    -> RF channel 70 (DONGLE_ESB_CHANNEL = 70)
 *   - NCS esb_ptx sample  -> RF channel 2  (set DONGLE_ESB_CHANNEL = 2)
 *
 * ESB config: DPL (dynamic payload length), 4 Mbps, 16-bit CRC, selective
 * auto-ack on, fast ramp-up. Must match the transmitter byte-for-byte.
 *
 * On-air payload: the canonical 7-byte USB HID mouse report layout
 * (buttons[2] LE + X[2] LE + Y[2] LE + wheel[1]); see include/dongle_ipc.h.
 *
 * The ESB event handler runs in RADIO ISR context, so it only enqueues the
 * payload to a message queue; a dedicated thread drains the queue and performs
 * the (potentially blocking) IPC send.
 */

#include <string.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/logging/log.h>

#include <nrfx.h>
#include <esb.h>
#if defined(CONFIG_CLOCK_CONTROL_NRF2)
#include <hal/nrf_lrcconf.h>
#endif

#include "dongle_ipc.h"

LOG_MODULE_REGISTER(dongle_radio, LOG_LEVEL_INF);

/* RF channel. Must match the transmitter. 70 = the tempo cpurad PTX
 * (TEMPO_ESB_CHANNEL). Use 2 to listen to the NCS esb_ptx sample instead.
 */
#define DONGLE_ESB_CHANNEL 70

/* Shared default addresses (identical in the NCS esb_ptx/esb_prx samples and
 * the tempo cpurad PTX). Must be identical on TX and RX.
 */
static const uint8_t base_addr_0[4]   = {0xE7, 0xE7, 0xE7, 0xE7};
static const uint8_t base_addr_1[4]   = {0xC2, 0xC2, 0xC2, 0xC2};
static const uint8_t addr_prefixes[8] = {
	0xE7, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8,
};

static struct esb_payload rx_payload;
static atomic_t rx_count;
static atomic_t drop_count;

/* ISR -> thread handoff for received mouse reports. */
K_MSGQ_DEFINE(report_msgq, sizeof(struct dongle_ipc_mouse_report), 16, 4);

/* ---- cpuapp IPC link (ICBMSG, single named endpoint) ---- */

static K_SEM_DEFINE(ipc_bound_sem, 0, 1);
static struct ipc_ept radio_ep;

static void radio_ipc_bound(void *priv)
{
	ARG_UNUSED(priv);
	k_sem_give(&ipc_bound_sem);
	LOG_INF("cpuapp IPC endpoint bound");
}

static void radio_ipc_recv(const void *data, size_t len, void *priv)
{
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	ARG_UNUSED(priv);
	/* cpuapp -> cpurad direction is unused in the receiver. */
}

/* Build an IPC mouse report from an ESB payload and enqueue it. Runs in the
 * RADIO ISR via the ESB event handler — non-blocking only.
 */
static void esb_payload_to_msgq(const struct esb_payload *p)
{
	struct dongle_ipc_mouse_report msg = {
		.type = DONGLE_IPC_MOUSE_REPORT,
		.len  = DONGLE_MOUSE_REPORT_SIZE,
	};
	uint8_t n = MIN(p->length, DONGLE_MOUSE_REPORT_SIZE);

	memcpy(msg.report, p->data, n);
	/* Any remaining report[] bytes stay zero from the initializer. */

	if (k_msgq_put(&report_msgq, &msg, K_NO_WAIT) != 0) {
		atomic_inc(&drop_count);
	}
}

static void event_handler(struct esb_evt const *event)
{
	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
	case ESB_EVENT_TX_FAILED:
		break;
	case ESB_EVENT_RX_RECEIVED:
		while (esb_read_rx_payload(&rx_payload) == 0) {
			atomic_inc(&rx_count);
			esb_payload_to_msgq(&rx_payload);
		}
		break;
	}
}

/* HF clock start. nRF54H20 uses the CLOCK_CONTROL_NRF2 driver; this is the
 * exact sequence used by the NCS esb_prx sample and the tempo cpurad app.
 */
#if defined(CONFIG_CLOCK_CONTROL_NRF2)
static int clocks_start(void)
{
	int err;
	int res;
	const struct device *radio_clk_dev =
		DEVICE_DT_GET_OR_NULL(DT_CLOCKS_CTLR(DT_NODELABEL(radio)));
	struct onoff_client radio_cli;

	if (radio_clk_dev == NULL) {
		LOG_ERR("radio clock controller not present in DT");
		return -ENODEV;
	}

	/* Keep the radio power domain on to reduce wake latency. */
	nrf_lrcconf_poweron_force_set(NRF_LRCCONF010,
				      NRF_LRCCONF_POWER_DOMAIN_1, true);

	sys_notify_init_spinwait(&radio_cli.notify);

	err = nrf_clock_control_request(radio_clk_dev, NULL, &radio_cli);
	if (err < 0) {
		LOG_ERR("nrf_clock_control_request: %d", err);
		return err;
	}

	do {
		err = sys_notify_fetch_result(&radio_cli.notify, &res);
		if (!err && res) {
			LOG_ERR("HF clock could not be started: %d", res);
			return res;
		}
	} while (err == -EAGAIN);

	/* HMPAN-84 errata: keep the LRCCONF clock running. */
	if (nrf54h_errata_84()) {
		nrf_lrcconf_clock_always_run_force_set(NRF_LRCCONF000, 0, true);
		nrf_lrcconf_task_trigger(NRF_LRCCONF000,
					 NRF_LRCCONF_TASK_CLKSTART_0);
	}

	LOG_INF("HF clock started");
	return 0;
}
#else
#error "This app targets nRF54H20 cpurad (CONFIG_CLOCK_CONTROL_NRF2)."
#endif

static int esb_initialize(void)
{
	int err;
	struct esb_config config = ESB_DEFAULT_CONFIG;

	config.protocol           = ESB_PROTOCOL_ESB_DPL;
	config.mode               = ESB_MODE_PRX;
	config.bitrate            = ESB_BITRATE_4MBPS;
	config.crc                = ESB_CRC_16BIT;
	config.event_handler      = event_handler;
	config.selective_auto_ack = true;
	config.payload_length     = 32;

	if (IS_ENABLED(CONFIG_ESB_FAST_SWITCHING)) {
		config.use_fast_ramp_up = true;
	}

	err = esb_init(&config);
	if (err) {
		LOG_ERR("esb_init: %d", err);
		return err;
	}

	err = esb_set_base_address_0(base_addr_0);
	if (err) {
		return err;
	}

	err = esb_set_base_address_1(base_addr_1);
	if (err) {
		return err;
	}

	err = esb_set_prefixes(addr_prefixes, ARRAY_SIZE(addr_prefixes));
	if (err) {
		return err;
	}

	err = esb_set_rf_channel(DONGLE_ESB_CHANNEL);
	if (err) {
		return err;
	}

	LOG_INF("ESB PRX ready: 4 Mbps DPL, ch=%u, fast-ramp=%d",
		DONGLE_ESB_CHANNEL, IS_ENABLED(CONFIG_ESB_FAST_SWITCHING));
	return 0;
}

/* Drains the RX message queue and forwards each report to cpuapp over IPC.
 * Independent of the ISR so the (potentially blocking) send never runs in
 * RADIO interrupt context.
 */
static void ipc_tx_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	/* Wait until cpuapp has bound the endpoint before sending. */
	k_sem_take(&ipc_bound_sem, K_FOREVER);

	struct dongle_ipc_mouse_report msg;

	while (1) {
		k_msgq_get(&report_msgq, &msg, K_FOREVER);

		int ret = ipc_service_send(&radio_ep, &msg, sizeof(msg));

		if (ret < 0) {
			atomic_inc(&drop_count);
		}
	}
}

K_THREAD_DEFINE(ipc_tx_tid, 1024, ipc_tx_thread, NULL, NULL, NULL,
		K_PRIO_PREEMPT(7), 0, 0);

int main(void)
{
	int err;

	LOG_INF("Dongle ESB receiver starting (cpurad)");

	err = clocks_start();
	if (err) {
		LOG_ERR("clocks_start: %d", err);
		return 0;
	}

	err = esb_initialize();
	if (err) {
		LOG_ERR("esb_initialize: %d", err);
		return 0;
	}

	/* Bring up the IPC link to cpuapp. */
	const struct device *ipc_dev =
		DEVICE_DT_GET(DT_NODELABEL(cpuapp_cpurad_ipc));

	if (!device_is_ready(ipc_dev)) {
		LOG_ERR("ICBMSG instance not ready");
		return 0;
	}

	err = ipc_service_open_instance(ipc_dev);
	if (err < 0 && err != -EALREADY) {
		LOG_ERR("ipc_service_open_instance: %d", err);
		return 0;
	}

	static struct ipc_ept_cfg cfg = {
		.name = DONGLE_IPC_EPT_NAME,
		.cb = {
			.bound    = radio_ipc_bound,
			.received = radio_ipc_recv,
		},
	};

	err = ipc_service_register_endpoint(ipc_dev, &radio_ep, &cfg);
	if (err < 0) {
		LOG_ERR("ipc_service_register_endpoint: %d", err);
		return 0;
	}

	err = esb_start_rx();
	if (err) {
		LOG_ERR("esb_start_rx: %d", err);
		return 0;
	}

	LOG_INF("Listening for ESB mouse packets on channel %u...",
		DONGLE_ESB_CHANNEL);

	uint32_t prev = 0;

	while (1) {
		k_sleep(K_SECONDS(1));

		uint32_t now = atomic_get(&rx_count);

		LOG_INF("alive: rx=%u (+%u/s) drops=%u", now, now - prev,
			(unsigned int)atomic_get(&drop_count));
		prev = now;
	}

	return 0;
}
