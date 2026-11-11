/* sync_to_slave() / sync_to_master() fakes. */
#define FFF_GCC_FUNCTION_ATTRIBUTES __attribute__((weak))

#include "fakes.h"

#include "threads/threads.h"

DEFINE_FAKE_VALUE_FUNC(int, sync_to_slave, struct GlassesState *, int, void *, size_t);
DEFINE_FAKE_VALUE_FUNC(unsigned int, sync_to_master, struct GlassesState *, int, void *, size_t);

struct g1_pkt_log g1_sync_to_slave_log;

static int sync_to_slave_default(struct GlassesState *state, int cmd, void *data, size_t len)
{
  sync_to_slave_Fake *fake = &sync_to_slave_fake;

  ARG_UNUSED(state);
  ARG_UNUSED(cmd);
  g1_pkt_log_add(&g1_sync_to_slave_log, data, data ? len : 0);
  /* as FFF returns return_val, or return_val_seq in turn when set */
  if (fake->return_val_seq_len == 0) {
    return fake->return_val;
  }
  if (fake->return_val_seq_idx < fake->return_val_seq_len) {
    return fake->return_val_seq[fake->return_val_seq_idx++];
  }
  return fake->return_val_seq[fake->return_val_seq_len - 1];
}

static void fakes_ipc_reset(bool keep_media)
{
  ARG_UNUSED(keep_media);

  RESET_FAKE(sync_to_slave);
  RESET_FAKE(sync_to_master);
  sync_to_slave_fake.custom_fake = sync_to_slave_default;
  g1_pkt_log_clear(&g1_sync_to_slave_log);
}

G1_UNIT_RESET_HOOK(fakes_ipc_reset);
