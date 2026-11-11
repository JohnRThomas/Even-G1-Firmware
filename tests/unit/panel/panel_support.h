/*
 * The panel suites' fake nrfx SPIM and helpers. Each test starts with the panel SPI wired as
 * init_context() wires it but closed, the panel callbacks on a fresh frame buffer and factory
 * brightness.
 */
#ifndef PANEL_SUPPORT_H_
#define PANEL_SUPPORT_H_

#include <zephyr/fff.h>

#include "harness.h"
#include "panel_display/panel.h"

/*
 * Fake nrfx SPIM: it logs every call; nrfx_spim_xfer() completes before returning (handler
 * included) and returns NRFX_ERROR_INVALID_STATE when uninitialised, where nrfx asserts.
 * xfer_error applies after xfer_error_skip successful transfers, for xfer_error_count of them
 * (0 = all).
 */
DECLARE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_spim_init, nrfx_spim_t const *,
      nrfx_spim_config_t const *, nrfx_spim_evt_handler_t, void *);
DECLARE_FAKE_VOID_FUNC(nrfx_spim_uninit, nrfx_spim_t const *);
DECLARE_FAKE_VALUE_FUNC(nrfx_err_t, nrfx_spim_xfer, nrfx_spim_t const *,
      nrfx_spim_xfer_desc_t const *, uint32_t);

enum g1_fake_spim_op {
  G1_SPIM_OP_INIT = 1,
  G1_SPIM_OP_UNINIT,
  G1_SPIM_OP_XFER,
};

#define G1_FAKE_SPIM_LOG_MAX 512

struct g1_spim_entry {
  enum g1_fake_spim_op op;
  uint8_t inst;          /* SPIM3 or SPIM4 */
  nrfx_err_t result;
  uint32_t t_us;         /* simulated time, us */
  /* G1_SPIM_OP_XFER only */
  uint32_t flags;
  size_t tx_len;
  size_t rx_len;
  size_t len;            /* max(tx_len, rx_len); 0 on failure */
  const uint8_t *mosi;   /* the TX bytes, then the over-read character; NULL on failure */
};

struct g1_fake_spim {
  struct {
    bool initialized;
    nrfx_spim_config_t config;
    nrfx_spim_evt_handler_t handler;
    void *context;
  } inst[5];
  /* set by the test; transfers starting with miso_opcode clock in miso, then zeros */
  nrfx_err_t xfer_error;
  int xfer_error_skip;
  int xfer_error_count;
  uint8_t miso_opcode;
  const uint8_t *miso;
  size_t miso_len;
  int count;     /* log entries */
  int xfers;     /* G1_SPIM_OP_XFER entries */
  int too_long;  /* longer than the EasyDMA limit of 0xffff bytes (nrfx asserts) */
  struct g1_spim_entry log[G1_FAKE_SPIM_LOG_MAX];
  size_t pool_used;
  uint8_t pool[1024 * 1024];
};

extern struct g1_fake_spim g1_fake_spim;

/* log index of the first op / transfer starting with opcode, -1 if none */
int g1_fake_spim_find(enum g1_fake_spim_op op);
int g1_fake_spim_find_cmd(uint8_t opcode);
void g1_fake_spim_clear_log(void);

#define PANEL_DEFAULT_LEVEL   0x15
#define PANEL_DEFAULT_LUMGEAR 0x3f

/* the lens draws 640x200 */
#define JBD_ROW_BYTES 320
#define FRAME_ROWS    200
#define FRAME_BYTES   (FRAME_ROWS * JBD_ROW_BYTES)

struct jbd_panel_context *panel_ctx(void);
struct spi_master_ctx *panel_spi(void);
uint8_t *panel_frame(void);

void panel_spi_up(void);
/* init_done without the init transactions */
void panel_running(void);
void panel_answer_id(void);

/* the index-th transfer (0 = first), NULL if none */
const struct g1_spim_entry *panel_xfer(int index);
/*
 * Transfer index clocked exactly the bytes of hex on SPIM4; "02 x=<x> y=<y> bytes=<n>" stands for
 * a cache write of n pixel bytes (and the dummy byte) at that address.
 */
bool panel_xfer_check(int index, const char *hex, char *msg, size_t msg_size);
/* there were exactly count transfers, each as panel_xfer_check() of its hex */
bool panel_xfers_check(const char *const *hex, int count, char *msg, size_t msg_size);

#define expect_xfer(index, hex)      G1_EXPECT(panel_xfer_check, (index), (hex))
#define expect_xfer_list(hex, count) G1_EXPECT(panel_xfers_check, (hex), (count))
/* exactly these transfers: expect_xfers("c0 0c 0a", "97") */
#define expect_xfers(...)                                                                          \
  do {                                                                                       \
    static const char *const _panel_xfers[] = {__VA_ARGS__};                           \
    expect_xfer_list(_panel_xfers, sizeof(_panel_xfers) / sizeof(_panel_xfers[0]));    \
  } while (0)

/* transfer index is a cache write whose 5-byte header is hex */
const struct g1_spim_entry *expect_window(int index, const char *header);
/* expect_window(), then pixel_bytes bytes of fill and the dummy byte */
void expect_fill_window(int index, const char *header, uint8_t fill, size_t pixel_bytes);
/* clr_cache(): the 480 cache rows in three windows from transfer first */
void expect_clear_windows(int first, uint8_t fill);

#endif /* PANEL_SUPPORT_H_ */
