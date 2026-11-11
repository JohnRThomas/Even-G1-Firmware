/*
 * Fake pmic_charger sensor driver; channel_get reports the last successful fetch, as the real
 * driver does.
 */
#ifndef G1_UNIT_FAKE_CHARGER_H_
#define G1_UNIT_FAKE_CHARGER_H_

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm1300_charger.h>

#define G1_FAKE_CHARGER_DEV DEVICE_DT_GET(DT_NODELABEL(pmic_charger))

#define FAKE_CHARGER_QUEUE_MAX 32
#define FAKE_CHARGER_GET_LOG   64

struct fake_charger_sample {
	struct sensor_value voltage; /* V */
	struct sensor_value current; /* A, positive = discharge */
	struct sensor_value temp;    /* degrees C */
	int32_t status;              /* BCHGCHARGESTATUS */
	int32_t error;               /* BCHGERRREASON */
};

struct fake_charger_sample fake_charger_sample(int64_t v_uv, int64_t i_ua, int64_t t_mdegc);
void fake_charger_set_sample(const struct fake_charger_sample *s);
int fake_charger_queue_sample(const struct fake_charger_sample *s);
int fake_charger_queued(void);

void fake_charger_fail_fetch(int ret, int n);

int fake_charger_fetch_count(void);
int fake_charger_get_count(void);
enum sensor_channel fake_charger_get_chan(int i);

#endif /* G1_UNIT_FAKE_CHARGER_H_ */
