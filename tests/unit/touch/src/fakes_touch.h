#ifndef G1_UNIT_FAKES_TOUCH_H_
#define G1_UNIT_FAKES_TOUCH_H_

#include <stdint.h>
#include <zephyr/fff.h>
#include <glasses_state.h>

DECLARE_FAKE_VALUE_FUNC(int, config_watchdog_wait_reset);
DECLARE_FAKE_VALUE_FUNC(uint8_t, nfc_is_tag_type_ok);
DECLARE_FAKE_VOID_FUNC(reset_all_usr_data, struct GlassesState *, int);
DECLARE_FAKE_VOID_FUNC(set_band_for_ESB, uint8_t);
extern uint8_t even_ai_scrolling;

#endif /* G1_UNIT_FAKES_TOUCH_H_ */
