/*
 * Module-private declarations for the production-test files
 * (production_test.c, serial_thread.c, serial_port.c).
 */
#ifndef G1_PRODUCTION_TEST_H_
#define G1_PRODUCTION_TEST_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include "system/glasses_state.h"

/* communication channel of the production-test protocol */
enum pt_comm_type {
	PT_COMM_NFC = 0,   /* st25dv mailbox */
	PT_COMM_UART = 1,  /* lpuart */
	PT_COMM_NONE = 2,
};

/* response frame: [cmd][1][3][payload len][payload...] */
typedef int (*pt_cmd_handler_t)(const uint8_t *req, uint32_t req_len, uint8_t **resp,
				uint8_t *resp_len, void *arg);

struct pt_cmd {
	uint8_t cmd;
	pt_cmd_handler_t handler;
};

/* production_test.c */
extern const struct pt_cmd pt_cmd_table[48];
int process_pt_protocol(int type, const uint8_t *req, uint32_t req_len, uint8_t **resp,
			uint8_t *resp_len, void *arg);
int64_t set_pt_deadline(void);                   /* pt_timeout_ms = uptime + 10 s */
uint8_t get_pt_level_flag(void);
uint8_t get_pt_level(void);
uint8_t set_pt_level(uint8_t level);
int is_ext_flash_burned(void);
uint8_t get_pt_test_mode(void);
void set_test_mode(uint8_t mode);
uint8_t get_pt_version_major(void);

#endif /* G1_PRODUCTION_TEST_H_ */
