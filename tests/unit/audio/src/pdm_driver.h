#ifndef G1_UNIT_AUDIO_PDM_DRIVER_H_
#define G1_UNIT_AUDIO_PDM_DRIVER_H_

#include <stdbool.h>

bool pdm_driver_active(void);
int pdm_driver_queued(void);
/* stop, and free the queued blocks to the slab */
void pdm_driver_reset(void);

#endif /* G1_UNIT_AUDIO_PDM_DRIVER_H_ */
