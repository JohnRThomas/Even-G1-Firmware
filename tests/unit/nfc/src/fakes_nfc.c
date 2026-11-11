#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_nfc.h"

DEFINE_FAKE_VALUE_FUNC(int64_t, nfc_deadline_set, uint32_t);
DEFINE_FAKE_VOID_FUNC(update_activity_time);
DEFINE_FAKE_VOID_FUNC(enter_silent_mode);
DEFINE_FAKE_VOID_FUNC(exit_silent_mode);
DEFINE_FAKE_VALUE_FUNC(const struct device *, get_charger_device);
__weak uint8_t can_enter_low_power;

DEFINE_FAKE_VOID_FUNC(set_ncs_mem_pt_level, uint8_t);
__weak int esb_signal;

DEFINE_FAKE_VOID_FUNC(pt_comm_busy_set);
DEFINE_FAKE_VOID_FUNC(pt_comm_busy_clear);
DEFINE_FAKE_VOID_FUNC(aw9320x_diff_get, int32_t *);

DEFINE_FAKE_VALUE_FUNC(int, calc_flash_crc, uint16_t *, int, uint32_t);
DEFINE_FAKE_VALUE_FUNC(int, save_sys_settings, struct GlassesState *, int);
DEFINE_FAKE_VALUE_FUNC(int, save_usr_settings, struct GlassesState *, int);
DEFINE_FAKE_VALUE_FUNC(int, read_sys_settting_from_flash, struct sys_settings *);
DEFINE_FAKE_VALUE_FUNC(int, read_usr_settting_from_flash, struct usr_settings *);
DEFINE_FAKE_VALUE_FUNC(char *, get_serial_number);
DEFINE_FAKE_VALUE_FUNC(char *, get_glasses_name);
DEFINE_FAKE_VALUE_FUNC(int, save_serial_number, const char *);
DEFINE_FAKE_VALUE_FUNC(int, dump_serial_number, const char *);

DEFINE_FAKE_VALUE_FUNC(int, enable_power_for_panel);
DEFINE_FAKE_VALUE_FUNC(int, disable_power_for_panel);
DEFINE_FAKE_VALUE_FUNC(int, opt3007_read, int);
DEFINE_FAKE_VALUE_FUNC(int, erase_audio_buffer);
DEFINE_FAKE_VALUE_FUNC(int, request_dmic_stream_start);
DECLARE_FAKE_VOID_FUNC(dmic_stream_stop);
DEFINE_FAKE_VOID_FUNC(dmic_stream_stop);

DEFINE_FAKE_VALUE_FUNC(int, lpuart_async_b, const struct device *, uint8_t *, size_t);
DEFINE_FAKE_VALUE_FUNC(int, read_serial_data, const struct device *, uint8_t **, uint32_t *);
DEFINE_FAKE_VOID_FUNC(serial_rx_clear);

DEFINE_FAKE_VOID_FUNC(demo_image_select, uint8_t);
__weak uint8_t demo_mode;
__weak uint32_t imu_roll_x100;
__weak uint32_t imu_yaw_x100;

DEFINE_FAKE_VALUE_FUNC(int, enqueue_bt_data, void *, uint8_t);
DEFINE_FAKE_VALUE_FUNC(int, get_work_mode);
DEFINE_FAKE_VALUE_FUNC(int, is_task_running, struct GlassesState *);
DEFINE_FAKE_VALUE_FUNC(int, update_persist_task_status_to_idle, struct GlassesState *);
DEFINE_FAKE_VOID_FUNC(aw93203_init);
DEFINE_FAKE_VALUE_FUNC(int32_t, aw9320x_read_chipid);
DEFINE_FAKE_VALUE_FUNC(int, display_close_screen, int);
DEFINE_FAKE_VALUE_FUNC(int, display_reflash_screen, uint8_t);
DEFINE_FAKE_VOID_FUNC(enter_wear_burial_point);
DEFINE_FAKE_VOID_FUNC(exit_wear_burial_point);
DEFINE_FAKE_VOID_FUNC(free_demo_line_buf);
DEFINE_FAKE_VALUE_FUNC(int, global_system_resume, struct GlassesState *);
DEFINE_FAKE_VALUE_FUNC(int, global_system_suspend, struct GlassesState *);
DEFINE_FAKE_VALUE_FUNC(int, init_serial_comm);
DEFINE_FAKE_VALUE_FUNC(uint8_t, opt3007_get_brightness_level, struct opt3007_context *);
DEFINE_FAKE_VOID_FUNC(reset_dash_buf, int);
DEFINE_FAKE_VALUE_FUNC(uint32_t, set_date, uint32_t);
__weak uint8_t task_status_lock;

struct g1_pkt_log g1_nfc_bt_log;

static int enqueue_bt_data_log(void *data, uint8_t len)
{
	g1_pkt_log_add(&g1_nfc_bt_log, data, len);
	return 0;
}

static char serial[] = "S110LAAL290026";

static char *serial_default(void)
{
	return serial;
}

__weak FUNC_NORETURN void sys_reboot(int type)
{
	ARG_UNUSED(type);
	ztest_test_fail();
	CODE_UNREACHABLE;
}

static void fakes_nfc_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);
	RESET_FAKE(nfc_deadline_set);
	RESET_FAKE(update_activity_time);
	RESET_FAKE(enter_silent_mode);
	RESET_FAKE(exit_silent_mode);
	RESET_FAKE(get_charger_device);
	RESET_FAKE(set_ncs_mem_pt_level);
	RESET_FAKE(pt_comm_busy_set);
	RESET_FAKE(pt_comm_busy_clear);
	RESET_FAKE(aw9320x_diff_get);
	RESET_FAKE(calc_flash_crc);
	RESET_FAKE(save_sys_settings);
	RESET_FAKE(save_usr_settings);
	RESET_FAKE(read_sys_settting_from_flash);
	RESET_FAKE(read_usr_settting_from_flash);
	RESET_FAKE(get_serial_number);
	RESET_FAKE(get_glasses_name);
	RESET_FAKE(save_serial_number);
	RESET_FAKE(dump_serial_number);
	RESET_FAKE(enable_power_for_panel);
	RESET_FAKE(disable_power_for_panel);
	RESET_FAKE(opt3007_read);
	RESET_FAKE(erase_audio_buffer);
	RESET_FAKE(request_dmic_stream_start);
	RESET_FAKE(dmic_stream_stop);
	RESET_FAKE(lpuart_async_b);
	RESET_FAKE(read_serial_data);
	RESET_FAKE(serial_rx_clear);
	RESET_FAKE(demo_image_select);
	RESET_FAKE(enqueue_bt_data);
	RESET_FAKE(get_work_mode);
	RESET_FAKE(is_task_running);
	RESET_FAKE(update_persist_task_status_to_idle);
	RESET_FAKE(aw93203_init);
	RESET_FAKE(aw9320x_read_chipid);
	RESET_FAKE(display_close_screen);
	RESET_FAKE(display_reflash_screen);
	RESET_FAKE(enter_wear_burial_point);
	RESET_FAKE(exit_wear_burial_point);
	RESET_FAKE(free_demo_line_buf);
	RESET_FAKE(global_system_resume);
	RESET_FAKE(global_system_suspend);
	RESET_FAKE(init_serial_comm);
	RESET_FAKE(opt3007_get_brightness_level);
	RESET_FAKE(reset_dash_buf);
	RESET_FAKE(set_date);
	task_status_lock = 0;
	g1_pkt_log_clear(&g1_nfc_bt_log);
	enqueue_bt_data_fake.custom_fake = enqueue_bt_data_log;
	get_serial_number_fake.custom_fake = serial_default;
	get_glasses_name_fake.custom_fake = serial_default;
	lpuart_async_b_fake.return_val = -EIO;
	read_serial_data_fake.return_val = -EIO;
	can_enter_low_power = 0;
	esb_signal = 0;
	demo_mode = 0;
	imu_roll_x100 = 0;
	imu_yaw_x100 = 0;
}

G1_UNIT_RESET_HOOK(fakes_nfc_reset);
