/* Each test starts with the SPI wired but closed and factory brightness. */
#ifndef PANEL_SUPPORT_H_
#define PANEL_SUPPORT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/fff.h>
#include <glasses_state.h>

#include "fakes_nrfx.h"
#include "panel_display/panel.h"
#include "spim_spis/spim.h"

#define PANEL_SPI_BUS  4
#define PANEL_SPI_SCK  8
#define PANEL_SPI_MOSI 9
#define PANEL_SPI_MISO 10
#define PANEL_SPI_SS   11
#define PANEL_SPI_MHZ  32

#define PANEL_DEFAULT_LEVEL   0x15
#define PANEL_DEFAULT_LUMGEAR 0x3f

/* the lens draws 640x200 */
#define JBD_ROW_BYTES 320
#define FRAME_ROWS    200
#define FRAME_BYTES   (FRAME_ROWS * JBD_ROW_BYTES)

/* no firmware header declares these */
int panel_set_brightness_level(struct jbd_panel_context *c, uint8_t level);
int panel_suspend(struct jbd_panel_context *c);
int panel_off(struct jbd_panel_context *c);
int panel_resume(struct jbd_panel_context *c);
int panel_on(struct jbd_panel_context *c);
int jdb_panel_init(struct jbd_panel_context *c);

DECLARE_FAKE_VALUE_FUNC(int, getDeviceRole);
DECLARE_FAKE_VALUE_FUNC(int, power_for_panel);
DECLARE_FAKE_VALUE_FUNC(int, power_down_panel);
DECLARE_FAKE_VALUE_FUNC(int, opt3007_chip_init, struct opt3007_context *);
DECLARE_FAKE_VOID_FUNC(opt3007_chip_deinit);
DECLARE_FAKE_VALUE_FUNC(int, panel_related, struct jbd_panel_context *, int, void *);

/* SPIM log index at each fake's last call, -1 if none */
struct panel_calls {
	int power_on_at;
	int power_down_at;
	int opt_init_at;
	int opt_deinit_at;
};
extern struct panel_calls panel_calls;

struct jbd_panel_context *panel_ctx(void);
struct spi_master_ctx *panel_spi(void);
uint8_t **panel_fb(void);
uint8_t *panel_frame(void);

void panel_spi_up(void);
/* init_done without the init transactions */
void panel_running(void);
void panel_answer_id(void);

void panel_reset_reg_cache(void);
int panel_cached_lum(void);
int panel_cached_cur(void);

int panel_xfer_count(void);
const struct g1_spim_entry *panel_xfer(int n);
bool panel_xfer_check(int n, const char *hex, char *msg, size_t size);
/* cache writes: "02 x=<x> y=<y> bytes=<n>", n without the last byte */
void panel_journal_line(const struct g1_spim_entry *e, char *out, size_t size);
bool panel_journal_check(int first, const char *const *lines, int n, char *msg, size_t size);

#define PANEL_MSG_SIZE 2048

#define expect_xfer(n, hex)                                                                        \
	do {                                                                                       \
		static char _p_msg[PANEL_MSG_SIZE];                                                \
		zassert_true(panel_xfer_check((n), (hex), _p_msg, sizeof(_p_msg)), "%s", _p_msg); \
	} while (0)

#define expect_journal(first, lines, n)                                                            \
	do {                                                                                       \
		static char _p_msg[PANEL_MSG_SIZE];                                                \
		zassert_true(panel_journal_check((first), (lines), (n), _p_msg, sizeof(_p_msg)),   \
			     "%s", _p_msg);                                                        \
	} while (0)

#endif /* PANEL_SUPPORT_H_ */
