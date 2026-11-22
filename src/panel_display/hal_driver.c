/*
 * JBD013VGA SPI transport, derived from the JBD reference code, on top of
 * the spi_interface_t of the panel context.
 */
#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include "g1_log.h"
#include "glasses_state.h"
#include "panel.h"

/* 02h, three address bytes, FFh */
#define WR_BUFFER_HDR_SIZE 5

/* write the frame buffer to the panel cache at column col / row row */
void spi_wr_buffer(uint16_t col, uint16_t row, uint8_t *buf, uint32_t len) {
  uint8_t rx[1] = {0};
  struct jbd_panel_context *c = get_jdb_panel_context();
  uint8_t *p = buf;
  uint32_t addr = (((uint32_t)row << 10U) & 0x7fc00U) | ((uint32_t)col & 0x3ffU);
  uint8_t backup[WR_BUFFER_HDR_SIZE];

  /* the header goes in front of buf, over the end of the previous row inside the frame */
  memcpy(backup, p - sizeof(backup), sizeof(backup));
  p[-5] = SPI_WR_BUFFER;
  p[-4] = addr >> 16U;
  p[-3] = addr >> 8U;
  p[-2] = addr;
  p[-1] = 0xff;
  lock_lcd_mutex(K_FOREVER);
  c->spi_callbacks->xfer_cb(c->spi_callbacks, p - 5, len + 6U, rx, 1);
  unlock_lcd_mutex();
  memcpy(p - sizeof(backup), backup, sizeof(backup));
}

uint16_t spi_read_id(void) {
  uint32_t tx;
  uint8_t rx[4] = {0};
  struct jbd_panel_context *c = get_jdb_panel_context();

  tx = SPI_RD_ID;

  lock_lcd_mutex(K_FOREVER);
  c->spi_callbacks->xfer_cb(c->spi_callbacks, (uint8_t *)&tx, 4, rx, 4);
  unlock_lcd_mutex();
  G1_LOG_INF("jbd panel devicve id: %02x, %02x, %02x, %02x\n", rx[0], rx[1], rx[2], rx[3]);
  return sys_get_be16(&rx[2]);
}

void delay_ms(uint32_t ms) {
  k_busy_wait(ms * 1000U);
}

void spi_send_cmd(uint8_t cmd) {
  uint8_t rx[1] = {0};
  struct jbd_panel_context *c = get_jdb_panel_context();

  lock_lcd_mutex(K_FOREVER);
  c->spi_callbacks->xfer_cb(c->spi_callbacks, &cmd, 1, rx, 1);
  unlock_lcd_mutex();
}

#define SPI_TX_CHUNK 0xf000U

int spi_tx_data(const uint8_t *buf, uint32_t len) {
  struct jbd_panel_context *c = get_jdb_panel_context();
  uint8_t rx[1] = {0};
  uint32_t whole = len / SPI_TX_CHUNK * SPI_TX_CHUNK;
  uint32_t rem = len - whole;
  const uint8_t *end = buf + whole;
  const uint8_t *cur = buf;

  for (; cur != end; cur += SPI_TX_CHUNK) {
    lock_lcd_mutex(K_FOREVER);
    c->spi_callbacks->xfer_cb(c->spi_callbacks, cur, SPI_TX_CHUNK, rx, 1);
    unlock_lcd_mutex();
  }
  lock_lcd_mutex(K_FOREVER);
  c->spi_callbacks->xfer_cb(c->spi_callbacks, cur, rem, rx, 1);
  return unlock_lcd_mutex();
}

/* send one command frame: cmd followed by len parameter bytes */
void spi_tx_frame(uint8_t cmd, const uint8_t *param, uint32_t len) {
  uint8_t buf[6];

  buf[0] = cmd;
  memcpy(&buf[1], param, len);
  spi_tx_data(buf, len + 1U);
}
