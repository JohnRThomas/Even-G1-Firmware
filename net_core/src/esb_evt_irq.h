/*
 * Moves the ESB event interrupt of the NCS ESB driver from SWI0 to SWI3.
 *
 * On the nRF5340 network core MPSL uses SWI0 for  itslow-priority processing
 * (MPSL_LOW_PRIO_IRQn).  esb_init() connects the ESB event handler to
 * ESB_EVT_IRQ at run time, so with the SDK's SWI0 it would replace the MPSL
 * handler and the Bluetooth controller would stop doing its low-priority work
 * once ESB starts.  Nothing else in this image uses SWI3.
 *
 * TODO(SDK v2.6.0): This is configurable in the newer SDK version, remove this
 * when the version is pushed forward. 
 *
 * ../CMakeLists.txt force-includes this header in front of the SDK's esb.c,
 * whose own #include of esb_peripherals.h then finds the include guard set.
 * The SDK itself is not modified.
 */
#ifndef NET_ESB_EVT_IRQ_H_
#define NET_ESB_EVT_IRQ_H_

#include "esb_peripherals.h"

#undef ESB_EVT_IRQ
#define ESB_EVT_IRQ        SWI3_IRQn
#undef ESB_EVT_IRQHandler
#define ESB_EVT_IRQHandler SWI3_IRQHandler

#endif /* NET_ESB_EVT_IRQ_H_ */
