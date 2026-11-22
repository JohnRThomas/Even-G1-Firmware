/* IPC service glue between the application core and the network core (ipc_host.c) */
#ifndef G1_IPC_HOST_H_
#define G1_IPC_HOST_H_

#include <glasses_state.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>

/* ipc_host.c */
int global_ipc_service_send(const void *data, size_t len);
int serialization_init(void);
int register_ipc_service_context(struct ipc_service_context *ctx);

#endif /* G1_IPC_HOST_H_ */
