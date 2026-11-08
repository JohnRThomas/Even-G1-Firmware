/*
 * Declarations of ui_onboarding.c.
 */
#ifndef G1_UI_SCREENS_UI_ONBOARDING_H_
#define G1_UI_SCREENS_UI_ONBOARDING_H_

#include "ui/screens/ui.h"

extern uint8_t onboarding_shown_sub_step; /* onboarding sub-step shown (slave_display_thread) */
int init_onboarding_info(void);
void onboarding_request_redraw(void);
void onboarding_draw_static_page(int event);
void onboarding_draw_animated_page(int event);
void onboarding_check_step_finished(void);
int ui_onboarding_task(struct jbd_panel_context *panel, void *arg, int event);
struct ble_req_hdr;
int onboarding_ble_process(struct GlassesState *gs, const struct ble_req_hdr *hdr,
			   const uint8_t *req, uint8_t *rsp);
int onboarding_sync_data(const uint8_t *data, uint32_t len);
void onboarding_deactivate(void);

#endif /* G1_UI_SCREENS_UI_ONBOARDING_H_ */
