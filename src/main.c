#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(dongle_hello, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("Hello world from dongle over RTT");
	printk("Hello world from dongle via printk/RTT\n");

	while (1) {
		LOG_INF("dongle heartbeat");
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
