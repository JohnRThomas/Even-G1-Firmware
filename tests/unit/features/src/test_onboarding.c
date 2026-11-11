/* 0x1F tutorial control: the reply follows what onboarding_ble_process() returns. */
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_display.h"
#include "features.h"

/* the fake returns 0, as onboarding does for a request longer than 3 bytes */
ZTEST(features, test_0x1f_no_reply_when_onboarding_returns_0)
{
	g1_send_hex("1f 12 04 00");

	zassert_equal(onboarding_ble_process_fake.call_count, 1);
	expect_no_frame();
}
