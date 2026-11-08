/*
 * Charging-case state (charger_box.c).
 */
#ifndef G1_SERVICES_POWER_CHARGER_BOX_H_
#define G1_SERVICES_POWER_CHARGER_BOX_H_

#include <stdint.h>

/* set_box_status() type */
enum {
	BOX_IN_BOX = 0,
	BOX_LID_CLOSED = 1
};

uint8_t get_box_status_by_type(uint8_t type);
void set_box_status(uint8_t type, uint8_t status);
int is_in_box(void);
uint8_t is_box_lid_closed(void);
uint8_t is_box_charging(void);
void init_box_status(void);

#endif /* G1_SERVICES_POWER_CHARGER_BOX_H_ */
