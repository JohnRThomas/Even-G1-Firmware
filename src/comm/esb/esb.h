/*
 * ESB link between the two lenses (esb_rx.c, esb_tx.c, esb_slave.c).  The radio
 * runs on the network core; the application core sends and receives packets
 * over IPC.
 */
#ifndef G1_COMM_ESB_ESB_H_
#define G1_COMM_ESB_ESB_H_

#include <stdint.h>
#include <stddef.h>
#include "system/glasses_state.h"

/* esb_rx.c: IPC endpoint receive callbacks */
extern uint8_t slave_msg_key_pending; /* the slave reported a key press on a message */
int esb_master_recv(struct GlassesState *gs, const uint8_t *data, size_t len);
int esb_slave_recv(struct GlassesState *gs, const uint8_t *data, size_t len);
int esb_ctrl_recv(struct GlassesState *gs, const uint8_t *data, size_t len);

/* esb_tx.c */
extern uint8_t push_msg_syncing; /* a notification is being copied to the other lens */
int sync_to_slave(struct GlassesState *state, int cmd, void *data, size_t len);
unsigned int sync_to_master(struct GlassesState *state, int cmd, void *data, size_t len);
void set_band_for_ESB(uint8_t band);
void esb_sync_start(struct GlassesState *state);

/* esb_slave.c */
extern uint8_t display_suspend_received;
void esb_slave_start(struct GlassesState *state);

#endif /* G1_COMM_ESB_ESB_H_ */
