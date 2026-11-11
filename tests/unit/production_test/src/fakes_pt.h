#ifndef G1_UNIT_FAKES_PT_H_
#define G1_UNIT_FAKES_PT_H_

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/fff.h>

#include "harness.h"

/* nfc/eeprom_st25dv.c mailbox */
DECLARE_FAKE_VALUE_FUNC(int, eeprom_st25dv_read_i2c_mb_mailbox_ram, const struct device *,
			uint8_t **, uint32_t *);
DECLARE_FAKE_VALUE_FUNC(int, eeprom_st25dv_write_i2c_mb_mailbox_ram, const struct device *,
			const void *, size_t);
DECLARE_FAKE_VALUE_FUNC(uint8_t, eeprom_st25dv_lock, const struct device *);
DECLARE_FAKE_VALUE_FUNC(uint8_t, eeprom_st25dv_unlock, const struct device *);

extern uint8_t g1_pt_mailbox_rx[256];
extern uint32_t g1_pt_mailbox_rx_len;
extern struct g1_pkt_log g1_pt_mailbox_tx;

DECLARE_FAKE_VALUE_FUNC(int, enable_charger);
DECLARE_FAKE_VOID_FUNC(set_box_status, uint8_t, uint8_t);
DECLARE_FAKE_VALUE_FUNC(uint8_t, is_box_lid_closed);
DECLARE_FAKE_VALUE_FUNC(int, enable_power_for_panel);
DECLARE_FAKE_VALUE_FUNC(int, disable_power_for_panel);
DECLARE_FAKE_VALUE_FUNC(int, opt3007_read, int);
DECLARE_FAKE_VALUE_FUNC(int, erase_audio_buffer);

DECLARE_FAKE_VALUE_FUNC(uint32_t, nfc_get_field_mv);

DECLARE_FAKE_VALUE_FUNC(int64_t, nfc_deadline_set, uint32_t);
DECLARE_FAKE_VOID_FUNC(update_activity_time);
DECLARE_FAKE_VOID_FUNC(enter_silent_mode);
DECLARE_FAKE_VOID_FUNC(exit_silent_mode);
DECLARE_FAKE_VALUE_FUNC(const struct device *, get_charger_device);
extern uint8_t can_enter_low_power;

DECLARE_FAKE_VOID_FUNC(set_ncs_mem_pt_level, uint8_t);

DECLARE_FAKE_VALUE_FUNC(int, calc_flash_crc, uint16_t *, int, uint32_t);
DECLARE_FAKE_VALUE_FUNC(int, save_sys_settings, struct GlassesState *, int);
DECLARE_FAKE_VALUE_FUNC(int, save_usr_settings, struct GlassesState *, int);
DECLARE_FAKE_VALUE_FUNC(int, read_sys_settting_from_flash, struct sys_settings *);
DECLARE_FAKE_VALUE_FUNC(int, read_usr_settting_from_flash, struct usr_settings *);
DECLARE_FAKE_VALUE_FUNC(char *, get_serial_number);
DECLARE_FAKE_VALUE_FUNC(char *, get_glasses_name);
DECLARE_FAKE_VALUE_FUNC(int, save_serial_number, const char *);
DECLARE_FAKE_VALUE_FUNC(int, dump_serial_number, const char *);

DECLARE_FAKE_VOID_FUNC(pt_comm_busy_set);
DECLARE_FAKE_VOID_FUNC(pt_comm_busy_clear);
DECLARE_FAKE_VOID_FUNC(aw9320x_diff_get, int32_t *);

DECLARE_FAKE_VOID_FUNC(demo_image_select, uint8_t);
extern uint8_t demo_mode;

extern uint32_t imu_roll_x100;
extern uint32_t imu_yaw_x100;

/* UART calls of serial_port.c, renamed in test_serial.c */
DECLARE_FAKE_VALUE_FUNC(int, g1_pt_uart_tx, const struct device *, const uint8_t *, size_t,
			int32_t);
DECLARE_FAKE_VOID_FUNC(g1_pt_uart_poll_out, const struct device *, unsigned char);
DECLARE_FAKE_VALUE_FUNC(int, g1_pt_uart_callback_set, const struct device *, uart_callback_t,
			void *);
DECLARE_FAKE_VALUE_FUNC(int, g1_pt_uart_rx_enable, const struct device *, uint8_t *, size_t,
			int32_t);
DECLARE_FAKE_VALUE_FUNC(int, g1_pt_uart_rx_buf_rsp, const struct device *, uint8_t *, size_t);
extern struct g1_pkt_log g1_pt_uart_tx_log;

extern jmp_buf g1_pt_reboot_jmp;
extern bool g1_pt_reboot_armed;
extern int g1_pt_reboot_count;
extern int g1_pt_reboot_type;
extern int64_t g1_pt_reboot_uptime_ms;

/* stmt must end in sys_reboot(); the test resumes after the macro. */
#define G1_PT_EXPECT_REBOOT(stmt)                                                                  \
	do {                                                                                       \
		g1_pt_reboot_armed = true;                                                         \
		if (setjmp(g1_pt_reboot_jmp) == 0) {                                               \
			stmt;                                                                      \
			g1_pt_reboot_armed = false;                                                \
			zassert_unreachable("returned without sys_reboot()");                      \
		}                                                                                  \
		g1_pt_reboot_armed = false;                                                        \
	} while (0)

#endif /* G1_UNIT_FAKES_PT_H_ */
