#include <string.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/ztest.h>

#include "fakes_pt.h"

#include "aw9320x/aw9320x.h"
#include "imu/imu_fusion.h"
#include "main.h"
#include "nfc/nfc_wlc.h"
#include "nfc/st25dv.h"
#include "notify/demo_image.h"
#include "sensors_audio/sensors_audio.h"
#include "storage/storage.h"
#include "threads/threads.h"

DEFINE_FAKE_VALUE_FUNC(int, eeprom_st25dv_read_i2c_mb_mailbox_ram, const struct device *,
		       uint8_t **, uint32_t *);
DEFINE_FAKE_VALUE_FUNC(int, eeprom_st25dv_write_i2c_mb_mailbox_ram, const struct device *,
		       const void *, size_t);
DEFINE_FAKE_VALUE_FUNC(uint8_t, eeprom_st25dv_lock, const struct device *);
DEFINE_FAKE_VALUE_FUNC(uint8_t, eeprom_st25dv_unlock, const struct device *);

uint8_t g1_pt_mailbox_rx[256];
uint32_t g1_pt_mailbox_rx_len;
struct g1_pkt_log g1_pt_mailbox_tx;

DEFINE_FAKE_VALUE_FUNC(int, enable_charger);
DEFINE_FAKE_VOID_FUNC(set_box_status, uint8_t, uint8_t);
DEFINE_FAKE_VALUE_FUNC(uint8_t, is_box_lid_closed);
DEFINE_FAKE_VALUE_FUNC(int, enable_power_for_panel);
DEFINE_FAKE_VALUE_FUNC(int, disable_power_for_panel);
DEFINE_FAKE_VALUE_FUNC(int, opt3007_read, int);
DEFINE_FAKE_VALUE_FUNC(int, erase_audio_buffer);

DEFINE_FAKE_VALUE_FUNC(uint32_t, nfc_get_field_mv);

DEFINE_FAKE_VALUE_FUNC(int64_t, nfc_deadline_set, uint32_t);
DEFINE_FAKE_VOID_FUNC(update_activity_time);
DEFINE_FAKE_VOID_FUNC(enter_silent_mode);
DEFINE_FAKE_VOID_FUNC(exit_silent_mode);
DEFINE_FAKE_VALUE_FUNC(const struct device *, get_charger_device);
uint8_t can_enter_low_power;

DEFINE_FAKE_VOID_FUNC(set_ncs_mem_pt_level, uint8_t);

DEFINE_FAKE_VALUE_FUNC(int, calc_flash_crc, uint16_t *, int, uint32_t);
DEFINE_FAKE_VALUE_FUNC(int, save_sys_settings, struct GlassesState *, int);
DEFINE_FAKE_VALUE_FUNC(int, save_usr_settings, struct GlassesState *, int);
DEFINE_FAKE_VALUE_FUNC(int, read_sys_settting_from_flash, struct sys_settings *);
DEFINE_FAKE_VALUE_FUNC(int, read_usr_settting_from_flash, struct usr_settings *);
DEFINE_FAKE_VALUE_FUNC(char *, get_serial_number);
DEFINE_FAKE_VALUE_FUNC(char *, get_glasses_name);
DEFINE_FAKE_VALUE_FUNC(int, save_serial_number, const char *);
DEFINE_FAKE_VALUE_FUNC(int, dump_serial_number, const char *);

DEFINE_FAKE_VOID_FUNC(pt_comm_busy_set);
DEFINE_FAKE_VOID_FUNC(pt_comm_busy_clear);
DEFINE_FAKE_VOID_FUNC(aw9320x_diff_get, int32_t *);

DEFINE_FAKE_VOID_FUNC(demo_image_select, uint8_t);
uint8_t demo_mode;

uint32_t imu_roll_x100;
uint32_t imu_yaw_x100;

DEFINE_FAKE_VALUE_FUNC(int, g1_pt_uart_tx, const struct device *, const uint8_t *, size_t,
		       int32_t);
DEFINE_FAKE_VOID_FUNC(g1_pt_uart_poll_out, const struct device *, unsigned char);
DEFINE_FAKE_VALUE_FUNC(int, g1_pt_uart_callback_set, const struct device *, uart_callback_t,
		       void *);
DEFINE_FAKE_VALUE_FUNC(int, g1_pt_uart_rx_enable, const struct device *, uint8_t *, size_t,
		       int32_t);
DEFINE_FAKE_VALUE_FUNC(int, g1_pt_uart_rx_buf_rsp, const struct device *, uint8_t *, size_t);
struct g1_pkt_log g1_pt_uart_tx_log;

jmp_buf g1_pt_reboot_jmp;
bool g1_pt_reboot_armed;
int g1_pt_reboot_count;
int g1_pt_reboot_type;
int64_t g1_pt_reboot_uptime_ms;

FUNC_NORETURN void sys_reboot(int type)
{
	g1_pt_reboot_count++;
	g1_pt_reboot_type = type;
	g1_pt_reboot_uptime_ms = k_uptime_get();
	if (g1_pt_reboot_armed) {
		longjmp(g1_pt_reboot_jmp, 1);
	}
	zassert_unreachable("unexpected sys_reboot(%d)", type);
	CODE_UNREACHABLE;
}

static int mailbox_read(const struct device *dev, uint8_t **buf, uint32_t *len)
{
	ARG_UNUSED(dev);
	memcpy(*buf, g1_pt_mailbox_rx, g1_pt_mailbox_rx_len);
	*len = g1_pt_mailbox_rx_len;
	return 0;
}

static int mailbox_write(const struct device *dev, const void *buf, size_t len)
{
	ARG_UNUSED(dev);
	g1_pkt_log_add(&g1_pt_mailbox_tx, buf, len);
	return 0;
}

static int uart_tx_log(const struct device *dev, const uint8_t *buf, size_t len, int32_t timeout)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(timeout);
	g1_pkt_log_add(&g1_pt_uart_tx_log, buf, len);
	return 0;
}

static void uart_poll_out_log(const struct device *dev, unsigned char c)
{
	ARG_UNUSED(dev);
	g1_pkt_log_add(&g1_pt_uart_tx_log, &c, 1);
}

static void fakes_pt_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);

	RESET_FAKE(eeprom_st25dv_read_i2c_mb_mailbox_ram);
	RESET_FAKE(eeprom_st25dv_write_i2c_mb_mailbox_ram);
	RESET_FAKE(eeprom_st25dv_lock);
	RESET_FAKE(eeprom_st25dv_unlock);
	eeprom_st25dv_read_i2c_mb_mailbox_ram_fake.custom_fake = mailbox_read;
	eeprom_st25dv_write_i2c_mb_mailbox_ram_fake.custom_fake = mailbox_write;
	memset(g1_pt_mailbox_rx, 0, sizeof(g1_pt_mailbox_rx));
	g1_pt_mailbox_rx_len = 0;
	g1_pkt_log_clear(&g1_pt_mailbox_tx);

	RESET_FAKE(enable_charger);
	RESET_FAKE(set_box_status);
	RESET_FAKE(is_box_lid_closed);
	RESET_FAKE(enable_power_for_panel);
	RESET_FAKE(disable_power_for_panel);
	RESET_FAKE(opt3007_read);
	RESET_FAKE(erase_audio_buffer);
	RESET_FAKE(nfc_get_field_mv);
	RESET_FAKE(nfc_deadline_set);
	RESET_FAKE(update_activity_time);
	RESET_FAKE(enter_silent_mode);
	RESET_FAKE(exit_silent_mode);
	RESET_FAKE(get_charger_device);
	can_enter_low_power = 0;
	RESET_FAKE(set_ncs_mem_pt_level);
	RESET_FAKE(calc_flash_crc);
	RESET_FAKE(save_sys_settings);
	RESET_FAKE(save_usr_settings);
	RESET_FAKE(read_sys_settting_from_flash);
	RESET_FAKE(read_usr_settting_from_flash);
	RESET_FAKE(get_serial_number);
	RESET_FAKE(get_glasses_name);
	RESET_FAKE(save_serial_number);
	RESET_FAKE(dump_serial_number);
	RESET_FAKE(pt_comm_busy_set);
	RESET_FAKE(pt_comm_busy_clear);
	RESET_FAKE(aw9320x_diff_get);
	RESET_FAKE(demo_image_select);
	demo_mode = 0;
	imu_roll_x100 = 0;
	imu_yaw_x100 = 0;

	RESET_FAKE(g1_pt_uart_tx);
	RESET_FAKE(g1_pt_uart_poll_out);
	RESET_FAKE(g1_pt_uart_callback_set);
	RESET_FAKE(g1_pt_uart_rx_enable);
	RESET_FAKE(g1_pt_uart_rx_buf_rsp);
	g1_pt_uart_tx_fake.custom_fake = uart_tx_log;
	g1_pt_uart_poll_out_fake.custom_fake = uart_poll_out_log;
	g1_pkt_log_clear(&g1_pt_uart_tx_log);

	g1_pt_reboot_armed = false;
	g1_pt_reboot_count = 0;
	g1_pt_reboot_type = -1;
	g1_pt_reboot_uptime_ms = 0;
}
G1_UNIT_RESET_HOOK(fakes_pt_reset);
