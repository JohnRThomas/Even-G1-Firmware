/*
 * "ipc0" IPC service endpoint shared by the logical endpoints of struct ipc_ops.
 */
#ifndef NET_IPC_H_
#define NET_IPC_H_

#include "net_common.h"

int ipc_init(void);
int ipc_ops_init(struct ipc_ops *ops);

#endif /* NET_IPC_H_ */
