/*
 * Dongle ESB PRX receiver (standalone nRF54H20 radio core / cpurad).
 *
 * Receives Enhanced ShockBurst packets and prints each payload over RTT.
 * Pairs with a matching ESB transmitter (PTX) that uses the same addresses
 * and RF channel:
 *
 *   - NCS esb_ptx sample  -> RF channel 2  (set DONGLE_ESB_CHANNEL = 2)
 *   - tempo cpurad PTX    -> RF channel 70 (set DONGLE_ESB_CHANNEL = 70)
 *
 * All three (this RX, the NCS PTX sample, the tempo PTX) share the same
 * default addresses, so only the channel has to match.
 *
 * ESB config: DPL (dynamic payload length), 2 Mbps, 16-bit CRC, pipe set by
 * the prefix table below, selective auto-ack on.
 *
 * The ESB event handler runs in RADIO ISR context. Zephyr deferred logging
 * is ISR-safe (it only enqueues), so logging the payload here is fine for a
 * bring-up receiver. If RX rate gets high, hand the payload to a thread
 * instead (see the tempo radio app for that pattern).
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <nrfx.h>
#include <esb.h>
#if defined(CONFIG_CLOCK_CONTROL_NRF2)
#include <hal/nrf_lrcconf.h>
#endif

LOG_MODULE_REGISTER(dongle_rx, LOG_LEVEL_INF);

/* RF channel. Must match the transmitter. 70 = the tempo cpurad PTX
 * (TEMPO_ESB_CHANNEL). Use 2 to listen to the NCS esb_ptx sample instead
 * (it never calls esb_set_rf_channel, so it stays on
 * CONFIG_ESB_DEFAULT_RF_CHANNEL = 2).
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

static void event_handler(struct esb_evt const *event)
{
	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
	case ESB_EVENT_TX_FAILED:
		break;
	case ESB_EVENT_RX_RECEIVED:
		while (esb_read_rx_payload(&rx_payload) == 0) {
			atomic_inc(&rx_count);
			LOG_INF("RX pipe=%u len=%u rssi=%d",
				rx_payload.pipe, rx_payload.length,
				(int)rx_payload.rssi);
			LOG_HEXDUMP_INF(rx_payload.data, rx_payload.length,
					"payload");
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
	config.bitrate            = ESB_BITRATE_2MBPS;
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

	LOG_INF("ESB PRX ready: 2 Mbps DPL, ch=%u, fast-ramp=%d",
		DONGLE_ESB_CHANNEL, IS_ENABLED(CONFIG_ESB_FAST_SWITCHING));
	return 0;
}

int main(void)
{
	int err;

	LOG_INF("Dongle ESB receiver starting");
	printk("Dongle ESB receiver starting (printk/RTT)\n");

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

	err = esb_start_rx();
	if (err) {
		LOG_ERR("esb_start_rx: %d", err);
		return 0;
	}

	LOG_INF("Listening for ESB packets on channel %u...", DONGLE_ESB_CHANNEL);

	uint32_t prev = 0;

	while (1) {
		k_sleep(K_SECONDS(1));

		uint32_t now = atomic_get(&rx_count);

		LOG_INF("alive: total_rx=%u (+%u/s)", now, now - prev);
		prev = now;
	}

	return 0;
}
