/* Zephyr's dmic_nrfx_pdm.c, #included for its state, with the Kconfig native_sim lacks. */
#define CONFIG_SOC_SERIES_NRF53X        1
#define CONFIG_AUDIO_DMIC_LOG_LEVEL     1
#define CONFIG_AUDIO_DMIC_INIT_PRIORITY 80

#include G1_UNIT_DMIC_NRFX_PDM_C

#include "pdm_driver.h"

bool pdm_driver_active(void)
{
	return dmic_nrfx_pdm_data0.active;
}

int pdm_driver_queued(void)
{
	return k_msgq_num_used_get(&dmic_nrfx_pdm_data0.rx_queue);
}

void pdm_driver_reset(void)
{
	struct dmic_nrfx_pdm_drv_data *d = &dmic_nrfx_pdm_data0;
	void *buffer;

	if (d->active) {
		(void)dmic_nrfx_pdm_trigger(DEVICE_DT_GET(PDM(0)), DMIC_TRIGGER_STOP);
	}
	while (k_msgq_get(&d->rx_queue, &buffer, K_NO_WAIT) == 0) {
		free_buffer(d, buffer);
	}
	d->active = false;
	d->stopping = false;
}
