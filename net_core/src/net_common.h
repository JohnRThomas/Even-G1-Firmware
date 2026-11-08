/*
 * Even Realities G1 network core - shared definitions.
 */
#ifndef NET_COMMON_H_
#define NET_COMMON_H_

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>
#include <stddef.h>

/* Log level (initial value 2). */
extern int net_log_level;

#define NET_ERR(...)                                                                               \
	do {                                                                                       \
		if (net_log_level > 0) {                                                           \
			printk(__VA_ARGS__);                                                       \
		}                                                                                  \
	} while (0)
#define NET_INF(...)                                                                               \
	do {                                                                                       \
		if (net_log_level > 1) {                                                           \
			printk(__VA_ARGS__);                                                       \
		}                                                                                  \
	} while (0)

struct net_ctx;

/* Endpoint multiplexed over the "ipc0" IPC endpoint: first payload byte = id. */
typedef int (*ipc_ep_cb_t)(struct net_ctx *ctx, const uint8_t *data, size_t len);

struct ipc_ep {
	uint8_t id;
	const char *name;
	ipc_ep_cb_t cb;
};

#define IPC_EP_MAX 22

struct ipc_ops {
	void *priv;
	int count;
	struct ipc_ep *eps[IPC_EP_MAX];
	int (*send)(const void *data, size_t len);
	int (*reg)(struct ipc_ep *ep);
};

#define ESB_OPS_BUF_SIZE 0x100

struct esb_ops {
	void (*start)(void);                    /* starts the ESB thread */
	struct ipc_ep ep;
	uint8_t *buf;                           /* ESB_OPS_BUF_SIZE bytes */
};

struct net_ctx {
	int role;                               /* 1 = master (M), 2 = slave (S) */
	int mode;
	int channel;                            /* 0xff = unset */
	struct k_sem sem0;
	struct k_sem sem1;
	struct ipc_ep hwid_ep;                  /* "cpunet-hw-id" */
	struct ipc_ops ipc;
	struct esb_ops esb;
	uint8_t mac0[6];
	uint8_t mac1[6];
};

#endif /* NET_COMMON_H_ */
