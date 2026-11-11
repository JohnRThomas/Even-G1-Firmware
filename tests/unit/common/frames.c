/*
 * Packets: what the phone sends (g1_send*()), the replies captured from ble_context.write, hex
 * text, the protocol vectors (tests/unit/vectors) and the byte checks behind the expect_*() macros.
 */
#include <errno.h>

#include "harness.h"
#include "ble/ble_proto.h"

/* NULL unless the suite links ble/ble_proto.c */
#pragma weak ble_handle_rx_data

struct g1_frame {
  uint16_t len;
  uint16_t sent; /* length passed to write() */
  uint8_t data[G1_FRAME_SIZE];
};

static struct g1_frame frames[G1_FRAME_MAX];
static int frame_head;
static int frame_count;
static int frames_dropped;
int g1_write_ret;

int g1_frame_capture(const uint8_t *data, uint16_t len)
{
  struct g1_frame *frame;

  if (frame_count == G1_FRAME_MAX) {
    frames_dropped++;
    return g1_write_ret;
  }
  frame = &frames[(frame_head + frame_count) % G1_FRAME_MAX];
  frame->sent = len;
  frame->len = MIN(len, G1_FRAME_SIZE);
  memcpy(frame->data, data, frame->len);
  frame_count++;
  return g1_write_ret;
}

int g1_frame_count(void)
{
  return frame_count;
}

int g1_frame_pop(uint8_t *buf, size_t cap)
{
  struct g1_frame *frame;

  if (frame_count == 0) {
    return -ENOMSG;
  }
  frame = &frames[frame_head];
  memcpy(buf, frame->data, MIN(cap, frame->len));
  frame_head = (frame_head + 1) % G1_FRAME_MAX;
  frame_count--;
  return frame->len;
}

const uint8_t *g1_frame_peek(int index, size_t *len)
{
  struct g1_frame *frame;

  if (index < 0 || index >= frame_count) {
    return NULL;
  }
  frame = &frames[(frame_head + index) % G1_FRAME_MAX];
  if (len) {
    *len = frame->len;
  }
  return frame->data;
}

void g1_frames_clear(void)
{
  frame_head = 0;
  frame_count = 0;
  frames_dropped = 0;
}

static int hex_value(char digit)
{
  if (digit >= '0' && digit <= '9') {
    return digit - '0';
  }
  if (digit >= 'a' && digit <= 'f') {
    return digit - 'a' + 10;
  }
  if (digit >= 'A' && digit <= 'F') {
    return digit - 'A' + 10;
  }
  return -1;
}

static int hex_parse_range(const char *text, const char *end, uint8_t *buf, size_t cap)
{
  size_t count = 0;

  while (text < end) {
    if (*text == '#') {
      while (text < end && *text != '\n') {
        text++;
      }
      continue;
    }
    if (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n' ||
        *text == ',' || *text == ':') {
      text++;
      continue;
    }
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X') &&
        hex_value(text[2]) >= 0 && hex_value(text[3]) >= 0) {
      text += 2;
    }
    if (hex_value(text[0]) < 0 || hex_value(text[1]) < 0) {
      return -EINVAL;
    }
    if (count == cap) {
      return -ENOSPC;
    }
    buf[count++] = (uint8_t)(hex_value(text[0]) << 4 | hex_value(text[1]));
    text += 2;
  }
  return (int)count;
}

int g1_hex_parse(const char *text, uint8_t *buf, size_t cap)
{
  return hex_parse_range(text, text + strlen(text), buf, cap);
}

int g1_hex(const char *hex, uint8_t *buf, size_t cap)
{
  int len = g1_hex_parse(hex, buf, cap);

  zassert_true(len >= 0, "bad hex \"%s\" (%d)", hex, len);
  return len;
}

void g1_hex_format(const uint8_t *buf, size_t len, char *out, size_t out_size)
{
  size_t used = 0;

  if (out_size == 0) {
    return;
  }
  out[0] = '\0';
  for (size_t index = 0; index < len; index++) {
    if (used + 8 > out_size) {
      snprintf(out + used, out_size - used, "...");
      return;
    }
    used += snprintf(out + used, out_size - used, index ? " %02x" : "%02x", buf[index]);
  }
}

extern const struct g1_unit_file g1_vectors[];

static const char *next_line(const char *text)
{
  const char *newline = strchr(text, '\n');

  return newline ? newline + 1 : text + strlen(text);
}

/* True if the line is the title "# <tag>" */
static bool is_vector_title(const char *line, const char *tag, size_t len)
{
  if (*line++ != '#') {
    return false;
  }
  while (*line == ' ' || *line == '\t') {
    line++;
  }
  if (strncmp(line, tag, len) != 0) {
    return false;
  }
  line += len;
  while (*line == ' ' || *line == '\t' || *line == '\r') {
    line++;
  }
  return *line == '\n' || *line == '\0';
}

/*
 * "0x<id>_<tag>" is the packet titled "# <tag>" whose first byte is <id>. A packet is the data
 * lines between its title and the next comment line.
 */
int g1_vector_load(const char *name, uint8_t *buf, size_t cap)
{
  if (name[0] != '0' || name[1] != 'x' || hex_value(name[2]) < 0 || hex_value(name[3]) < 0 ||
      name[4] != '_' || name[5] == '\0') {
    return -ENOENT;
  }
  int id = hex_value(name[2]) << 4 | hex_value(name[3]);
  const char *tag = name + 5;
  size_t len = strlen(tag);

  for (const struct g1_unit_file *file = g1_vectors; file->name != NULL; file++) {
    for (const char *line = (const char *)file->data; *line; line = next_line(line)) {
      const char *data = next_line(line);
      const char *end = data;
      uint8_t first;
      int count;

      if (!is_vector_title(line, tag, len)) {
        continue;
      }
      while (*end && *end != '#') {
        end = next_line(end);
      }
      count = hex_parse_range(data, end, &first, 1);
      if ((count == 1 || count == -ENOSPC) && first == id) {
        return hex_parse_range(data, end, buf, cap);
      }
    }
  }
  return -ENOENT;
}

int g1_vector(const char *name, uint8_t *buf, size_t cap)
{
  int len = g1_vector_load(name, buf, cap);

  zassert_true(len > 0, "vector %s: %d (tests/unit/vectors/*.hex)", name, len);
  return len;
}

/* longer than any test buffer (the fake flash is 8 MiB): a larger length is a bug */
#define LENGTH_MAX 0x1000000

/*
 * A mismatch report shows at most REPORT_BYTES of each buffer; when the buffers are longer, both
 * are shown from REPORT_LEAD bytes before the first difference.
 */
#define REPORT_BYTES 128
#define REPORT_LEAD  16

/* len is a length up to max; else msg tells a negative length (an error code) from a long one */
static bool length_ok(const char *what, size_t len, size_t max, char *msg, size_t msg_size)
{
  if (len <= max) {
    return true;
  }
  if ((long)len < 0) {
    snprintf(msg, msg_size, "%s length %ld is negative", what, (long)len);
  } else {
    snprintf(msg, msg_size, "%s length %lu is more than %lu", what, (unsigned long)len,
       (unsigned long)max);
  }
  return false;
}

/* the hex of buf[start..len), cut after REPORT_BYTES; "from byte N:" first when start > 0 */
static void report_window(const uint8_t *buf, size_t len, size_t start, char *out, size_t out_size)
{
  size_t shown = MIN(len - start, REPORT_BYTES);
  size_t used = 0;

  if (start > 0) {
    used = snprintf(out, out_size, "from byte %u: ", (unsigned int)start);
  }
  g1_hex_format(buf + start, shown, out + used, out_size - used);
  if (start + shown < len) {
    used = strlen(out);
    snprintf(out + used, out_size - used, " ...");
  }
}

bool g1_bytes_check(const uint8_t *got, size_t got_len, const uint8_t *want, size_t want_len,
        char *msg, size_t msg_size)
{
  static char want_text[G1_MSG_SIZE / 2 - 64];
  static char got_text[G1_MSG_SIZE / 2 - 64];
  size_t first_diff = 0;
  size_t start = 0;

  if (!length_ok("got", got_len, LENGTH_MAX, msg, msg_size) ||
      !length_ok("expected", want_len, LENGTH_MAX, msg, msg_size)) {
    return false;
  }
  if (got_len == want_len && memcmp(got, want, want_len) == 0) {
    return true;
  }
  while (first_diff < MIN(got_len, want_len) && got[first_diff] == want[first_diff]) {
    first_diff++;
  }
  if (MAX(got_len, want_len) > REPORT_BYTES && first_diff > REPORT_LEAD) {
    start = first_diff - REPORT_LEAD;
  }
  report_window(want, want_len, start, want_text, sizeof(want_text));
  report_window(got, got_len, start, got_text, sizeof(got_text));
  snprintf(msg, msg_size,
     "differs at byte %u\n  expected (%u bytes) %s\n  got      (%u bytes) %s",
     (unsigned int)first_diff, (unsigned int)want_len, want_text, (unsigned int)got_len,
     got_text);
  return false;
}

bool g1_hex_check(const void *got, size_t len, const char *hex, char *msg, size_t msg_size)
{
  static uint8_t want[G1_FRAME_SIZE];
  int want_len = g1_hex_parse(hex, want, sizeof(want));

  if (want_len < 0) {
    snprintf(msg, msg_size, "bad expected hex \"%s\" (%d)", hex, want_len);
    return false;
  }
  /* no hex is longer, so a longer got cannot match */
  if (!length_ok("got", len, sizeof(want), msg, msg_size)) {
    return false;
  }
  return g1_bytes_check(got, len, want, want_len, msg, msg_size);
}

bool g1_fill_check(const void *buf, size_t len, uint8_t value, const char *what, char *msg,
       size_t msg_size)
{
  const uint8_t *bytes = buf;

  if (!length_ok(what, len, LENGTH_MAX, msg, msg_size)) {
    return false;
  }
  for (size_t offset = 0; offset < len; offset++) {
    if (bytes[offset] != value) {
      snprintf(msg, msg_size, "%s byte %u is 0x%02x, expected 0x%02x", what,
         (unsigned int)offset, bytes[offset], value);
      return false;
    }
  }
  return true;
}

bool g1_frame_check(const char *hex, int pad_to, char *msg, size_t msg_size)
{
  static uint8_t want[G1_FRAME_SIZE];
  int len = g1_hex_parse(hex, want, sizeof(want));

  if (len < 0) {
    snprintf(msg, msg_size, "bad expected hex \"%s\" (%d)", hex, len);
    return false;
  }
  if (pad_to >= 0) {
    if (len > pad_to || pad_to > (int)sizeof(want)) {
      snprintf(msg, msg_size,
         "expected hex has %d bytes, more than the padded %d", len, pad_to);
      return false;
    }
    memset(want + len, 0, pad_to - len);
    len = pad_to;
  }
  return g1_frame_check_bytes(want, len, msg, msg_size);
}

bool g1_frame_check_bytes(const uint8_t *want, size_t len, char *msg, size_t msg_size)
{
  static char want_text[G1_MSG_SIZE / 2 - 64];
  static char diff[G1_MSG_SIZE];
  uint8_t got[G1_FRAME_SIZE];
  int got_len;

  if (!length_ok("expected", len, G1_FRAME_SIZE, msg, msg_size)) {
    return false;
  }
  g1_hex_format(want, len, want_text, sizeof(want_text));
  if (frames_dropped) {
    snprintf(msg, msg_size, "frame queue overflowed (%d frames dropped) before %s",
       frames_dropped, want_text);
    return false;
  }
  if (frame_count == 0) {
    snprintf(msg, msg_size, "no frame sent; expected (%u bytes) %s", (unsigned int)len,
       want_text);
    return false;
  }
  if (frames[frame_head].sent > G1_FRAME_SIZE) {
    snprintf(msg, msg_size, "frame of %u bytes is longer than G1_FRAME_SIZE",
       frames[frame_head].sent);
    return false;
  }
  got_len = g1_frame_pop(got, sizeof(got));
  if (g1_bytes_check(got, got_len, want, len, diff, sizeof(diff))) {
    return true;
  }
  snprintf(msg, msg_size, "frame %s", diff);
  return false;
}

bool g1_frame_check_none(char *msg, size_t msg_size)
{
  char text[256];
  size_t len;
  const uint8_t *frame;

  if (frame_count == 0 && frames_dropped == 0) {
    return true;
  }
  frame = g1_frame_peek(0, &len);
  if (frame == NULL) {
    snprintf(msg, msg_size, "expected no frame, %d were dropped", frames_dropped);
    return false;
  }
  g1_hex_format(frame, len, text, sizeof(text));
  snprintf(msg, msg_size, "expected no frame, %d queued; first (%u bytes) %s", frame_count,
     (unsigned int)len, text);
  return false;
}

void g1_pkt_log_add(struct g1_pkt_log *log, const void *data, size_t len)
{
  if (log->count == G1_PKT_LOG_MAX) {
    log->dropped++;
    return;
  }
  log->len[log->count] = len;
  memcpy(log->data[log->count], data, MIN(len, G1_PKT_LOG_SIZE));
  log->count++;
}

void g1_pkt_log_clear(struct g1_pkt_log *log)
{
  log->count = 0;
  log->dropped = 0;
}

bool g1_pkt_check(const struct g1_pkt_log *log, int index, const char *hex, char *msg,
      size_t msg_size)
{
  static char diff[G1_MSG_SIZE];

  if (index < 0 || index >= log->count) {
    snprintf(msg, msg_size, "no packet %d (%d logged, %d dropped)", index, log->count,
       log->dropped);
    return false;
  }
  if (log->len[index] > G1_PKT_LOG_SIZE) {
    snprintf(msg, msg_size, "packet %d has %u bytes; the log keeps %d", index,
       (unsigned int)log->len[index], G1_PKT_LOG_SIZE);
    return false;
  }
  if (g1_hex_check(log->data[index], log->len[index], hex, diff, sizeof(diff))) {
    return true;
  }
  snprintf(msg, msg_size, "packet %d %s", index, diff);
  return false;
}

void g1_send(const uint8_t *buf, size_t len)
{
  struct ble_context *ctx = g1_ble();

  zassert_not_null(ble_handle_rx_data, "g1_send() needs ble/ble_proto.c in FIRMWARE");
  /* As ble_work_thread(): short packets reuse rx_bufs[2] without clearing it. */
  zassert_true(len <= sizeof(ctx->rx_data), "packet of %u bytes: NUS rx holds %u",
         (unsigned int)len, (unsigned int)sizeof(ctx->rx_data));
  if (len > 20) {
    memcpy(ctx->rx_data, buf, len);
    ctx->rx_len = len;
    ble_handle_rx_data(ctx->rx_data, len, 0);
    memset(ctx->rx_data, 0, sizeof(ctx->rx_data));
  } else {
    memcpy(ctx->rx_bufs[2], buf, len);
    ctx->rx_pending = 1;
    ctx->small_len = len;
    ble_handle_rx_data(ctx->rx_bufs[2], len, 0);
  }
  ctx->rx_len = 0;
  ctx->rx_pending = 0;
}

void g1_send_hex(const char *hex)
{
  uint8_t buf[256];

  g1_send(buf, g1_hex(hex, buf, sizeof(buf)));
}

void g1_send_vector(const char *name)
{
  uint8_t buf[256];

  g1_send(buf, g1_vector(name, buf, sizeof(buf)));
}

int g1_send_vector_with(const char *name, int offset, uint8_t value)
{
  uint8_t buf[256];
  int len = g1_vector(name, buf, sizeof(buf));

  zassert_true(offset < len, "vector %s: %d bytes, patching byte %d", name, len, offset);
  buf[offset] = value;
  g1_send(buf, len);
  return len;
}

size_t g1_put_chunk(uint8_t *pkt, uint8_t cmd, uint8_t seq, uint8_t sub, uint16_t total,
        uint16_t index, const void *payload, size_t payload_len)
{
  size_t len = G1_CHUNK_HDR_LEN + payload_len;

  memmove(&pkt[G1_CHUNK_HDR_LEN], payload, payload_len);
  pkt[0] = cmd;
  sys_put_le16(len, &pkt[1]);
  pkt[3] = seq;
  pkt[4] = sub;
  sys_put_le16(total, &pkt[5]);
  sys_put_le16(index, &pkt[7]);
  return len;
}

void g1_fill_pattern(uint8_t *buf, size_t len, size_t start)
{
  for (size_t offset = 0; offset < len; offset++) {
    buf[offset] = (start + offset) & 0xff;
  }
}

uint8_t g1_sum8(const uint8_t *buf, size_t len)
{
  uint8_t sum = 0;

  for (size_t offset = 0; offset < len; offset++) {
    sum += buf[offset];
  }
  return sum;
}
