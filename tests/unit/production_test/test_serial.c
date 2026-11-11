/* Frame: 5a a5 <7f req | ff rsp> <n> <payload> <sum8 of all before> */
#include <zephyr/drivers/uart.h>

#include "pt_test.h"

/* the UART calls of serial_port.c, renamed to these fakes */
DEFINE_FAKE_VALUE_FUNC(int, g1_pt_uart_tx, const struct device *, const uint8_t *, size_t,
           int32_t);
FAKE_VOID_FUNC(g1_pt_uart_poll_out, const struct device *, unsigned char);
FAKE_VALUE_FUNC(int, g1_pt_uart_callback_set, const struct device *, uart_callback_t, void *);
FAKE_VALUE_FUNC(int, g1_pt_uart_rx_enable, const struct device *, uint8_t *, size_t, int32_t);
FAKE_VALUE_FUNC(int, g1_pt_uart_rx_buf_rsp, const struct device *, uint8_t *, size_t);
#define uart_tx           g1_pt_uart_tx
#define uart_poll_out     g1_pt_uart_poll_out
#define uart_callback_set g1_pt_uart_callback_set
#define uart_rx_enable    g1_pt_uart_rx_enable
#define uart_rx_buf_rsp   g1_pt_uart_rx_buf_rsp
#include "production_test/serial_port.c"
#undef uart_tx
#undef uart_poll_out
#undef uart_callback_set
#undef uart_rx_enable
#undef uart_rx_buf_rsp

struct g1_pkt_log g1_pt_uart_tx_log;
struct device g1_pt_uart_dev = {.name = "g1_unit_lpuart"};

/* what read_serial_data() returned; every test starts with payload_len 0 */
static uint8_t payload[256];
static uint32_t payload_len;

static int uart_tx_log(const struct device *dev, const uint8_t *buf, size_t len, int32_t timeout)
{
  ARG_UNUSED(dev);
  ARG_UNUSED(timeout);
  g1_pkt_log_add(&g1_pt_uart_tx_log, buf, len);
  return 0;
}

static void uart_poll_out_log(const struct device *dev, unsigned char byte)
{
  ARG_UNUSED(dev);
  g1_pkt_log_add(&g1_pt_uart_tx_log, &byte, 1);
}

static void serial_test_reset(bool keep_media)
{
  ARG_UNUSED(keep_media);

  RESET_FAKE(g1_pt_uart_tx);
  RESET_FAKE(g1_pt_uart_poll_out);
  RESET_FAKE(g1_pt_uart_callback_set);
  RESET_FAKE(g1_pt_uart_rx_enable);
  g1_pt_uart_tx_fake.custom_fake = uart_tx_log;
  g1_pt_uart_poll_out_fake.custom_fake = uart_poll_out_log;
  g1_pkt_log_clear(&g1_pt_uart_tx_log);
  payload_len = 0;
}
G1_UNIT_RESET_HOOK(serial_test_reset);

/* uart_callback() may read buf[-1] */
static uint8_t rx_area[1 + BUF_SIZE];

static void receive_bytes(const uint8_t *bytes, size_t len, size_t offset)
{
  uint8_t *buf = &rx_area[1];
  struct uart_event evt = {.type = UART_RX_RDY};

  memset(rx_area, 0, sizeof(rx_area));
  memcpy(buf + offset, bytes, len);
  evt.data.rx.buf = buf;
  evt.data.rx.offset = offset;
  evt.data.rx.len = len;
  uart_callback(&g1_pt_uart_dev, &evt, &g1_pt_uart_dev);
}

void pt_test_uart_receive(const char *hex, size_t offset)
{
  uint8_t bytes[BUF_SIZE];

  receive_bytes(bytes, g1_hex(hex, bytes, sizeof(bytes)), offset);
}

/* a request frame of len bytes with the payload 00 01 02 ... */
static void receive_pattern_frame(uint8_t *frame, size_t len)
{
  g1_hex("5a a5 7f", frame, len);
  frame[3] = len - 5;
  g1_fill_pattern(&frame[4], len - 5, 0);
  frame[len - 1] = g1_sum8(frame, len - 1);
  receive_bytes(frame, len, 0);
}

/* read_serial_data() into payload and payload_len */
static int read_payload(void)
{
  uint8_t *dest = payload;

  return read_serial_data(&g1_pt_uart_dev, &dest, &payload_len);
}

/* ---- read_serial_data() ---- */

ZTEST(production_test, test_serial_frame_payload_is_returned)
{
  uint8_t *dest = payload;

  pt_test_uart_receive("5a a5 7f 04 24 01 01 00 a8", 0);

  zassert_equal(read_serial_data(&g1_pt_uart_dev, &dest, &payload_len), 0);
  expect_bytes(payload, payload_len, "24 01 01 00");
  zassert_equal(dest, payload, "the payload is copied into the caller's buffer");
}

ZTEST(production_test, test_serial_frame_after_noise_is_found)
{
  pt_test_uart_receive("11 5a 33 5a a5 7f 01 07 86", 0);

  zassert_equal(read_payload(), 0);
  expect_bytes(payload, payload_len, "07");
}

ZTEST(production_test, test_serial_frame_at_event_offset_is_found)
{
  pt_test_uart_receive("5a a5 7f 01 07 86", 100);

  zassert_equal(read_payload(), 0);
  expect_bytes(payload, payload_len, "07");
}

ZTEST(production_test, test_serial_frame_with_bad_checksum_is_dropped)
{
  pt_test_uart_receive("5a a5 7f 01 07 87", 0);

  zassert_equal(read_payload(), -2);
}

ZTEST(production_test, test_serial_response_header_is_not_a_request)
{
  pt_test_uart_receive("5a a5 ff 01 07 06", 0);

  zassert_equal(read_payload(), -2);
}

ZTEST(production_test, test_serial_read_without_frame_returns_minus_2)
{
  zassert_equal(read_payload(), -2);
}

ZTEST(production_test, test_serial_frame_is_consumed_by_read)
{
  pt_test_uart_receive("5a a5 7f 01 07 86", 0);
  zassert_equal(read_payload(), 0);
  zassert_equal(read_payload(), -2);
}

ZTEST(production_test, test_serial_read_rejects_null_arguments)
{
  uint8_t *dest = payload;

  pt_test_uart_receive("5a a5 7f 01 07 86", 0);

  zassert_equal(read_serial_data(NULL, &dest, &payload_len), -1);
  zassert_equal(read_serial_data(&g1_pt_uart_dev, NULL, &payload_len), -1);
  zassert_equal(read_serial_data(&g1_pt_uart_dev, &dest, NULL), -1);
  zassert_equal(read_payload(), 0, "frame still pending");
}

ZTEST(production_test, test_serial_read_of_invalid_buffer_returns_minus_3)
{
  memset(serial_rx_buf, 0x5a, sizeof(serial_rx_buf));
  serial_rx_len = 6;

  zassert_equal(read_payload(), -3);
  zassert_equal(read_payload(), -2);
}

ZTEST(production_test, test_serial_rx_clear_drops_pending_frame)
{
  pt_test_uart_receive("5a a5 7f 01 07 86", 0);
  serial_rx_clear();

  zassert_equal(read_payload(), -2);
}

ZTEST(production_test, test_serial_frame_of_255_bytes_is_received)
{
  uint8_t frame[255];

  receive_pattern_frame(frame, sizeof(frame));

  zassert_equal(read_payload(), 0);
  zassert_equal(payload_len, 250);
  zassert_mem_equal(payload, &frame[4], 250);
}

/* DEFECT: the frame length n + 5 is a uint8_t, so 256 wraps to 0 and the frame is lost. */
ZTEST_EXPECT_FAIL(production_test, test_serial_frame_filling_the_whole_buffer_is_received);
ZTEST(production_test, test_serial_frame_filling_the_whole_buffer_is_received)
{
  uint8_t frame[256];

  receive_pattern_frame(frame, sizeof(frame));

  zassert_equal(read_payload(), 0);
  zassert_equal(payload_len, 251);
}

/* ---- lpuart_async_a/b() ---- */

ZTEST(production_test, test_uart_setup_registers_callback_and_enables_rx)
{
  zassert_equal(lpuart_async_a(&g1_pt_uart_dev), 0);

  zassert_equal(g1_pt_uart_callback_set_fake.call_count, 1);
  zassert_equal(g1_pt_uart_callback_set_fake.arg0_val, &g1_pt_uart_dev);
  zassert_equal(g1_pt_uart_callback_set_fake.arg1_val, uart_callback);
  zassert_equal(g1_pt_uart_callback_set_fake.arg2_val, &g1_pt_uart_dev);
  zassert_equal(g1_pt_uart_rx_enable_fake.call_count, 1);
  zassert_not_null(g1_pt_uart_rx_enable_fake.arg1_val);
  zassert_equal(g1_pt_uart_rx_enable_fake.arg2_val, 256);
  zassert_equal(lpuart_async_a(NULL), -1);
}

/* the stray 0x5a starts no valid header */
ZTEST(production_test, test_uart_write_sends_buffer_then_first_byte_again)
{
  uint8_t frame[] = {0x5a, 0xa5, 0xff, 0x01, 0x07, 0x06};

  zassert_equal(lpuart_async_b(&g1_pt_uart_dev, frame, sizeof(frame)), 0);

  zassert_equal(g1_pt_uart_tx_log.count, 2);
  expect_pkt(&g1_pt_uart_tx_log, 0, "5a a5 ff 01 07 06");
  expect_pkt(&g1_pt_uart_tx_log, 1, "5a");
}

ZTEST(production_test, test_uart_write_rejects_null_and_empty)
{
  uint8_t byte = 0;

  zassert_equal(lpuart_async_b(NULL, &byte, 1), -1);
  zassert_equal(lpuart_async_b(&g1_pt_uart_dev, NULL, 1), -1);
  zassert_equal(lpuart_async_b(&g1_pt_uart_dev, &byte, 0), -1);
  zassert_equal(g1_pt_uart_tx_fake.call_count, 0);
}

ZTEST(production_test, test_uart_write_driver_error_returns_minus_2)
{
  uint8_t byte = 0x5a;

  G1_FAKE_RETURN(g1_pt_uart_tx, -EBUSY);

  zassert_equal(lpuart_async_b(&g1_pt_uart_dev, &byte, 1), -2);
  zassert_equal(g1_pt_uart_poll_out_fake.call_count, 0);
}
