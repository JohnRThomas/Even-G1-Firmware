#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include "fakes_touch.h"
#include "harness.h"

DEFINE_FAKE_VALUE_FUNC(int, config_watchdog_wait_reset);
DEFINE_FAKE_VALUE_FUNC(uint8_t, nfc_is_tag_type_ok);
DEFINE_FAKE_VOID_FUNC(reset_all_usr_data, struct GlassesState *, int);
DEFINE_FAKE_VOID_FUNC(set_band_for_ESB, uint8_t);
__weak uint8_t even_ai_scrolling;

static void fakes_touch_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);
	RESET_FAKE(config_watchdog_wait_reset);
	RESET_FAKE(nfc_is_tag_type_ok);
	RESET_FAKE(reset_all_usr_data);
	RESET_FAKE(set_band_for_ESB);
	even_ai_scrolling = 0;
}

G1_UNIT_RESET_HOOK(fakes_touch_reset);
