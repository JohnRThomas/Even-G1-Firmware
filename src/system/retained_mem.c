/*
 * Retained memory block at 0x2007fc00, shared with the bootloader and the
 * network core and kept across reboots.
 */
#include <string.h>
#include "system/retained_mem.h"
#include "main.h"
#include "ui/screens/dashboard.h"

/* placed at 0x2007fc00 by the linker (linker/ncs_mem.ld) */
struct ncs_mem ncs_mem __attribute__((section(".ncs_mem")));
void set_ncs_mem_ts(uint32_t ts)
{
	NCS_MEM->ts = ts;
}

bool getEraseDFU(void)
{
	return NCS_MEM->erase_dfu;
}

void setEraseDFU(bool set)
{
	NCS_MEM->erase_dfu = set;
}

void set_ncs_mem_pt_level(uint8_t val)
{
	NCS_MEM->pt_level = val;
}

void set_ncs_mem_initialized(void)
{
	NCS_MEM->magic = NCS_MEM_MAGIC;
}

int is_ncs_mem_initialized(void)
{
	return NCS_MEM->magic == NCS_MEM_MAGIC ? 0 : -1;
}

void save_dashboard_ts_to_ncs_mem(void)
{
	struct dashboard_ts_context *ts = GLOBAL_STATE->dashboard_ts;

	NCS_MEM->ts = ts->ts;
	NCS_MEM->weather_icon = ts->weather_icon;
	NCS_MEM->temperature = ts->temperature;
	NCS_MEM->ms_ts = ts->ms_ts;
	NCS_MEM->temp_unit = ts->temp_unit;
	NCS_MEM->time_disp_mode = ts->time_disp_mode;
	NCS_MEM->retained_b = ts->retained_b;
	NCS_MEM->retained_a = ts->retained_a;
	memcpy(NCS_MEM->retained_buf_a, ts->retained_buf_a, sizeof(NCS_MEM->retained_buf_a));
	memcpy(NCS_MEM->retained_buf_b, ts->retained_buf_b, sizeof(NCS_MEM->retained_buf_b));
}

void restore_dashboard_ts_from_ncs_mem(void)
{
	struct GlassesState *gs = GLOBAL_STATE;

	gs->dashboard_ts->ts = NCS_MEM->ts + 1;
	gs->dashboard_ts->weather_icon = NCS_MEM->weather_icon;
	gs->dashboard_ts->temperature = NCS_MEM->temperature;
	gs->dashboard_ts->ms_ts = NCS_MEM->ms_ts;
	gs->dashboard_ts->temp_unit = NCS_MEM->temp_unit;
	gs->dashboard_ts->time_disp_mode = NCS_MEM->time_disp_mode;
	gs->dashboard_ts->retained_b = NCS_MEM->retained_b;
	gs->dashboard_ts->retained_a = NCS_MEM->retained_a;
	gs->dashboard_ts->retained_c = NCS_MEM->retained_c;
	if (NCS_MEM->notes_index < 4) {
		notes_index = NCS_MEM->notes_index;
	}
	memcpy(gs->dashboard_ts->retained_buf_a, NCS_MEM->retained_buf_a,
	       sizeof(NCS_MEM->retained_buf_a));
	memcpy(gs->dashboard_ts->retained_buf_b, NCS_MEM->retained_buf_b,
	       sizeof(NCS_MEM->retained_buf_b));
}

void set_ncs_mem_notes_index(uint8_t idx)
{
	NCS_MEM->notes_index = idx;
}

void deinit_ncs_memory(void)
{
	NCS_MEM->ms_ts = 0;
	NCS_MEM->ts = 0;
	NCS_MEM->weather_icon = 0;
	NCS_MEM->temperature = 0;
	NCS_MEM->temp_unit = 0;
	NCS_MEM->time_disp_mode = 0;
	NCS_MEM->retained_a = 0;
	NCS_MEM->retained_b = 0;
	NCS_MEM->retained_c = 0;
	memset(NCS_MEM->retained_buf_a, 0, sizeof(NCS_MEM->retained_buf_a));
	memset(NCS_MEM->retained_buf_b, 0, sizeof(NCS_MEM->retained_buf_a));
	NCS_MEM->magic = 0;
}
