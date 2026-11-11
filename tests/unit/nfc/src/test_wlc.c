/* Charge state: 1 charging, 2 no field, 3 updating, 4 full. */
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "devices.h"
#include "fake_npm1300.h"
#include "nfc_test.h"

#include "nfc/nfc_wlc.c"

#define E    TAG_E
#define ADC0 DEVICE_DT_GET(DT_NODELABEL(adc0))
#define I2C1 DEVICE_DT_GET(DT_NODELABEL(i2c1))

void nfc_test_set_pin_mv(uint32_t mv)
{
	zassert_ok(adc_emul_const_value_set(ADC0, 3, mv));
}

uint8_t nfc_test_sum8(const uint8_t *buf, size_t n)
{
	uint8_t s = 0;

	for (size_t i = 0; i < n; i++) {
		s += buf[i];
	}
	return s;
}

void nfc_wlc_test_reset(void)
{
	memset(&wlc_listener, 0, sizeof(wlc_listener));
	wlc_listener.state = 1;
	wlc_info_sent = 1;
	memset(&nfc_charge, 0, sizeof(nfc_charge));
	nfc_tick_start = 0;
	nfc_field_mv = 0;
	memset(&CCFileStruct, 0, sizeof(CCFileStruct));
	nfc_field_tick = 0;
	wlcCapabilityItem = 0;
	wlcStatusInfoItem = 0;
	wlc_wait_ms = 0;
	nfc_charge_tick = 0;
	wlc_tag_configured = 0;
	ndefRecordPoolIndex = 0;
	nfc_tag_type_ok = 0;
	nfc_field_on = 0;
	wlcCapabilityTemp = 0;
	wlc_field_present = 0;
	nfc_low_field = 0;
	wlc_last_battery = 0;
	global_st25dv_context_ptr = eeprom_st25dv_init();
	adc_nfc_init();
	nfc_test_set_pin_mv(0);
}

#define STAT_CC 0x08

static int charger_charging(const struct device *dev, uint8_t base, uint8_t off, uint8_t *v)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(base);
	ARG_UNUSED(off);
	*v = STAT_CC;
	return 0;
}

static void wlc_before(void *fixture)
{
	ARG_UNUSED(fixture);
	nfc_wlc_test_reset();
	nfc_pt_test_reset();
	/* charger already charging */
	mfd_npm1300_reg_read_fake.custom_fake = charger_charging;
	st25dv_emul_set_write_time_ms(E, 1);
}

#define GPO_PORT DEVICE_DT_GET(DT_NODELABEL(gpio1))
#define GPO_PIN  10

static struct gpio_callback gpo_cb;
static bool gpo_cb_added;
static int gpo_falling;

static void gpo_edge(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	gpo_falling++;
}

static void gpo_line_attach(void)
{
	zassert_ok(gpio_pin_configure(GPO_PORT, GPO_PIN, GPIO_INPUT | GPIO_PULL_UP));
	st25dv_emul_connect_gpo(E, GPO_PORT, GPO_PIN);
	gpio_init_callback(&gpo_cb, gpo_edge, BIT(GPO_PIN));
	zassert_ok(gpio_add_callback(GPO_PORT, &gpo_cb));
	gpo_cb_added = true;
	zassert_ok(gpio_pin_interrupt_configure(GPO_PORT, GPO_PIN, GPIO_INT_EDGE_FALLING));
	gpo_falling = 0;
}

static void wlc_after(void *fixture)
{
	ARG_UNUSED(fixture);
	if (gpo_cb_added) {
		(void)gpio_pin_interrupt_configure(GPO_PORT, GPO_PIN, GPIO_INT_DISABLE);
		(void)gpio_remove_callback(GPO_PORT, &gpo_cb);
		(void)gpio_pin_configure(GPO_PORT, GPO_PIN, GPIO_DISCONNECTED);
		gpo_cb_added = false;
	}
}

ZTEST_SUITE(nfc_wlc, NULL, NULL, wlc_before, wlc_after, NULL);

static uint32_t field_reading(void)
{
	uint32_t mv = 0xdeadbeef;

	zassert_equal(nfc_read_field_mv(&mv), 1);
	return mv;
}

static void listener_defaults(struct wlc_listener *w)
{
	static const uint8_t cap[10] = {0, 0, 0, 1, 9, 9, 0x1e, 3, 0x0a, 0};
	static const uint8_t stai[9] = {1, 5, 0, 0, 0, 0, 0, 0, 0};

	memset(w, 0, sizeof(*w));
	w->active = 1;
	memcpy(w->cap_info, cap, sizeof(cap));
	memcpy(w->stai_info, stai, sizeof(stai));
	w->fod_info[1] = 2;
}

static const uint8_t *mem(void)
{
	return st25dv_emul_user_mem(E);
}

static void expect_mem(uint32_t at, const uint8_t *exp, size_t n)
{
	char g[3 * 48 + 4];
	char e[3 * 48 + 4];

	g1_hex_format(&mem()[at], n, g, sizeof(g));
	g1_hex_format(exp, n, e, sizeof(e));
	zassert_mem_equal(&mem()[at], exp, n, "tag at %u\n  got  %s\n  want %s", at, g, e);
}

/* ---- field voltage ---- */

ZTEST(nfc_wlc, test_field_reading_is_ten_times_the_pin_millivolts)
{
	nfc_test_set_pin_mv(600);
	zassert_within(field_reading(), 6000, 10);

	nfc_test_set_pin_mv(55);
	zassert_within(field_reading(), 550, 10);
}

ZTEST(nfc_wlc, test_no_voltage_at_the_pin_reads_as_no_field)
{
	zassert_equal(field_reading(), 0);
}

ZTEST(nfc_wlc, test_pin_voltage_of_1_2v_or_more_reads_as_no_field)
{
	nfc_test_set_pin_mv(1150);
	zassert_within(field_reading(), 11500, 10);
	nfc_test_set_pin_mv(1250);
	zassert_equal(field_reading(), 0);
}

static int adc_fails(const struct device *dev, unsigned int chan, void *data, uint32_t *mv)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(chan);
	ARG_UNUSED(data);
	ARG_UNUSED(mv);
	return -EIO;
}

ZTEST(nfc_wlc, test_adc_failure_reads_as_no_field)
{
	zassert_ok(adc_emul_value_func_set(ADC0, 3, adc_fails, NULL));
	zassert_equal(field_reading(), 0);
	nfc_test_set_pin_mv(0);
}

ZTEST(nfc_wlc, test_field_is_detected_above_5_1v)
{
	nfc_test_set_pin_mv(530);
	zassert_true(nfc_field_detected());
	zassert_within(nfc_get_field_mv(), 5300, 10, "the reading is kept");

	nfc_test_set_pin_mv(490);
	zassert_false(nfc_field_detected());
	zassert_within(nfc_get_field_mv(), 4900, 10);
}

ZTEST(nfc_wlc, test_vdc_rect_is_a_fresh_reading)
{
	nfc_test_set_pin_mv(700);
	zassert_within(chargerGetVdcRect(), 7000, 10);
	nfc_test_set_pin_mv(0);
	zassert_equal(chargerGetVdcRect(), 0);
	zassert_equal(nfc_get_field_mv(), 0);
}

static uint8_t charge_state_at(uint32_t pin_mv, uint8_t battery)
{
	struct nfc_charge c = {.state = 3};

	g1_state()->battery_left = battery;
	nfc_test_set_pin_mv(pin_mv);
	nfc_field_voltage_update();
	nfc_charge_update_state(&c);
	return c.state;
}

ZTEST(nfc_wlc, test_charge_state_is_no_field_without_the_case_field)
{
	zassert_equal(charge_state_at(0, 50), 2);
	zassert_equal(charge_state_at(400, 50), 2);
}

ZTEST(nfc_wlc, test_charge_state_is_charging_in_the_field_below_100_percent)
{
	zassert_equal(charge_state_at(600, 50), 1);
	zassert_equal(charge_state_at(600, 99), 1);
}

ZTEST(nfc_wlc, test_charge_state_is_full_in_the_field_at_100_percent)
{
	zassert_equal(charge_state_at(600, 100), 4);
}

ZTEST(nfc_wlc, test_field_appearing_is_reported_present_and_keeps_the_listener_state)
{
	struct wlc_listener w;
	uint8_t present = 0;

	listener_defaults(&w);
	w.state = 5;
	nfc_test_set_pin_mv(600);

	wlc_update_field_state(&w, &present);
	zassert_equal(present, 1);
	zassert_equal(w.state, 5);
}

ZTEST(nfc_wlc, test_field_lost_while_not_charging_restarts_the_listener)
{
	struct wlc_listener w;
	uint8_t present = 0;

	listener_defaults(&w);
	nfc_test_set_pin_mv(600);
	wlc_update_field_state(&w, &present);
	w.state = 10;

	nfc_test_set_pin_mv(0);
	wlc_update_field_state(&w, &present);
	zassert_equal(present, 0);
	zassert_equal(w.state, 1, "back to the initialisation state");
}

ZTEST(nfc_wlc, test_field_lost_while_charging_keeps_the_listener_state)
{
	struct wlc_listener w;
	uint8_t present = 0;

	listener_defaults(&w);
	nfc_test_set_pin_mv(600);
	wlc_update_field_state(&w, &present);
	w.state = 14;
	w.charging = 1;

	nfc_test_set_pin_mv(0);
	wlc_update_field_state(&w, &present);
	zassert_equal(present, 0);
	zassert_equal(w.state, 14);
}

ZTEST(nfc_wlc, test_no_field_without_an_earlier_field_changes_nothing)
{
	struct wlc_listener w;
	uint8_t present = 1;

	listener_defaults(&w);
	w.state = 10;

	wlc_update_field_state(&w, &present);
	zassert_equal(present, 0);
	zassert_equal(w.state, 10);
}

/* DEFECT: tick counts are compared with ms constants; waits are 32.768x too short. */
#if CONFIG_SYS_CLOCK_TICKS_PER_SEC != 1000
ZTEST_EXPECT_FAIL(nfc_wlc, test_tick_elapsed_counts_milliseconds);
ZTEST_EXPECT_FAIL(nfc_wlc, test_listener_settles_150ms_after_writing_its_records);
ZTEST_EXPECT_FAIL(nfc_wlc, test_listener_waits_the_wlc_interval_before_polling_again);
ZTEST_EXPECT_FAIL(nfc_wlc, test_field_loss_is_reported_after_3s_without_field);
#endif

ZTEST(nfc_wlc, test_tick_elapsed_counts_milliseconds)
{
	nfc_tick_reset();
	g1_clock_advance_ms(150);

	zassert_within(nfc_tick_elapsed(), 150, 1, "%u", nfc_tick_elapsed());
}

ZTEST(nfc_wlc, test_listener_settles_150ms_after_writing_its_records)
{
	struct wlc_listener w;

	listener_defaults(&w);
	w.state = 3;
	wlc_listener_process(&w);
	zassert_equal(w.state, 4);

	g1_clock_advance_ms(140);
	wlc_listener_process(&w);
	zassert_equal(w.state, 4, "still settling after 140 ms");
	g1_clock_advance_ms(20);
	wlc_listener_process(&w);
	zassert_equal(w.state, 9);
}

ZTEST(nfc_wlc, test_listener_waits_the_wlc_interval_before_polling_again)
{
	struct wlc_listener w;

	listener_defaults(&w);
	w.state = 9;
	wlc_listener_process(&w); /* CAP_WT_INT 9: 2^12 - 50 = 4046 ms */
	zassert_equal(w.state, 10);

	g1_clock_advance_ms(4000);
	wlc_listener_process(&w);
	zassert_equal(w.state, 10, "still waiting after 4000 ms");
	g1_clock_advance_ms(100);
	wlc_listener_process(&w);
	zassert_equal(w.state, 11);
}

ZTEST(nfc_wlc, test_field_loss_is_reported_after_3s_without_field)
{
	struct wlc_listener w;
	struct nfc_charge c = {0};

	listener_defaults(&w);
	g1_state()->battery_left = 50;
	nfc_test_set_pin_mv(600);
	nfc_charge_refresh(&c, &w);
	zassert_equal(c.state, 1);

	nfc_test_set_pin_mv(0);
	g1_clock_advance_ms(2900);
	nfc_charge_refresh(&c, &w);
	zassert_equal(w.field_lost, 0, "not yet after 2.9 s");
	g1_clock_advance_ms(200);
	nfc_charge_refresh(&c, &w);
	zassert_equal(w.field_lost, 1);
}

/* ---- type 5 tag ---- */

ZTEST(nfc_wlc, test_tag_size_is_blocks_times_block_size)
{
	zassert_equal(BSP_NFCTAG_GetByteSize(), 512, "ST25DV04KC");
	st25dv_emul_set_mem_size(E, 0x01ff, 0x03);
	zassert_equal(BSP_NFCTAG_GetByteSize(), 2048, "ST25DV16KC");
	st25dv_emul_set_mem_size(E, 0x07ff, 0x03);
	zassert_equal(BSP_NFCTAG_GetByteSize(), 8192, "ST25DV64KC");
}

ZTEST(nfc_wlc, test_blank_tag_is_not_formatted)
{
	zassert_equal(NfcType5_NDEFDetection(), NDEF_ERROR_NOT_FORMATED);
	zassert_equal(CCFileStruct.State, TT5_NO_NDEF);
}

static void put(uint32_t at, const uint8_t *b, size_t n)
{
	memcpy(&st25dv_emul_user_mem(E)[at], b, n);
}

ZTEST(nfc_wlc, test_cc_with_an_empty_ndef_tlv_is_an_initialized_tag)
{
	put(0, (const uint8_t *)"\xe1\x40\x40\x01\x03\x00", 6);

	zassert_equal(NfcType5_NDEFDetection(), NDEF_OK);
	zassert_equal(CCFileStruct.State, TT5_INITIALIZED);
	zassert_equal(CCFileStruct.NDEF_offset, 4);
	zassert_equal(CCFileStruct.MemorySize, 0x40);
}

ZTEST(nfc_wlc, test_wrong_magic_or_version_is_not_formatted)
{
	put(0, (const uint8_t *)"\xe3\x40\x40\x01\x03\x00", 6);
	zassert_equal(NfcType5_NDEFDetection(), NDEF_ERROR_NOT_FORMATED, "magic");
	put(0, (const uint8_t *)"\xe1\x80\x40\x01", 4);
	zassert_equal(NfcType5_NDEFDetection(), NDEF_ERROR_NOT_FORMATED, "version 2.0");
	put(0, (const uint8_t *)"\xe1\x44\x40\x01", 4);
	zassert_equal(NfcType5_NDEFDetection(), NDEF_ERROR_NOT_FORMATED, "no read access");
}

/*
 * CC byte 1 (NFC Forum T5T): version in b7..b4, read access in b3..b2, write access in b1..b0.
 * 43h is version 1.0, read always, write 3: only read access decides whether the tag is
 * formatted, so the CC is kept and the NDEF TLV goes after it.
 */
ZTEST(nfc_wlc, test_cc_with_write_access_bits_set_is_a_formatted_tag)
{
	static const uint8_t exp[] = {0xe1, 0x43, 0x40, 0x01, 0x03, 0x02, 0x12, 0x34, 0xfe};

	put(0, (const uint8_t *)"\xe1\x43\x40\x01\x03\x00", 6);
	zassert_equal(NfcType5_NDEFDetection(), NDEF_OK);

	zassert_equal(nfc_write_ndef((const uint8_t *)"\x12\x34", 2), NDEF_OK);
	expect_mem(0, exp, sizeof(exp));
}

ZTEST(nfc_wlc, test_8_byte_cc_puts_the_ndef_tlv_at_offset_8)
{
	put(0, (const uint8_t *)"\xe2\x40\x00\x01\x00\x00\x00\x40\x03\x00", 10);

	zassert_equal(NfcType5_NDEFDetection(), NDEF_OK);
	zassert_equal(CCFileStruct.NDEF_offset, 8);
	zassert_equal(CCFileStruct.ExtMemorySize, 0x0040, "MLEN from bytes 6..7, big endian");
	zassert_equal(CCFileStruct.State, TT5_INITIALIZED);
}

/* DEFECT: a proprietary TLV is skipped by L only, without its T and L bytes. */
ZTEST_EXPECT_FAIL(nfc_wlc, test_proprietary_tlv_is_skipped_to_the_ndef_tlv);
ZTEST(nfc_wlc, test_proprietary_tlv_is_skipped_to_the_ndef_tlv)
{
	put(0, (const uint8_t *)"\xe1\x40\x40\x01\xfd\x02\x55\x55\x03\x00", 10);

	zassert_equal(NfcType5_NDEFDetection(), NDEF_OK);
	zassert_equal(CCFileStruct.NDEF_offset, 8);
}

ZTEST(nfc_wlc, test_tt5_init_writes_the_cc_and_an_empty_ndef_tlv)
{
	static const uint8_t exp[] = {0xe1, 0x40, 0x40, 0x01, 0x03, 0x00};

	CCFileStruct.MagicNumber = 0xe1;
	CCFileStruct.Version = 0x40;
	CCFileStruct.MemorySize = 0x40;
	CCFileStruct.TT5Tag = 0x01;

	zassert_equal(NfcType5_TT5Init(), NDEF_OK);
	expect_mem(0, exp, sizeof(exp));
	zassert_equal(CCFileStruct.NDEF_offset, 4);
}

ZTEST(nfc_wlc, test_ndef_on_a_blank_tag_formats_it_then_writes_the_tlv)
{
	static const uint8_t exp[] = {
		0xe1, 0x40, 0x40, 0x01, 0x03, 0x03, 0xab, 0xcd, 0xef, 0xfe,
	};

	zassert_equal(nfc_write_ndef((const uint8_t *)"\xab\xcd\xef", 3), NDEF_OK);
	expect_mem(0, exp, sizeof(exp));
}

ZTEST(nfc_wlc, test_ndef_on_a_formatted_tag_keeps_its_cc)
{
	static const uint8_t exp[] = {0xe1, 0x40, 0x40, 0x05, 0x03, 0x02, 0x12, 0x34, 0xfe};

	put(0, (const uint8_t *)"\xe1\x40\x40\x05\x03\x00", 6);

	zassert_equal(nfc_write_ndef((const uint8_t *)"\x12\x34", 2), NDEF_OK);
	expect_mem(0, exp, sizeof(exp));
}

ZTEST(nfc_wlc, test_ndef_of_255_bytes_or_more_has_a_3_byte_tlv_length)
{
	static uint8_t msg[255];
	static const uint8_t hdr[] = {0x03, 0xff, 0x00, 0xff};

	for (int i = 0; i < 255; i++) {
		msg[i] = (uint8_t)i;
	}
	put(0, (const uint8_t *)"\xe1\x40\x40\x01\x03\x00", 6);

	zassert_equal(nfc_write_ndef(msg, sizeof(msg)), NDEF_OK);
	expect_mem(4, hdr, sizeof(hdr));
	expect_mem(8, msg, sizeof(msg));
	zassert_equal(mem()[8 + 255], 0xfe);
}

/* DEFECT: the chip takes 256 bytes per write; the failure returns NDEF_OK. */
ZTEST_EXPECT_FAIL(nfc_wlc, test_ndef_longer_than_256_bytes_is_written);
ZTEST(nfc_wlc, test_ndef_longer_than_256_bytes_is_written)
{
	static uint8_t msg[300];
	static const uint8_t hdr[] = {0x03, 0xff, 0x01, 0x2c};

	for (int i = 0; i < 300; i++) {
		msg[i] = (uint8_t)i;
	}
	put(0, (const uint8_t *)"\xe1\x40\x40\x01\x03\x00", 6);

	nfc_write_ndef(msg, sizeof(msg));
	expect_mem(4, hdr, sizeof(hdr));
	expect_mem(8, msg, sizeof(msg));
	zassert_equal(mem()[8 + 300], 0xfe);
}

/* DEFECT: NDEF_ERROR_MEMORY_TAG is returned as NDEF_OK. */
ZTEST_EXPECT_FAIL(nfc_wlc, test_ndef_larger_than_the_tag_is_reported_as_an_error);
ZTEST(nfc_wlc, test_ndef_larger_than_the_tag_is_reported_as_an_error)
{
	static uint8_t msg[600];

	put(0, (const uint8_t *)"\xe1\x40\x40\x01\x03\x00", 6);

	zassert_equal(NfcType5_WriteNDEF(sizeof(msg), msg), NDEF_ERROR_MEMORY_TAG);
	zassert_equal(mem()[5], 0x00, "nothing written");
	zassert_not_equal(nfc_write_ndef(msg, sizeof(msg)), NDEF_OK);
}

/* ---- WLC message ---- */

static const uint8_t cap_default[10] = {0, 0, 0, 1, 9, 9, 0x1e, 3, 0x0a, 0};

#define WLCCAP_PAYLOAD 0x20, 0x01, 0x09, 0x1e, 0x03, 0x0a

ZTEST(nfc_wlc, test_wlc_message_has_the_capability_and_status_records)
{
	static const uint8_t stai[9] = {1, 0x05, 0x32, 0, 0x01, 0, 0, 0, 0};
	static const uint8_t fod[3] = {0, 2, 0};
	static const uint8_t exp[] = {
		0xe1, 0x40, 0x40, 0x01, 0x03, 0x1c,
		0x91, 0x06, 0x06, 'W', 'L', 'C', 'C', 'A', 'P', WLCCAP_PAYLOAD,
		0x51, 0x07, 0x03, 'W', 'L', 'C', 'S', 'T', 'A', 'I', 0x05, 0x32, 0x01,
		0xfe,
	};

	zassert_equal(wlc_write_ndef_message(cap_default, stai, fod), 0);
	expect_mem(0, exp, sizeof(exp));
}

ZTEST(nfc_wlc, test_wlc_message_without_status_is_the_capability_record_alone)
{
	static const uint8_t stai[9] = {0};
	static const uint8_t fod[3] = {0};
	static const uint8_t exp[] = {
		0xe1, 0x40, 0x40, 0x01, 0x03, 0x0f,
		0xd1, 0x06, 0x06, 'W', 'L', 'C', 'C', 'A', 'P', WLCCAP_PAYLOAD,
		0xfe,
	};

	zassert_equal(wlc_write_ndef_message(cap_default, stai, fod), 0);
	expect_mem(0, exp, sizeof(exp));
}

/* DEFECT: WLCFOD is decoded from the encode buffer and comes out as WLCCAP. */
ZTEST_EXPECT_FAIL(nfc_wlc, test_wlc_foreign_object_record_is_wlcfod_with_its_two_bytes);
ZTEST(nfc_wlc, test_wlc_foreign_object_record_is_wlcfod_with_its_two_bytes)
{
	static const uint8_t stai[9] = {0};
	static const uint8_t fod[3] = {1, 0x5a, 0xa5};
	static const uint8_t exp[] = {
		0xe1, 0x40, 0x40, 0x01, 0x03, 0x1a,
		0x91, 0x06, 0x06, 'W', 'L', 'C', 'C', 'A', 'P', WLCCAP_PAYLOAD,
		0x51, 0x06, 0x02, 'W', 'L', 'C', 'F', 'O', 'D', 0x5a, 0xa5,
		0xfe,
	};

	zassert_equal(wlc_write_ndef_message(cap_default, stai, fod), 0);
	expect_mem(0, exp, sizeof(exp));
}

/* DEFECT: 39 bytes are encoded into a 32-byte buffer. */
ZTEST_EXPECT_FAIL(nfc_wlc, test_wlc_message_with_all_three_records_is_written_whole);
ZTEST(nfc_wlc, test_wlc_message_with_all_three_records_is_written_whole)
{
	static const uint8_t stai[9] = {1, 0x05, 0x32, 0, 0x01, 0, 0, 0, 0};
	static const uint8_t fod[3] = {1, 0x5a, 0xa5};

	zassert_equal(wlc_write_ndef_message(cap_default, stai, fod), 0);
	zassert_equal(mem()[5], 39, "NDEF TLV length");
}

/* ---- nfc_init() ---- */

ZTEST(nfc_wlc, test_init_reads_the_identity_of_an_st25dv04kc_and_accepts_it)
{
	zassert_equal(nfc_init(), 0);
	zassert_equal_ptr(global_st25dv_context_ptr, &global_st25dv_context);
	zassert_equal(nfc_is_tag_type_ok(), 1);
}

ZTEST(nfc_wlc, test_tag_type_is_ok_for_the_kc_series_ie_product_codes_only)
{
	static const struct {
		uint8_t code;
		uint8_t ok;
	} cases[] = {
		{0x50, 1}, /* 04KC */
		{0x51, 1}, /* 16KC / 64KC */
		{0x24, 0}, /* 04K */
		{0x26, 0}, /* 16K / 64K */
		{0x00, 0},
	};
	uint8_t uid[8] = {0x10, 0x32, 0x54, 0x76, 0x98, 0x00, 0x02, 0xe0};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		uid[5] = cases[i].code;
		st25dv_emul_set_uid(E, uid);
		nfc_tag_type_ok = 0;
		nfc_init();
		zassert_equal(nfc_is_tag_type_ok(), cases[i].ok, "product code %02x",
			      cases[i].code);
	}
}

ZTEST(nfc_wlc, test_init_on_a_silent_tag_leaves_the_tag_type_unknown)
{
	st25dv_emul_set_nak(E, true);

	zassert_equal(nfc_init(), 0);
	zassert_equal(nfc_is_tag_type_ok(), 0);
}

ZTEST(nfc_wlc, test_init_without_the_bus_stops_before_the_tag)
{
	global_st25dv_context_ptr = NULL;
	g1_device_set_ready(I2C1, false);

	zassert_equal(nfc_init(), 0);
	zassert_is_null(global_st25dv_context_ptr);
	zassert_equal(st25dv_emul_xfers(E), 0);
	zassert_equal(nfc_is_tag_type_ok(), 0);
}

ZTEST(nfc_wlc, test_init_starts_out_of_the_case)
{
	set_box_status(BOX_IN_BOX, 1);
	set_box_status(BOX_LID_CLOSED, 1);
	set_box_status(2, 1);

	nfc_init();
	zassert_equal(is_in_box(), 0);
	zassert_equal(is_box_lid_closed(), 0);
	zassert_equal(is_box_charging(), 0);
}

/* ---- nfc_wlc_process() ---- */

static int find_xfer(int from, uint8_t addr, const uint8_t *wr, size_t n)
{
	int count = MIN(st25dv_emul_xfers(E), ST25DV_EMUL_LOG);

	for (int i = from; i < count; i++) {
		const struct st25dv_emul_xfer *x = st25dv_emul_xfer(E, i);

		if (x->addr == addr && x->wr_len == n && memcmp(x->wr, wr, n) == 0 &&
		    x->ret == 0) {
			return i;
		}
	}
	return -1;
}

ZTEST(nfc_wlc, test_first_pass_configures_the_tag_inside_the_i2c_session)
{
	static const uint8_t open[] = {0x09, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0x09,
				       0,    0,    0, 0, 0, 0, 0, 0};
	static const uint8_t close[] = {0x09, 0x00, 0x12, 0x34, 0x56, 0x78, 0x13, 0x24, 0x56,
					0x78, 0x09, 0x12, 0x34, 0x56, 0x78, 0x13, 0x24, 0x56,
					0x78};
	static const uint8_t gpo1[] = {0x00, 0x00, 0xff};
	int i_open;
	int i_gpo;
	int i_close;

	zassert_ok(st25dv_write_eh_ctrl_dyn(global_st25dv_context_ptr, ST25DV_EMUL_EH_EN));

	zassert_equal(nfc_wlc_process(), 0);

	zassert_equal(st25dv_emul_get_sys(E, ST25DV_EMUL_SYS_GPO1), 0xff);
	zassert_equal(st25dv_emul_get_sys(E, ST25DV_EMUL_SYS_GPO2), 0x0c);
	zassert_equal(st25dv_emul_get_dyn(E, ST25DV_EMUL_DYN_EH_CTRL) & ST25DV_EMUL_EH_EN, 0,
		      "energy harvesting off");
	zassert_false(st25dv_emul_session_open(E), "session closed again");

	i_open = find_xfer(0, 0x57, open, sizeof(open));
	i_gpo = find_xfer(0, 0x57, gpo1, sizeof(gpo1));
	i_close = find_xfer(0, 0x57, close, sizeof(close));
	zassert_true(i_open >= 0 && i_gpo > i_open && i_close > i_gpo,
		     "password %d, GPO1 %d, wrong password %d", i_open, i_gpo, i_close);
}

ZTEST(nfc_wlc, test_tag_is_configured_on_the_first_pass_only)
{
	static const uint8_t pwd_addr[] = {0x09, 0x00};
	int n;

	nfc_wlc_process();
	n = st25dv_emul_xfers(E);
	nfc_wlc_process();

	for (int i = n; i < MIN(st25dv_emul_xfers(E), ST25DV_EMUL_LOG); i++) {
		zassert_false(memcmp(st25dv_emul_xfer(E, i)->wr, pwd_addr, 2) == 0 &&
				      st25dv_emul_xfer(E, i)->addr == 0x57,
			      "password presented again (transfer %d)", i);
	}
}

ZTEST(nfc_wlc, test_third_pass_puts_the_wlc_records_with_the_battery_level_on_the_tag)
{
	static const uint8_t exp_head[] = {
		0xe1, 0x40, 0x40, 0x01, 0x03, 0x1c,
		0x91, 0x06, 0x06, 'W', 'L', 'C', 'C', 'A', 'P', WLCCAP_PAYLOAD,
		0x51, 0x07, 0x03, 'W', 'L', 'C', 'S', 'T', 'A', 'I', 0x05, 0x4b,
	};

	g1_state()->battery_left = 75;
	for (int i = 0; i < 3; i++) {
		zassert_equal(nfc_wlc_process(), 0);
	}

	expect_mem(0, exp_head, sizeof(exp_head));
	zassert_equal(mem()[34], 0xfe);
}

/* ---- GPO line ---- */

static const uint8_t case_query[] = {0x13, 0x01, 0x02, 0x00, 0x16};

ZTEST(nfc_wlc, test_configured_tag_interrupts_the_lens_on_field_and_case_message)
{
	zassert_equal(nfc_wlc_process(), 0);
	gpo_line_attach();

	st25dv_emul_rf_field(E, true);
	zassert_equal(gpo_falling, 1, "field appeared");
	st25dv_emul_enable_mailbox(E);
	zassert_ok(st25dv_emul_rf_put_msg(E, case_query, sizeof(case_query)));
	zassert_equal(gpo_falling, 2, "case message");
	zassert_equal(gpio_pin_get_raw(GPO_PORT, GPO_PIN), 1, "line back to idle (high)");
}

ZTEST(nfc_wlc, test_it_status_reports_a_case_message_and_clears)
{
	uint8_t it = 0;

	st25dv_emul_set_sys(E, ST25DV_EMUL_SYS_GPO1, 0xff);
	st25dv_emul_enable_mailbox(E);
	zassert_ok(st25dv_emul_rf_put_msg(E, (const uint8_t *)"\x13\x01\x02\x00\x16", 5));

	zassert_ok(nfc_read_it_status(&it));
	zassert_equal(it & ST25DV_EMUL_IT_RF_PUT_MSG, ST25DV_EMUL_IT_RF_PUT_MSG);
	zassert_ok(nfc_read_it_status(&it));
	zassert_equal(it, 0);
}

ZTEST(nfc_wlc, test_it_status_rejects_null_and_a_missing_tag)
{
	uint8_t it;

	zassert_equal(nfc_read_it_status(NULL), -1);
	global_st25dv_context_ptr = NULL;
	zassert_equal(nfc_read_it_status(&it), -1);
	zassert_equal(st25dv_emul_xfers(E), 0);
}

ZTEST(nfc_wlc, test_it_status_of_a_silent_tag_is_tried_10_times)
{
	uint8_t it;

	st25dv_emul_set_nak(E, true);

	zassert_equal(nfc_read_it_status(&it), -EIO);
	zassert_equal(st25dv_emul_xfers(E), 10 * 20, "10 reads of 20 attempts");
}
