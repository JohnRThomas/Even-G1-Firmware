/*
 * Declarations of file_storage.c.
 */
#ifndef G1_SERVICES_STORAGE_FILE_STORAGE_H_
#define G1_SERVICES_STORAGE_FILE_STORAGE_H_

#include "services/storage/storage.h"

extern uint8_t sys_settings_dirty;
extern struct k_sem flash_ops_sem;
extern uint32_t LAST_FILE_CRC;
int enqueue_file(const void *data, size_t len);
void storage_commit_request(int cmd, uint8_t sys_dirty);
void start_flash_storage_threads(struct GlassesState *gs);

#endif /* G1_SERVICES_STORAGE_FILE_STORAGE_H_ */
