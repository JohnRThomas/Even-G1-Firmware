#include "harness.h"
#include "fakes_gui.h"

#include "panel_display/panel.h"

DEFINE_FAKE_VALUE_FUNC(int, _reflash_fb_data_to_lcd, uint32_t, uint32_t, int, int, int, int);
DEFINE_FAKE_VALUE_FUNC(int, _reflash_fb_data_to_lcd_ex, uint32_t, uint32_t, int, int, int, int,
		       int);
DEFINE_FAKE_VOID_FUNC(jbd_clear_cache_sync, uint8_t);

static void fakes_gui_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);
	RESET_FAKE(_reflash_fb_data_to_lcd);
	RESET_FAKE(_reflash_fb_data_to_lcd_ex);
	RESET_FAKE(jbd_clear_cache_sync);
}
G1_UNIT_RESET_HOOK(fakes_gui_reset);
