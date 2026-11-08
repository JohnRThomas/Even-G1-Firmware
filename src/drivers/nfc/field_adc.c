/*
 * NFC field voltage: the rectified voltage of the charging case's field, read on
 * an ADC channel (the zephyr,user io-channel).  Follows Zephyr's
 * samples/drivers/adc.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include "drivers/nfc/field_adc.h"
#include "system/logging.h"

/* ADC io-channels of the zephyr,user node (overlay: <&adc 3>, channel@3) */
#define DT_SPEC_AND_COMMA(node_id, prop, idx) ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, DT_SPEC_AND_COMMA)};

/* Zephyr ADC sample style channel loop (one channel) */
int nfc_field_adc_init(void)
{
	int err;

	for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
		err = 0;
		if (adc_is_ready_dt(&adc_channels[i])) {
			err = adc_channel_setup_dt(&adc_channels[i]);
		} else {
			G1_LOG_ERR("ADC controller device %s not ready\n\n",
				   adc_channels[i].dev->name);
		}
		if (err < 0) {
			G1_LOG_ERR("Could not setup channel #%d (%d)\n\n", i, err);
		}
	}

	return 0;
}

uint32_t nfc_field_adc_read_mv(void)
{
	int err;
	uint16_t buf;
	int32_t val_mv;
	struct adc_sequence sequence = {
		.buffer = &buf,
		/* buffer size in bytes, not number of samples */
		.buffer_size = sizeof(buf),
	};

	(void)adc_sequence_init_dt(&adc_channels[0], &sequence);

	err = adc_read(adc_channels[0].dev, &sequence);
	if (err < 0) {
		G1_LOG_ERR("Could not read (%d)\n\n", err);
		return 0;
	}

	if (adc_channels[0].channel_cfg.differential) {
		val_mv = (int32_t)((int16_t)buf);
	} else {
		val_mv = (int32_t)buf;
	}

	err = adc_raw_to_millivolts_dt(&adc_channels[0], &val_mv);
	/* conversion to mV may not be supported, skip if not */
	if (err < 0) {
		G1_LOG_ERR(" (value in mV not available)\n\n");
		return 0;
	}

	if (val_mv > 0 && val_mv < 1200) {
		return val_mv;
	}

	return 0;
}
