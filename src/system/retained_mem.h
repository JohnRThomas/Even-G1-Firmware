/*
 * Retained memory block at 0x2007fc00 (retained_mem.c): dashboard time stamp and
 * weather, DFU erase request, production-test level and the quick-note index
 * survive a reboot here.
 */
#ifndef G1_SYSTEM_RETAINED_MEM_H_
#define G1_SYSTEM_RETAINED_MEM_H_

#include <stdint.h>
#include <stdbool.h>

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

extern struct ncs_mem ncs_mem;
#define NCS_MEM       (&ncs_mem)
#define NCS_MEM_MAGIC 0x12345678

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

#endif /* G1_SYSTEM_RETAINED_MEM_H_ */
