/* Host stand-in for <pinctrl_soc.h>; one dummy pin per state. */
#ifndef G1_UNIT_PINCTRL_SOC_H_
#define G1_UNIT_PINCTRL_SOC_H_

#include <stdint.h>

typedef uint32_t pinctrl_soc_pin_t;

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop) {0}

#endif /* G1_UNIT_PINCTRL_SOC_H_ */
