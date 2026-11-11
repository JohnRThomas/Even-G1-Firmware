/* Lengths come from the text: a short initializer would zero-fill silently. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "nrf_fuel_gauge.h"

static const struct battery_model model = {
#include "sensors_audio/battery_model.inc"
};

__asm__(".pushsection .rodata.g1_battery_model_text,\"a\"\n"
	".global g1_battery_model_text\n"
	"g1_battery_model_text:\n"
	".incbin \"" G1_BATTERY_MODEL_INC "\"\n"
	".byte 0\n"
	".popsection\n");
extern const char g1_battery_model_text[];

ZTEST_SUITE(battery_model, NULL, NULL, NULL, NULL, NULL);

/* -1 if absent */
static int count_values(const char *field)
{
	char key[32];
	const char *p;
	const char *end;
	int n = 0;
	bool in_value = false;

	snprintf(key, sizeof(key), ".%s = {", field);
	p = strstr(g1_battery_model_text, key);
	if (p == NULL) {
		return -1;
	}
	p += strlen(key);
	end = strchr(p, '}');
	if (end == NULL) {
		return -1;
	}
	for (; p < end; p++) {
		if (*p == ',') {
			in_value = false;
		} else if (*p != ' ' && *p != '\n' && *p != '\t' && !in_value) {
			in_value = true;
			n++;
		}
	}
	return n;
}

ZTEST(battery_model, test_every_table_has_the_length_the_library_expects)
{
	static const struct {
		const char *field;
		int len;
	} fields[] = {
		{"param_1", 201}, {"temps", 3},     {"param_2", 122}, {"param_3", 201},
		{"param_4", 201}, {"param_5", 201}, {"param_6", 201}, {"param_7", 122},
		{"param_8", 122}, {"param_9", 3},   {"param_10", 6},  {"param_11", 6},
		{"param_12", 3},
	};

	for (size_t i = 0; i < ARRAY_SIZE(fields); i++) {
		zassert_equal(count_values(fields[i].field), fields[i].len, "%s: %d values",
			      fields[i].field, count_values(fields[i].field));
	}
}

ZTEST(battery_model, test_model_is_named_evenbat)
{
	zassert_equal(count_values("name"), 7);
	zassert_equal(strcmp(model.name, "EVENBAT"), 0, "name %.8s", model.name);
}

ZTEST(battery_model, test_soc_grid_is_0_to_1_in_steps_of_0_005)
{
	for (int i = 0; i < 201; i++) {
		zassert_within(model.param_1[i], i * 0.005f, 1e-6f, "param_1[%d] = %f", i,
			       (double)model.param_1[i]);
	}
}

/* 4.40 V: the charger's termination voltage */
ZTEST(battery_model, test_voltage_grid_rises_evenly_from_2_99_v_to_4_40_v)
{
	const float step = (4.40f - 2.99f) / 121;

	zassert_within(model.param_2[0], 2.99f, 1e-6f);
	zassert_within(model.param_2[121], 4.40f, 1e-6f);
	for (int i = 1; i < 122; i++) {
		zassert_true(model.param_2[i] > model.param_2[i - 1], "param_2[%d]", i);
		zassert_within(model.param_2[i] - model.param_2[i - 1], step, 1e-5f,
			       "step %d", i);
	}
}

ZTEST(battery_model, test_every_value_is_finite)
{
	const float *f = (const float *)&model;
	size_t n = offsetof(struct battery_model, name) / sizeof(float);

	for (size_t i = 0; i < n; i++) {
		zassert_true(isfinite(f[i]), "value %zu", i);
	}
}
