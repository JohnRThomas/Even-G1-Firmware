#include <errno.h>
#include <zephyr/ztest.h>
#include <zephyr/fff.h>

#include "harness.h"
#include "devices.h"
#include "gpio_util.h"

/* for its static state */
#include "button/button.c"

FAKE_VOID_FUNC(trigger_nfc_event);
FAKE_VALUE_FUNC(int64_t, nfc_deadline_set, uint32_t);
FAKE_VALUE_FUNC(uint8_t, nfc_is_tag_type_ok);
int aw93203_ready;
uint8_t can_enter_low_power;

#define GPIO0 G1_GPIO0
#define GPIO1 G1_GPIO1

#define PIN_SW0       9  /* touch controller interrupt */
#define PIN_SW1       10 /* NFC GPO */
#define PIN_PANEL     24
#define PIN_TMR       19
#define PIN_NFC_LPD   21
#define PIN_TOUCH_PWR 30
#define PIN_PMIC_RST  23

#define GPIO0_OUTPUTS                                                                              \
	(BIT(PIN_PANEL) | BIT(PIN_TMR) | BIT(PIN_NFC_LPD) | BIT(PIN_TOUCH_PWR) | BIT(PIN_PMIC_RST))

#define SW_FLAGS (GPIO_INPUT | GPIO_PULL_UP | GPIO_ACTIVE_LOW)

static void buttons_before(void *fixture)
{
	ARG_UNUSED(fixture);
	/* reconfiguring a pin fires callbacks */
	g1_gpio_remove_callback(GPIO1, &button1_irq_cb);
	g1_gpio_remove_callback(GPIO1, &button2_irq_cb);
	g1_gpio_port_reset(GPIO0);
	g1_gpio_port_reset(GPIO1);

	button2_count = 0;
	touch_irq_pending = 0;
	aw93203_ready = 0;
	can_enter_low_power = 0;
	RESET_FAKE(trigger_nfc_event);
	RESET_FAKE(nfc_deadline_set);
	RESET_FAKE(nfc_is_tag_type_ok);
}

ZTEST_SUITE(buttons_gpio, NULL, NULL, buttons_before, NULL, NULL);

static void sw_set(gpio_pin_t pin, int level)
{
	zassert_ok(gpio_emul_input_set(GPIO1, pin, level), "P1.%02u <- %d", pin, level);
}

static void gpo_pulse(void)
{
	sw_set(PIN_SW1, 0);
	sw_set(PIN_SW1, 1);
}

static uint8_t init_with_straps(int p26, int p25, int p27, int p28)
{
	static const gpio_pin_t pins[] = { 26, 25, 27, 28 };
	const int levels[] = { p26, p25, p27, p28 };

	for (int i = 0; i < 4; i++) {
		zassert_ok(gpio_pin_configure(GPIO0, pins[i], GPIO_INPUT));
		zassert_ok(gpio_emul_input_set(GPIO0, pins[i], levels[i]));
	}
	return button_init();
}

/* ---- setup ---- */

ZTEST(buttons_gpio, test_init_configures_the_straps_as_plain_inputs)
{
	static const gpio_pin_t straps[] = { 26, 25, 28, 27 };

	button_init();

	for (int i = 0; i < ARRAY_SIZE(straps); i++) {
		zassert_equal(g1_gpio_flags(GPIO0, straps[i]), GPIO_INPUT, "P0.%u flags 0x%x",
			      straps[i], g1_gpio_flags(GPIO0, straps[i]));
	}
}

ZTEST(buttons_gpio, test_init_drives_the_control_lines_to_their_boot_levels)
{
	static const gpio_pin_t lines[] = { PIN_PANEL, PIN_TMR, PIN_NFC_LPD, PIN_TOUCH_PWR,
					    PIN_PMIC_RST };

	button_init();

	for (int i = 0; i < ARRAY_SIZE(lines); i++) {
		zassert_equal(g1_gpio_flags(GPIO0, lines[i]) & (GPIO_INPUT | GPIO_OUTPUT),
			      GPIO_INPUT | GPIO_OUTPUT, "P0.%u flags 0x%x", lines[i],
			      g1_gpio_flags(GPIO0, lines[i]));
	}
	zassert_equal(g1_gpio_outputs(GPIO0), BIT(PIN_TOUCH_PWR), "gpio0 outputs 0x%08x",
		      g1_gpio_outputs(GPIO0));
}

ZTEST(buttons_gpio, test_init_arms_sw0_on_the_falling_edge)
{
	gpio_flags_t flags;

	button_init();

	flags = g1_gpio_flags(GPIO1, PIN_SW0);
	zassert_equal(flags & ~G1_GPIO_INT_FLAGS, SW_FLAGS, "flags 0x%x", flags);
	zassert_equal(flags & G1_GPIO_INT_FLAGS, GPIO_INT_EDGE_FALLING, "flags 0x%x", flags);
}

ZTEST(buttons_gpio, test_init_arms_the_nfc_gpo_edge_by_tag_type)
{
	gpio_flags_t flags;

	nfc_is_tag_type_ok_fake.return_val = 1;
	button_init();
	flags = g1_gpio_flags(GPIO1, PIN_SW1);
	zassert_equal(flags & ~G1_GPIO_INT_FLAGS, SW_FLAGS, "flags 0x%x", flags);
	zassert_equal(flags & G1_GPIO_INT_FLAGS, GPIO_INT_EDGE_RISING, "tag ok: 0x%x", flags);

	nfc_is_tag_type_ok_fake.return_val = 0;
	nfc_gpo_init();
	flags = g1_gpio_flags(GPIO1, PIN_SW1);
	zassert_equal(flags & G1_GPIO_INT_FLAGS, GPIO_INT_EDGE_FALLING, "tag not ok: 0x%x", flags);
}

/* ---- SW0 ---- */

ZTEST(buttons_gpio, test_sw0_falling_edge_flags_the_touch_irq_once_touch_is_ready)
{
	button_init();
	aw93203_ready = 1;

	sw_set(PIN_SW0, 0);

	zassert_equal(touch_irq_pending, 1);
	zassert_equal(k_sem_count_get(&g1_state()->touch_sem), 1);
	zassert_equal(g1_gpio_flags(GPIO1, PIN_SW0) & G1_GPIO_INT_FLAGS, GPIO_INT_EDGE_FALLING);
}

ZTEST(buttons_gpio, test_sw0_edge_before_touch_is_ready_is_ignored)
{
	button_init();

	sw_set(PIN_SW0, 0);

	zassert_equal(touch_irq_pending, 0);
	zassert_equal(k_sem_count_get(&g1_state()->touch_sem), 0);
	zassert_equal(g1_gpio_flags(GPIO1, PIN_SW0) & G1_GPIO_INT_FLAGS, GPIO_INT_EDGE_FALLING);
}

ZTEST(buttons_gpio, test_sw0_release_does_not_interrupt)
{
	button_init();
	aw93203_ready = 1;
	sw_set(PIN_SW0, 0);
	touch_irq_pending = 0;
	k_sem_reset(&g1_state()->touch_sem);

	sw_set(PIN_SW0, 1);

	zassert_equal(touch_irq_pending, 0);
	zassert_equal(k_sem_count_get(&g1_state()->touch_sem), 0);
}

ZTEST(buttons_gpio, test_sw0_irq_level_reads_the_raw_line)
{
	button_init();
	zassert_equal(get_sw0_irq_level(), 1);

	sw_set(PIN_SW0, 0);
	zassert_equal(get_sw0_irq_level(), 0);

	sw_set(PIN_SW0, 1);
	zassert_equal(get_sw0_irq_level(), 1);
}

/* ---- SW1 ---- */

ZTEST(buttons_gpio, test_every_gpo_pulse_triggers_an_nfc_event)
{
	button_init();

	gpo_pulse();
	gpo_pulse();
	gpo_pulse();

	zassert_equal(trigger_nfc_event_fake.call_count, 3);
}

ZTEST(buttons_gpio, test_fifth_gpo_pulse_cancels_pending_low_power)
{
	button_init();
	can_enter_low_power = 2;

	for (int i = 0; i < 4; i++) {
		gpo_pulse();
	}
	zassert_equal(nfc_deadline_set_fake.call_count, 0);
	zassert_equal(can_enter_low_power, 2);

	gpo_pulse();
	zassert_equal(nfc_deadline_set_fake.call_count, 1);
	zassert_equal(nfc_deadline_set_fake.arg0_val, 0);
	zassert_equal(can_enter_low_power, 0);
}

ZTEST(buttons_gpio, test_fifth_gpo_pulse_in_work_mode_8_cancels_low_power)
{
	button_init();
	g1_state()->work_mode = 8;
	can_enter_low_power = 1;

	for (int i = 0; i < 5; i++) {
		gpo_pulse();
	}

	zassert_equal(nfc_deadline_set_fake.call_count, 1);
	zassert_equal(nfc_deadline_set_fake.arg0_val, 0);
	zassert_equal(can_enter_low_power, 0);
}

ZTEST(buttons_gpio, test_gpo_pulses_are_not_counted_otherwise)
{
	button_init();
	can_enter_low_power = 1;

	for (int i = 0; i < 10; i++) {
		gpo_pulse();
	}

	zassert_equal(nfc_deadline_set_fake.call_count, 0);
	zassert_equal(can_enter_low_power, 1);
}

/* ---- role ---- */

/* P0.26: 0 master, 1 slave */
ZTEST(buttons_gpio, test_init_returns_the_p0_26_role_strap)
{
	zassert_equal(init_with_straps(0, 1, 1, 1), 0);
	zassert_equal(init_with_straps(1, 0, 0, 0), 1);
}

/* ---- power ---- */

ZTEST(buttons_gpio, test_power_helpers_toggle_exactly_their_line)
{
	static const struct {
		int (*fn)(void);
		const char *name;
		gpio_pin_t pin;
		int level;
	} steps[] = {
		/* each step changes its line */
		{ panel_gpio_enable, "panel_gpio_enable", PIN_PANEL, 1 },
		{ panel_gpio_disable, "panel_gpio_disable", PIN_PANEL, 0 },
		{ nfc_lpd_on, "nfc_lpd_on", PIN_NFC_LPD, 1 },
		{ nfc_lpd_off, "nfc_lpd_off", PIN_NFC_LPD, 0 },
		{ touch_power_off, "touch_power_off", PIN_TOUCH_PWR, 0 },
		{ touch_power_on, "touch_power_on", PIN_TOUCH_PWR, 1 },
		{ reset_pmic_on, "reset_pmic_on", PIN_PMIC_RST, 1 },
		{ reset_pmic_off, "reset_pmic_off", PIN_PMIC_RST, 0 },
	};

	button_init();
	for (int i = 0; i < ARRAY_SIZE(steps); i++) {
		gpio_port_value_t before = g1_gpio_outputs(GPIO0);
		gpio_port_value_t want = (before & ~BIT(steps[i].pin)) |
					 (steps[i].level ? BIT(steps[i].pin) : 0);

		zassert_equal(steps[i].fn(), 0, "%s", steps[i].name);
		zassert_equal(g1_gpio_outputs(GPIO0), want, "%s: outputs 0x%08x, want 0x%08x",
			      steps[i].name, g1_gpio_outputs(GPIO0), want);
		zassert_not_equal(before, want, "%s: step does not change its line", steps[i].name);
	}
}

/* ---- reinit ---- */

/* a registered callback is not added twice */
ZTEST(buttons_gpio, test_reinit_restores_the_boot_state_without_doubling_callbacks)
{
	gpio_flags_t sw0_flags;
	uint8_t role;

	/* count every event (not binary) */
	k_sem_init(&g1_state()->touch_sem, 0, 10);
	role = init_with_straps(1, 0, 0, 0);
	sw0_flags = g1_gpio_flags(GPIO1, PIN_SW0);
	panel_gpio_enable();
	touch_power_off();
	reset_pmic_on();

	zassert_equal(button_reinit(), role);

	zassert_equal(g1_gpio_outputs(GPIO0), BIT(PIN_TOUCH_PWR), "gpio0 outputs 0x%08x",
		      g1_gpio_outputs(GPIO0));
	zassert_equal(g1_gpio_flags(GPIO1, PIN_SW0), sw0_flags);

	/* reinit fired the callbacks */
	k_sem_reset(&g1_state()->touch_sem);
	touch_irq_pending = 0;
	aw93203_ready = 1;
	sw_set(PIN_SW0, 0);
	zassert_equal(k_sem_count_get(&g1_state()->touch_sem), 1);
}

/* ---- no gpio1 ---- */

ZTEST(buttons_gpio, test_init_without_gpio1_skips_both_interrupt_lines)
{
	g1_device_set_ready(GPIO1, false);

	zassert_equal(init_with_straps(1, 0, 0, 0), 1);

	zassert_equal(g1_gpio_flags(GPIO1, PIN_SW0), 0, "P1.09 configured");
	zassert_equal(g1_gpio_flags(GPIO1, PIN_SW1), 0, "P1.10 configured");
	zassert_equal(g1_gpio_outputs(GPIO0), BIT(PIN_TOUCH_PWR));
}
