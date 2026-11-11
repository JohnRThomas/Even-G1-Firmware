/* Frame: 5a a5 <7f req | ff rsp> <n> <payload> <sum8 of all before> */
#include <string.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "fakes_pt.h"
#include "pt_test.h"

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

#include "production_test/production_test.h"

const struct device g1_pt_uart_dev = {.name = "g1_unit_lpuart"};

/* uart_callback() may read buf[-1] */
static uint8_t rx_area[1 + BUF_SIZE];

void pt_test_uart_receive(const uint8_t *bytes, size_t n, size_t offset)
{
	uint8_t *buf = &rx_area[1];
	struct uart_event evt = {.type = UART_RX_RDY};

	memset(rx_area, 0, sizeof(rx_area));
	memcpy(buf + offset, bytes, n);
	evt.data.rx.buf = buf;
	evt.data.rx.offset = offset;
	evt.data.rx.len = n;
	uart_callback(&g1_pt_uart_dev, &evt, (void *)&g1_pt_uart_dev);
}

uint8_t pt_test_sum8(const uint8_t *buf, size_t n)
{
	uint8_t s = 0;

	while (n--) {
		s += *buf++;
	}
	return s;
}

/* ---- read_serial_data() ---- */

ZTEST(production_test, test_serial_frame_payload_is_returned)
{
	static const uint8_t frame[] = {0x5a, 0xa5, 0x7f, 0x04, 0x24, 0x01, 0x01, 0x00, 0xa8};
	uint8_t out[256];
	uint8_t *p = out;
	uint32_t len = 0;

	pt_test_uart_receive(frame, sizeof(frame), 0);

	zassert_equal(read_serial_data(&g1_pt_uart_dev, &p, &len), 0, NULL);
	zassert_equal(len, 4, NULL);
	zassert_mem_equal(out, &frame[4], 4, NULL);
	zassert_equal(p, out, "the payload is copied into the caller's buffer");
}

ZTEST(production_test, test_serial_frame_after_noise_is_found)
{
	static const uint8_t bytes[] = {0x11, 0x5a, 0x33, 0x5a, 0xa5, 0x7f, 0x01, 0x07, 0x86};
	uint8_t out[256];
	uint8_t *p = out;
	uint32_t len = 0;

	pt_test_uart_receive(bytes, sizeof(bytes), 0);

	zassert_equal(read_serial_data(&g1_pt_uart_dev, &p, &len), 0, NULL);
	zassert_equal(len, 1, NULL);
	zassert_equal(out[0], 0x07, NULL);
}

ZTEST(production_test, test_serial_frame_at_event_offset_is_found)
{
	static const uint8_t frame[] = {0x5a, 0xa5, 0x7f, 0x01, 0x07, 0x86};
	uint8_t out[256];
	uint8_t *p = out;
	uint32_t len = 0;

	pt_test_uart_receive(frame, sizeof(frame), 100);

	zassert_equal(read_serial_data(&g1_pt_uart_dev, &p, &len), 0, NULL);
	zassert_equal(len, 1, NULL);
	zassert_equal(out[0], 0x07, NULL);
}

ZTEST(production_test, test_serial_frame_with_bad_checksum_is_dropped)
{
	static const uint8_t frame[] = {0x5a, 0xa5, 0x7f, 0x01, 0x07, 0x87};
	uint8_t out[256];
	uint8_t *p = out;
	uint32_t len = 0;

	pt_test_uart_receive(frame, sizeof(frame), 0);

	zassert_equal(read_serial_data(&g1_pt_uart_dev, &p, &len), -2, NULL);
}

ZTEST(production_test, test_serial_response_header_is_not_a_request)
{
	static const uint8_t frame[] = {0x5a, 0xa5, 0xff, 0x01, 0x07, 0x06};
	uint8_t out[256];
	uint8_t *p = out;
	uint32_t len = 0;

	pt_test_uart_receive(frame, sizeof(frame), 0);

	zassert_equal(read_serial_data(&g1_pt_uart_dev, &p, &len), -2, NULL);
}

ZTEST(production_test, test_serial_read_without_frame_returns_minus_2)
{
	uint8_t out[256];
	uint8_t *p = out;
	uint32_t len = 0;

	zassert_equal(read_serial_data(&g1_pt_uart_dev, &p, &len), -2, NULL);
}

ZTEST(production_test, test_serial_frame_is_consumed_by_read)
{
	static const uint8_t frame[] = {0x5a, 0xa5, 0x7f, 0x01, 0x07, 0x86};
	uint8_t out[256];
	uint8_t *p = out;
	uint32_t len = 0;

	pt_test_uart_receive(frame, sizeof(frame), 0);
	zassert_equal(read_serial_data(&g1_pt_uart_dev, &p, &len), 0, NULL);
	zassert_equal(read_serial_data(&g1_pt_uart_dev, &p, &len), -2, NULL);
}

ZTEST(production_test, test_serial_read_rejects_null_arguments)
{
	static const uint8_t frame[] = {0x5a, 0xa5, 0x7f, 0x01, 0x07, 0x86};
	uint8_t out[256];
	uint8_t *p = out;
	uint32_t len = 0;

	pt_test_uart_receive(frame, sizeof(frame), 0);

	zassert_equal(read_serial_data(NULL, &p, &len), -1, NULL);
	zassert_equal(read_serial_data(&g1_pt_uart_dev, NULL, &len), -1, NULL);
	zassert_equal(read_serial_data(&g1_pt_uart_dev, &p, NULL), -1, NULL);
	zassert_equal(read_serial_data(&g1_pt_uart_dev, &p, &len), 0, "frame still pending");
}

ZTEST(production_test, test_serial_read_of_invalid_buffer_returns_minus_3)
{
	uint8_t out[256];
	uint8_t *p = out;
	uint32_t len = 0;

	memset(serial_rx_buf, 0x5a, sizeof(serial_rx_buf));
	serial_rx_len = 6;

	zassert_equal(read_serial_data(&g1_pt_uart_dev, &p, &len), -3, NULL);
	zassert_equal(read_serial_data(&g1_pt_uart_dev, &p, &len), -2, NULL);
}

ZTEST(production_test, test_serial_rx_clear_drops_pending_frame)
{
	static const uint8_t frame[] = {0x5a, 0xa5, 0x7f, 0x01, 0x07, 0x86};
	uint8_t out[256];
	uint8_t *p = out;
	uint32_t len = 0;

	pt_test_uart_receive(frame, sizeof(frame), 0);
	serial_rx_clear();

	zassert_equal(read_serial_data(&g1_pt_uart_dev, &p, &len), -2, NULL);
}

ZTEST(production_test, test_serial_frame_of_255_bytes_is_received)
{
	uint8_t frame[255];
	uint8_t out[256];
	uint8_t *p = out;
	uint32_t len = 0;

	frame[0] = 0x5a;
	frame[1] = 0xa5;
	frame[2] = 0x7f;
	frame[3] = 250;
	for (int i = 0; i < 250; i++) {
		frame[4 + i] = (uint8_t)i;
	}
	frame[254] = pt_test_sum8(frame, 254);
	pt_test_uart_receive(frame, sizeof(frame), 0);

	zassert_equal(read_serial_data(&g1_pt_uart_dev, &p, &len), 0, NULL);
	zassert_equal(len, 250, NULL);
	zassert_mem_equal(out, &frame[4], 250, NULL);
}

/* DEFECT: the frame length n + 5 is a uint8_t, so 256 wraps to 0 and the frame is lost. */
ZTEST_EXPECT_FAIL(production_test, test_serial_frame_filling_the_whole_buffer_is_received);
ZTEST(production_test, test_serial_frame_filling_the_whole_buffer_is_received)
{
	uint8_t frame[256];
	uint8_t out[256];
	uint8_t *p = out;
	uint32_t len = 0;

	frame[0] = 0x5a;
	frame[1] = 0xa5;
	frame[2] = 0x7f;
	frame[3] = 251;
	for (int i = 0; i < 251; i++) {
		frame[4 + i] = (uint8_t)i;
	}
	frame[255] = pt_test_sum8(frame, 255);
	pt_test_uart_receive(frame, sizeof(frame), 0);

	zassert_equal(read_serial_data(&g1_pt_uart_dev, &p, &len), 0, NULL);
	zassert_equal(len, 251, NULL);
}

/* ---- lpuart_async_a/b() ---- */

ZTEST(production_test, test_uart_setup_registers_callback_and_enables_rx)
{
	zassert_equal(lpuart_async_a(&g1_pt_uart_dev), 0, NULL);

	zassert_equal(g1_pt_uart_callback_set_fake.call_count, 1, NULL);
	zassert_equal(g1_pt_uart_callback_set_fake.arg0_val, &g1_pt_uart_dev, NULL);
	zassert_equal(g1_pt_uart_callback_set_fake.arg1_val, uart_callback, NULL);
	zassert_equal(g1_pt_uart_callback_set_fake.arg2_val, (void *)&g1_pt_uart_dev, NULL);
	zassert_equal(g1_pt_uart_rx_enable_fake.call_count, 1, NULL);
	zassert_not_null(g1_pt_uart_rx_enable_fake.arg1_val, NULL);
	zassert_equal(g1_pt_uart_rx_enable_fake.arg2_val, 256, NULL);
	zassert_equal(lpuart_async_a(NULL), -1, NULL);
}

/* the stray 0x5a starts no valid header */
ZTEST(production_test, test_uart_write_sends_buffer_then_first_byte_again)
{
	static const uint8_t frame[] = {0x5a, 0xa5, 0xff, 0x01, 0x07, 0x06};

	zassert_equal(lpuart_async_b(&g1_pt_uart_dev, (uint8_t *)frame, sizeof(frame)), 0, NULL);

	zassert_equal(g1_pt_uart_tx_log.count, 2, NULL);
	zassert_equal(g1_pt_uart_tx_log.len[0], sizeof(frame), NULL);
	zassert_mem_equal(g1_pt_uart_tx_log.data[0], frame, sizeof(frame), NULL);
	zassert_equal(g1_pt_uart_tx_log.len[1], 1, NULL);
	zassert_equal(g1_pt_uart_tx_log.data[1][0], 0x5a, NULL);
}

ZTEST(production_test, test_uart_write_rejects_null_and_empty)
{
	uint8_t b = 0;

	zassert_equal(lpuart_async_b(NULL, &b, 1), -1, NULL);
	zassert_equal(lpuart_async_b(&g1_pt_uart_dev, NULL, 1), -1, NULL);
	zassert_equal(lpuart_async_b(&g1_pt_uart_dev, &b, 0), -1, NULL);
	zassert_equal(g1_pt_uart_tx_fake.call_count, 0, NULL);
}

ZTEST(production_test, test_uart_write_driver_error_returns_minus_2)
{
	uint8_t b = 0x5a;

	g1_pt_uart_tx_fake.custom_fake = NULL;
	g1_pt_uart_tx_fake.return_val = -EBUSY;

	zassert_equal(lpuart_async_b(&g1_pt_uart_dev, &b, 1), -2, NULL);
	zassert_equal(g1_pt_uart_poll_out_fake.call_count, 0, NULL);
}
