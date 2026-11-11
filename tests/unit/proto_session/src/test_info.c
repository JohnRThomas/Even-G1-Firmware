/* Identity and link information reads. */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "session.h"
#include "fakes_storage.h"

#include "storage/storage.h"

/* factory settings record, loaded as at boot */
static void load_settings_with(const char *sn, const char *name)
{
	struct sys_settings rec;

	memset(&rec, 0, sizeof(rec));
	rec.valid = 1;
	strncpy(rec.sn, sn, sizeof(rec.sn) - 1);
	strncpy(rec.name, name, sizeof(rec.name) - 1);
	memcpy(&g1_fake_flash_image[SYS_SETTINGS_ADDR], &rec, sizeof(rec));
	zassert_equal(load_sys_setting(g1_state()), 0);
}

ZTEST(proto_session, test_0x34_reports_the_serial_number_from_the_settings)
{
	load_settings_with("S110LAAL290026", "G1R1EKT099");

	g1_send_vector("0x34_device_sn_get");

	expect_frame("34 34 53 31 31 30 4c 41 41 4c 32 39 30 30 32 36 00 00 00 00");
	expect_no_frame();
}

/* the lens serial number is the record's name field */
ZTEST(proto_session, test_0x33_reports_the_lens_serial_number_from_the_settings)
{
	load_settings_with("S110LAAL290026", "G1R1EKT099");

	g1_send_vector("0x33_glasses_sn_get");

	expect_frame_padded("33 33 47 31 52 31 45 4b 54 30 39 39", 20);
	expect_no_frame();
}
