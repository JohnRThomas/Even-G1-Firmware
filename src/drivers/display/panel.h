/*
 * JBD013VGA micro-LED panel layer (jbd_panel.c, panel_fb.c, jbd_panel_flush.c,
 * jbd013_cmd.c, jbd013_spi.c).
 */
#ifndef G1_PANEL_H_
#define G1_PANEL_H_

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include "system/glasses_state.h"

/* JBD013VGA SPI commands */
#define SPI_WR_ENABLE            0x06
#define SPI_WR_STATUS_REG1       0x01
#define SPI_WR_STATUS_REG2       0x31
#define SPI_WR_CURRENT_REG       0x46
#define SPI_WR_LUM_REG           0x36
#define SPI_WR_OFFSET_REG        0xc0
#define SPI_RD_ID                0x9f
#define SPI_SYNC                 0x97
#define SPI_RST_EN               0x66
#define SPI_RST                  0x99
#define SPI_DISPLAY_ENABLE       0xa3
#define SPI_DISPLAY_DISABLE      0xa9
#define SPI_DISPLAY_DEFAULT_MODE 0x71
#define SPI_DISPLAY_RL           0x73
#define SPI_DEEP_POWER_DOWN      0xb9
#define SPI_WR_BUFFER            0x02

#define JBD_PANEL_DEVICE_ID 0x4010

#define PANEL_WIDTH     640
#define PANEL_FB_STRIDE (PANEL_WIDTH / 2)          /* 4 bpp */
#define PANEL_FB_SIZE   (PANEL_FB_ROWS * PANEL_FB_STRIDE)

#define LP_JBD_BIT BIT(8)

#endif /* G1_PANEL_H_ */
