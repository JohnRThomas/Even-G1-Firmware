/*
 * Declarations of settings_records.c.
 */
#ifndef G1_SERVICES_STORAGE_SETTINGS_RECORDS_H_
#define G1_SERVICES_STORAGE_SETTINGS_RECORDS_H_

#include "services/storage/storage.h"

extern char SERIAL_NUMBER[32];
extern char glasses_name[32];
int save_sys_settings(struct GlassesState *gs, int force);
int load_sys_setting(struct GlassesState *gs);
int save_usr_settings(struct GlassesState *gs, int unused);
int clear_user_settings(struct GlassesState *gs);
int load_usr_setting(struct GlassesState *gs);
void reset_all_usr_data(struct GlassesState *gs, int reboot);
int get_settings_from_flash(struct GlassesState *gs);
char *get_serial_number(void);
char *get_glasses_name(void); /* hardware version string */
int save_serial_number(const char *sn);
int dump_serial_number(const char *name);
int read_sys_settting_from_flash(struct sys_settings *out);
int read_usr_settting_from_flash(struct usr_settings *out);
int load_settings_from_flash(struct GlassesState *gs);

#endif /* G1_SERVICES_STORAGE_SETTINGS_RECORDS_H_ */
