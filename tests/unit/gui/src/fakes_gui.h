/* Panel transfer fakes for gui.c. */
#ifndef G1_UNIT_FAKES_GUI_H_
#define G1_UNIT_FAKES_GUI_H_

#include <stdint.h>
#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(int, _reflash_fb_data_to_lcd, uint32_t, uint32_t, int, int, int, int);
DECLARE_FAKE_VALUE_FUNC(int, _reflash_fb_data_to_lcd_ex, uint32_t, uint32_t, int, int, int, int,
			int);
DECLARE_FAKE_VOID_FUNC(jbd_clear_cache_sync, uint8_t);

#endif /* G1_UNIT_FAKES_GUI_H_ */
