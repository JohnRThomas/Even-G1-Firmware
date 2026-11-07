/*
 * Declarations of src/main.c.
 */
#ifndef G1_MAIN_H_
#define G1_MAIN_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/regulator.h>
#include <glasses_state.h>

/* Copy of the private config struct of drivers/regulator/regulator_npm1300.c */
struct regulator_npm1300_config {
	struct regulator_common_config common;
	const struct device *mfd;
	uint8_t source;
	int32_t retention_uv;
	struct gpio_dt_spec enable_gpios;
	struct gpio_dt_spec retention_gpios;
	struct gpio_dt_spec pwm_gpios;
};

extern struct GlassesState *GLOBAL_STATE;

/*
 * Memory block at 0x2007fc00, retained across reboots / shared with the
 * bootloader and the network core.
 */
struct ncs_mem {
	uint32_t ts;
	uint8_t weather_icon;
	uint8_t temperature;
	uint8_t pad_06[2];
	uint64_t ms_ts;
	uint8_t retained_buf_a[0x25];
	uint8_t retained_buf_b[0x2a];
	uint8_t temp_unit;
	uint8_t time_disp_mode;
	uint8_t retained_a;
	uint8_t retained_b;
	bool erase_dfu;
	uint32_t magic;
	uint8_t retained_c;
	uint8_t notes_index;
	uint8_t pad_6a[6];
	uint8_t pt_level;
};

/* placed at 0x2007fc00 by the linker */
extern struct ncs_mem ncs_mem;
#define NCS_MEM (&ncs_mem)
#define NCS_MEM_MAGIC 0x12345678

/* main.c */
extern uint8_t slave_msg_key_pending;  /* new message flag */
extern char net_build_time[32];
extern int esb_signal;
extern int esb_rx_count;
extern int esb_tx_fail_count;
extern int esb_tx_count;
int power_for_panel(void);
int power_down_panel(void);
int runtime_info_sync(struct GlassesState *gs);
int getDeviceRole(void);
uint8_t is_master(void);
void set_ncs_mem_ts(uint32_t ts);
bool getEraseDFU(void);
void setEraseDFU(bool set);
void set_ncs_mem_pt_level(uint8_t val);
void set_ncs_mem_initialized(void);
int is_ncs_mem_initialized(void);
void save_dashboard_ts_to_ncs_mem(void);
void restore_dashboard_ts_from_ncs_mem(void);
void set_ncs_mem_notes_index(uint8_t idx);
void deinit_ncs_memory(void);
int erase_dfu_flash(void);
int set_shipmode(int enable);
void set_band_for_ESB(uint8_t band);
int get_work_mode(void);
int change_work_mode(int mode);
/* power hooks of the flash module */
void ext_flash_power_up(void);
void ext_flash_power_down(void);

#endif /* G1_MAIN_H_ */
