/*
 * Declarations of ui_notification.c.
 */
#ifndef G1_UI_SCREENS_UI_NOTIFICATION_H_
#define G1_UI_SCREENS_UI_NOTIFICATION_H_

#include "ui/screens/ui.h"
#include "services/notifications/notify.h"

int draw_template_notification_page(struct jbd_panel_context *panel, void *arg);
void draw_message(uint8_t unread, struct notif_msg *msg);
void set_hide_icon_on_exit(void);
void set_particle_pending(void);
int ui_ancs_notificaton_task(struct jbd_panel_context *panel, void *arg, int event);

#endif /* G1_UI_SCREENS_UI_NOTIFICATION_H_ */
