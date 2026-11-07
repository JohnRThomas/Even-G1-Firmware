/*
 * Even Realities G1 network core - shared definitions (reconstruction of v1.63).
 */
#ifndef NET_COMMON_H_
#define NET_COMMON_H_

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>
#include <stddef.h>

/* Vendor log level (0x21000580, .data, initial value 2). */
extern int net_log_level;

#define NET_ERR(...) do { if (net_log_level > 0) { printk(__VA_ARGS__); } } while (0)
#define NET_INF(...) do { if (net_log_level > 1) { printk(__VA_ARGS__); } } while (0)

struct net_ctx;

/* Endpoint multiplexed over the "ipc0" IPC endpoint: first payload byte = id. */
typedef int (*ipc_ep_cb_t)(struct net_ctx *ctx, const uint8_t *data, size_t len);

struct ipc_ep {
  uint8_t id;
  const char *name;
  ipc_ep_cb_t cb;
};                                              /* 0x0c */

#define IPC_EP_MAX 22

struct ipc_ops {
  void *priv;                             /* +0x00 (unused?) */
  int count;                              /* +0x04 */
  struct ipc_ep *eps[IPC_EP_MAX];         /* +0x08 */
  int (*send)(const void *data, size_t len);   /* +0x60 */
  int (*reg)(struct ipc_ep *ep);          /* +0x64 */
};                                              /* 0x68 */

#define ESB_OPS_BUF_SIZE 0x100

struct esb_ops {
  void (*start)(void);                    /* starts the ESB thread */
  struct ipc_ep ep;
  uint8_t *buf;                           /* ESB_OPS_BUF_SIZE bytes */
};

struct net_ctx {
  int role;                               /* 0x00: 1 = master (M), 2 = slave (S) */
  int mode;                               /* 0x04 */
  int channel;                            /* 0x08: 0xff = unset */
  struct k_sem sem0;                      /* 0x0c */
  struct k_sem sem1;                      /* 0x24 */
  struct ipc_ep hwid_ep;                  /* 0x3c: "cpunet-hw-id" */
  struct ipc_ops ipc;                     /* 0x48 */
  struct esb_ops esb;                     /* 0xb0 */
  uint8_t mac0[6];                        /* 0xc4 */
  uint8_t mac1[6];                        /* 0xca */
};

#endif /* NET_COMMON_H_ */
