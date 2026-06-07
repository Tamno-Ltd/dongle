/* USB HID mouse enumeration and report submission on cpuapp.
 * 16-button mouse, 16-bit X/Y, 8-bit wheel.
 * Adapted from tempo app/usb/usb_hid.c (new USB device stack / usbd).
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/usb/class/hid.h>
#include <zephyr/drivers/usb/udc_buf.h>
#include <zephyr/logging/log.h>

#include "usb/usb_hid.h"
#include "usb/usb_hid_report.h"

LOG_MODULE_REGISTER(dongle_usb, LOG_LEVEL_INF);

/* ---- VID/PID (development only — Zephyr project VID) ---- */

#define DONGLE_USB_VID 0x2fe3
#define DONGLE_USB_PID 0x0100

/* ---- HID report descriptor: 16-button mouse, 16-bit X/Y, 8-bit wheel ---- */

static const uint8_t hid_report_desc[] = {
	HID_USAGE_PAGE(HID_USAGE_GEN_DESKTOP),
	HID_USAGE(HID_USAGE_GEN_DESKTOP_MOUSE),
	HID_COLLECTION(HID_COLLECTION_APPLICATION),
		HID_USAGE(HID_USAGE_GEN_DESKTOP_POINTER),
		HID_COLLECTION(HID_COLLECTION_PHYSICAL),
			/* 16 buttons (2 bytes) */
			HID_USAGE_PAGE(HID_USAGE_GEN_BUTTON),
			HID_USAGE_MIN8(1),
			HID_USAGE_MAX8(16),
			HID_LOGICAL_MIN8(0),
			HID_LOGICAL_MAX8(1),
			HID_REPORT_SIZE(1),
			HID_REPORT_COUNT(16),
			HID_INPUT(0x02),                /* Data, Var, Abs */
			/* X and Y axes (16-bit signed, relative) */
			HID_USAGE_PAGE(HID_USAGE_GEN_DESKTOP),
			HID_USAGE(HID_USAGE_GEN_DESKTOP_X),
			HID_USAGE(HID_USAGE_GEN_DESKTOP_Y),
			HID_LOGICAL_MIN16(0x01, 0x80),  /* -32767 */
			HID_LOGICAL_MAX16(0xFF, 0x7F),  /* +32767 */
			HID_REPORT_SIZE(16),
			HID_REPORT_COUNT(2),
			HID_INPUT(0x06),                /* Data, Var, Rel */
			/* Wheel (8-bit signed, relative) */
			HID_USAGE(HID_USAGE_GEN_DESKTOP_WHEEL),
			HID_LOGICAL_MIN8(-127),
			HID_LOGICAL_MAX8(127),
			HID_REPORT_SIZE(8),
			HID_REPORT_COUNT(1),
			HID_INPUT(0x06),                /* Data, Var, Rel */
		HID_END_COLLECTION,
	HID_END_COLLECTION,
};

/* ---- USBD context (file-scope macros) ---- */

USBD_DEVICE_DEFINE(dongle_usbd,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   DONGLE_USB_VID, DONGLE_USB_PID);

USBD_DESC_LANG_DEFINE(dongle_lang);
USBD_DESC_MANUFACTURER_DEFINE(dongle_mfr, "Tamno");
USBD_DESC_PRODUCT_DEFINE(dongle_product, "Tamno Dongle Mouse");

USBD_DESC_CONFIG_DEFINE(dongle_fs_cfg_desc, "FS Configuration");
USBD_DESC_CONFIG_DEFINE(dongle_hs_cfg_desc, "HS Configuration");

/* Bus-powered, 500 mA (bMaxPower = 250 × 2 mA) */
USBD_CONFIGURATION_DEFINE(dongle_fs_config, 0, 250, &dongle_fs_cfg_desc);
USBD_CONFIGURATION_DEFINE(dongle_hs_config, 0, 250, &dongle_hs_cfg_desc);

/* ---- HID device state ---- */

static const struct device *hid_dev;
static bool hid_ready;
static uint8_t hid_protocol = HID_PROTOCOL_REPORT;
static bool usb_enabled;

/* DMA-aligned report buffer (sized for the largest report) */
UDC_STATIC_BUF_DEFINE(report_buf, DONGLE_REPORT_SIZE);

/* ---- HID device callbacks ---- */

static void hid_iface_ready(const struct device *dev, const bool ready)
{
	hid_ready = ready;
	LOG_INF("HID interface %s", ready ? "ready" : "not ready");
}

static int hid_get_report(const struct device *dev,
			  const uint8_t type, const uint8_t id,
			  const uint16_t len, uint8_t *const buf)
{
	LOG_DBG("Get Report type %u id %u len %u", type, id, len);
	return 0;
}

static void hid_set_protocol(const struct device *dev, const uint8_t proto)
{
	hid_protocol = proto;
	LOG_INF("HID protocol set to %s",
		proto == HID_PROTOCOL_REPORT ? "Report" : "Boot");
}

static const struct hid_device_ops hid_ops = {
	.iface_ready = hid_iface_ready,
	.get_report = hid_get_report,
	.set_protocol = hid_set_protocol,
};

/* ---- USB message callback ---- */

static int dongle_usb_enable_context(struct usbd_context *const ctx)
{
	int ret = usbd_enable(ctx);

	if (ret == -EALREADY) {
		usb_enabled = true;
		return 0;
	}

	if (ret) {
		LOG_ERR("usbd_enable failed: %d", ret);
		return ret;
	}

	usb_enabled = true;
	LOG_INF("USB device enabled");
	return 0;
}

static int dongle_usb_disable_context(struct usbd_context *const ctx)
{
	int ret = usbd_disable(ctx);

	hid_ready = false;

	if (ret == -EALREADY) {
		usb_enabled = false;
		return 0;
	}

	if (ret) {
		LOG_ERR("usbd_disable failed: %d", ret);
		return ret;
	}

	usb_enabled = false;
	LOG_INF("USB device disabled");
	return 0;
}

static void usbd_msg_handler(struct usbd_context *const ctx,
			     const struct usbd_msg *const msg)
{
	LOG_INF("USB msg: %s", usbd_msg_type_string(msg->type));

	if (!usbd_can_detect_vbus(ctx)) {
		return;
	}

	switch (msg->type) {
	case USBD_MSG_VBUS_READY:
		(void)dongle_usb_enable_context(ctx);
		break;
	case USBD_MSG_VBUS_REMOVED:
		(void)dongle_usb_disable_context(ctx);
		break;
	default:
		break;
	}
}

/* ---- Public API ---- */

int dongle_usb_submit_mouse_report(int16_t dx, int16_t dy,
				   uint16_t buttons, int8_t wheel)
{
	if (!hid_ready) {
		return -EACCES;
	}

	uint16_t size;

	if (hid_protocol == HID_PROTOCOL_BOOT) {
		size = dongle_format_boot_report(report_buf, dx, dy,
						 buttons, wheel);
	} else {
		size = dongle_format_report(report_buf, dx, dy,
					    buttons, wheel);
	}

	LOG_DBG("HID report: dx=%d dy=%d btn=0x%04x w=%d", dx, dy, buttons, wheel);

	return hid_device_submit_report(hid_dev, size, report_buf);
}

int dongle_usb_init(void)
{
	int ret;

	/* Register HID device with report descriptor */
	hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);

	if (!device_is_ready(hid_dev)) {
		LOG_ERR("HID device not ready");
		return -ENODEV;
	}

	ret = hid_device_register(hid_dev,
				  hid_report_desc, sizeof(hid_report_desc),
				  &hid_ops);
	if (ret) {
		LOG_ERR("HID register failed: %d", ret);
		return ret;
	}

	/* String descriptors */
	ret = usbd_add_descriptor(&dongle_usbd, &dongle_lang);
	if (ret) {
		LOG_ERR("Failed to add lang descriptor: %d", ret);
		return ret;
	}

	ret = usbd_add_descriptor(&dongle_usbd, &dongle_mfr);
	if (ret) {
		LOG_ERR("Failed to add manufacturer descriptor: %d", ret);
		return ret;
	}

	ret = usbd_add_descriptor(&dongle_usbd, &dongle_product);
	if (ret) {
		LOG_ERR("Failed to add product descriptor: %d", ret);
		return ret;
	}

	/* High-Speed configuration (if controller supports it) */
	if (USBD_SUPPORTS_HIGH_SPEED &&
	    usbd_caps_speed(&dongle_usbd) == USBD_SPEED_HS) {
		ret = usbd_add_configuration(&dongle_usbd, USBD_SPEED_HS,
					     &dongle_hs_config);
		if (ret) {
			LOG_ERR("Failed to add HS configuration: %d", ret);
			return ret;
		}

		ret = usbd_register_all_classes(&dongle_usbd, USBD_SPEED_HS,
						1, NULL);
		if (ret) {
			LOG_ERR("Failed to register HS classes: %d", ret);
			return ret;
		}

		usbd_device_set_code_triple(&dongle_usbd, USBD_SPEED_HS,
					    0, 0, 0);
	}

	/* Full-Speed configuration (always present) */
	ret = usbd_add_configuration(&dongle_usbd, USBD_SPEED_FS,
				     &dongle_fs_config);
	if (ret) {
		LOG_ERR("Failed to add FS configuration: %d", ret);
		return ret;
	}

	ret = usbd_register_all_classes(&dongle_usbd, USBD_SPEED_FS, 1, NULL);
	if (ret) {
		LOG_ERR("Failed to register FS classes: %d", ret);
		return ret;
	}

	usbd_device_set_code_triple(&dongle_usbd, USBD_SPEED_FS, 0, 0, 0);

	/* USB message callback for state logging */
	ret = usbd_msg_register_cb(&dongle_usbd, usbd_msg_handler);
	if (ret) {
		LOG_ERR("Failed to register message callback: %d", ret);
		return ret;
	}

	/* Initialize USBD */
	ret = usbd_init(&dongle_usbd);
	if (ret) {
		LOG_ERR("usbd_init failed: %d", ret);
		return ret;
	}

	LOG_INF("USB HID mouse initialized (16-button, 16-bit X/Y)");
	return 0;
}

int dongle_usb_enable(void)
{
	if (usb_enabled) {
		return 0;
	}

	if (usbd_can_detect_vbus(&dongle_usbd)) {
		LOG_INF("USB VBUS detection available; waiting for host VBUS");
		return 0;
	}

	return dongle_usb_enable_context(&dongle_usbd);
}
