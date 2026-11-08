/*
 * Even Realities G1 - GPIO lines, touch/NFC interrupt buttons and the
 * hardware-id straps.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#include "g1_log.h"
#include "button.h"
#include "../aw9320x/aw9320x.h"
#include "../nfc/nfc_wlc.h"
#include "../threads/threads.h"
#include <glasses_state.h>

#define GPIO0 DEVICE_DT_GET(DT_NODELABEL(gpio0))
#define GPIO1 DEVICE_DT_GET(DT_NODELABEL(gpio1))

static uint8_t button2_count;
static struct gpio_callback button1_irq_cb;
static struct gpio_callback button2_irq_cb;

int touch_irq_pending;
static const struct gpio_dt_spec panel_gpio = {.port = GPIO0, .pin = 24, .dt_flags = 0};
static const struct gpio_dt_spec gpio_p0_21 = {.port = GPIO0, .pin = 21, .dt_flags = 0};
static const struct gpio_dt_spec gpio_p0_30 = {.port = GPIO0, .pin = 30, .dt_flags = 0};
static const struct gpio_dt_spec button_irq2_gpio_spec = GPIO_DT_SPEC_GET(DT_NODELABEL(sw1), gpios);
static const struct gpio_dt_spec pmic_reset_gpio = {.port = GPIO0, .pin = 23, .dt_flags = 0};

static const struct gpio_dt_spec button_irq_gpio_spec = GPIO_DT_SPEC_GET(DT_NODELABEL(sw0), gpios);

static void button2_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	trigger_nfc_event();
	if (get_dashboard_state()->work_mode == 8 || can_enter_low_power == 2) {
		if (++button2_count > 4) {
			can_enter_low_power = 0;
			nfc_deadline_set(0);
		}
		if (can_enter_low_power == 0) {
			button2_count = 0;
		}
	} else {
		button2_count = 0;
	}
}

static void button1_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	gpio_pin_interrupt_configure_dt(&button_irq_gpio_spec, GPIO_INT_DISABLE);
	if (pins == BIT(9) && aw93203_ready) {
		touch_irq_pending = 1;
		k_sem_give(&get_dashboard_state()->touch_sem);
	}
	gpio_pin_interrupt_configure_dt(&button_irq_gpio_spec, GPIO_INT_EDGE_TO_ACTIVE);
}

void panel_gpio_disable(void)
{
	gpio_pin_set_dt(&panel_gpio, 0);
}

void panel_gpio_enable(void)
{
	gpio_pin_set_dt(&panel_gpio, 1);
}

void nfc_lpd_on(void)
{
	gpio_pin_set_dt(&gpio_p0_21, 1);
}

void nfc_lpd_off(void)
{
	gpio_pin_set_dt(&gpio_p0_21, 0);
}

void touch_power_on(void)
{
	gpio_pin_set_dt(&gpio_p0_30, 1);
}

void touch_power_off(void)
{
	gpio_pin_set_dt(&gpio_p0_30, 0);
}

void reset_pmic_on(void)
{
	gpio_pin_set_dt(&pmic_reset_gpio, 1);
}

void reset_pmic_off(void)
{
	gpio_pin_set_dt(&pmic_reset_gpio, 0);
}

static void gpio_init(void)
{
	const struct gpio_dt_spec id1 = {.port = GPIO0, .pin = 26, .dt_flags = 0};
	const struct gpio_dt_spec id2 = {.port = GPIO0, .pin = 25, .dt_flags = 0};
	const struct gpio_dt_spec id3 = {.port = GPIO0, .pin = 28, .dt_flags = 0};
	const struct gpio_dt_spec id4 = {.port = GPIO0, .pin = 27, .dt_flags = 0};
	const struct gpio_dt_spec tmr = {.port = GPIO0, .pin = 19, .dt_flags = 0};

	gpio_pin_configure_dt(&id1, GPIO_INPUT);
	gpio_pin_configure_dt(&id2, GPIO_INPUT);
	gpio_pin_configure_dt(&id3, GPIO_INPUT);
	gpio_pin_configure_dt(&id4, GPIO_INPUT);

	gpio_pin_configure_dt(&panel_gpio, GPIO_INPUT | GPIO_OUTPUT);
	gpio_pin_set_dt(&panel_gpio, 0);
	gpio_pin_configure_dt(&tmr, GPIO_INPUT | GPIO_OUTPUT);
	gpio_pin_set_dt(&tmr, 0);
	gpio_pin_configure_dt(&gpio_p0_21, GPIO_INPUT | GPIO_OUTPUT);
	gpio_pin_set_dt(&gpio_p0_21, 0);
	gpio_pin_configure_dt(&gpio_p0_30, GPIO_INPUT | GPIO_OUTPUT);
	gpio_pin_set_dt(&gpio_p0_30, 1);
	gpio_pin_configure_dt(&pmic_reset_gpio, GPIO_INPUT | GPIO_OUTPUT);
	gpio_pin_set_dt(&pmic_reset_gpio, 0);
}

static void nfc_gpo_init(void)
{
	int ret;

	if (!device_is_ready(button_irq2_gpio_spec.port)) {
		G1_LOG(1, "SW0 GPIO port device not ready\n\n");
		return;
	}
	ret = gpio_pin_configure_dt(&button_irq2_gpio_spec, GPIO_INPUT);
	if (ret < 0) {
		return;
	}
	gpio_init_callback(&button2_irq_cb, button2_pressed, BIT(button_irq2_gpio_spec.pin));
	ret = gpio_add_callback(button_irq2_gpio_spec.port, &button2_irq_cb);
	if (ret < 0) {
		G1_LOG(1, "Unable to configure SW0 GPIO pin!\n\n");
		return;
	}
	if (nfc_is_tag_type_ok()) {
		gpio_pin_interrupt_configure_dt(&button_irq2_gpio_spec, GPIO_INT_EDGE_TO_INACTIVE);
	} else {
		gpio_pin_interrupt_configure_dt(&button_irq2_gpio_spec, GPIO_INT_EDGE_TO_ACTIVE);
	}
	G1_LOG(1, "done, pin= %d\n\n", button_irq2_gpio_spec.pin);
}

static uint8_t get_device_hw_id(void)
{
	uint8_t id1 = gpio_pin_get(GPIO0, 26);
	uint8_t id2 = gpio_pin_get(GPIO0, 25);
	uint8_t id3 = gpio_pin_get(GPIO0, 28);
	uint8_t id4 = gpio_pin_get(GPIO0, 27);
	uint8_t panel = gpio_pin_get_raw(GPIO0, 24);
	uint8_t tmr = gpio_pin_get_raw(GPIO0, 19);
	uint8_t nfc_lp = gpio_pin_get_raw(GPIO0, 21);
	uint8_t touch = gpio_pin_get_raw(GPIO0, 30);

	G1_LOG(1, "DEVICE_ID1_PIN: %d, device1_status: %d\n", 26, id1);
	G1_LOG(1, "DEVICE_ID2_PIN: %d, device2_status: %d\n", 25, id2);
	G1_LOG(1, "DEVICE_ID3_PIN: %d, device3_status: %d\n", 28, id3);
	G1_LOG(1, "DEVICE_ID4_PIN: %d, device4_status: %d\n", 27, id4);
	G1_LOG(1, "device_compat_val: %d %d %d %d\n", id1, id2, id3, id4);
	G1_LOG(1, "panel_power_status %d tmr_status %d nfc_lpd_status %d touch_power_status %d\n",
	       panel, tmr, nfc_lp, touch);
	return id1;
}

uint8_t button_init(void)
{
	gpio_init();

	if (!device_is_ready(button_irq_gpio_spec.port)) {
		G1_LOG(1, "SW0 GPIO port device not ready\n\n");
	} else if (gpio_pin_configure_dt(&button_irq_gpio_spec, GPIO_INPUT) >= 0) {
		gpio_init_callback(&button1_irq_cb, button1_pressed, BIT(button_irq_gpio_spec.pin));
		if (gpio_add_callback(button_irq_gpio_spec.port, &button1_irq_cb) < 0) {
			G1_LOG(1, "Unable to configure SW0 GPIO pin!\n\n");
		} else {
			gpio_pin_interrupt_configure_dt(&button_irq_gpio_spec,
							GPIO_INT_EDGE_TO_ACTIVE);
			G1_LOG(1, "done, pin= %d\n\n", button_irq_gpio_spec.pin);
		}
	}

	nfc_gpo_init();
	return get_device_hw_id();
}

int get_sw0_irq_level(void)
{
	return gpio_pin_get_raw(GPIO1, 9);
}
