/*
 * Coherent debug output with the data cache enabled.
 *
 * This application blinks the LED and prints a heartbeat. That is all it does,
 * and the triviality is deliberate: the interesting work is not in this file.
 * It mirrors Zephyr's basic blinky sample, vendored here so the application is
 * self-contained.
 *
 * The load-bearing pieces live elsewhere:
 *   - the board devicetree carves sram1 as a non-cacheable memory region and
 *     exposes it through an alias
 *   - prj.conf sets CONFIG_SEGGER_RTT_SECTION_CUSTOM_DTS_REGION so the debug
 *     transport places its buffers into that region
 *
 * What is being demonstrated: with the data cache enabled, the debug probe reads
 * this application's live output out of physical SRAM over SWD. That works only
 * because the region is marked non-cacheable, so CPU writes bypass the cache
 * instead of sitting in it.
 *
 * The baseline this replaced: with the debug buffers in ordinary cached RAM, the
 * same read returned stale zeros unless the data cache was disabled entirely.
 * Turning the cache off is a real fix and a bad trade, because the measurement
 * engine that follows wants the cache.
 *
 * All configuration is in prj.conf. See ../../BUILDING.md for the build
 * invocation and for how to read the output back.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
	bool on = true;

	if (!gpio_is_ready_dt(&led)) return -1;
	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE) < 0) return -1;

	while (1) {
		gpio_pin_set_dt(&led, on);
		printk("LED state: %s\n", on ? "ON" : "OFF");
		on = !on;
		k_msleep(1000);
	}
	return 0;
}
