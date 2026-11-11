/* Expected bytes follow the NFC Forum NDEF 1.0 and WLC 2.0 layouts. */
#include <string.h>
#include <zephyr/ztest.h>

#include "harness.h"
#include "nfc_test.h"

#include "nfc/nfc_wlc.h"

ReturnCode ndefMessageDecode(const ndefConstBuffer *bufPayload, ndefMessage *message);

static void ndef_before(void *fixture)
{
	ARG_UNUSED(fixture);
	nfc_wlc_test_reset();
}

ZTEST_SUITE(nfc_ndef, NULL, NULL, ndef_before, NULL, NULL);

static uint8_t out[512];

static uint32_t encode(const ndefRecord *r)
{
	ndefBuffer b = {out, sizeof(out)};

	zassert_equal(ndefRecordEncode(r, &b), ERR_NONE);
	return b.length;
}

static void expect_bytes(const uint8_t *got, uint32_t n, const uint8_t *exp, uint32_t en)
{
	char g[3 * 64 + 4];
	char e[3 * 64 + 4];

	g1_hex_format(got, n, g, sizeof(g));
	g1_hex_format(exp, en, e, sizeof(e));
	zassert_true(n == en && memcmp(got, exp, n) == 0, "\n  got  %s\n  want %s", g, e);
}

static void record(ndefRecord *r, const char *type, const uint8_t *payload, uint32_t len)
{
	ndefConstBuffer8 t = {(const uint8_t *)type, (uint8_t)strlen(type)};
	ndefConstBuffer p = {payload, len};

	zassert_equal(ndefRecordReset(r), ERR_NONE);
	zassert_equal(ndefRecordSetType(r, NDEF_TNF_RTD_WELL_KNOWN_TYPE, &t), ERR_NONE);
	zassert_equal(ndefRecordSetPayload(r, &p), ERR_NONE);
}

/* ---- records ---- */

ZTEST(nfc_ndef, test_empty_record_is_mb_me_sr_tnf_empty_with_zero_lengths)
{
	static const uint8_t exp[] = {0xd0, 0x00, 0x00};
	ndefRecord r;

	zassert_equal(ndefRecordReset(&r), ERR_NONE);
	expect_bytes(out, encode(&r), exp, sizeof(exp));
}

ZTEST(nfc_ndef, test_short_record_has_a_1_byte_payload_length)
{
	static const uint8_t exp[] = {0xd1, 0x01, 0x03, 'T', 0xaa, 0xbb, 0xcc};
	ndefRecord r;

	record(&r, "T", (const uint8_t *)"\xaa\xbb\xcc", 3);
	zassert_equal(ndefRecordGetHeaderLength(&r), 4);
	zassert_equal(ndefRecordGetLength(&r), 7);
	expect_bytes(out, encode(&r), exp, sizeof(exp));
}

ZTEST(nfc_ndef, test_255_byte_payload_is_still_a_short_record)
{
	static uint8_t payload[255];
	ndefRecord r;

	record(&r, "T", payload, sizeof(payload));
	zassert_equal(encode(&r), 3 + 1 + 255);
	zassert_equal(out[0], 0xd1, "SR set");
	zassert_equal(out[2], 0xff);
}

ZTEST(nfc_ndef, test_256_byte_payload_needs_a_4_byte_big_endian_length)
{
	static uint8_t payload[300];
	static const uint8_t hdr[] = {0xc1, 0x01, 0x00, 0x00, 0x01, 0x2c, 'T'};
	ndefRecord r;

	record(&r, "T", payload, sizeof(payload));
	zassert_equal(ndefRecordGetHeaderLength(&r), 7);
	zassert_equal(encode(&r), 7 + 300);
	expect_bytes(out, 7, hdr, sizeof(hdr));

	record(&r, "T", payload, 256);
	zassert_equal(encode(&r), 7 + 256);
	zassert_equal(out[0], 0xc1, "SR clear");
	zassert_mem_equal(&out[2], "\x00\x00\x01\x00", 4);
}

ZTEST(nfc_ndef, test_id_sets_il_and_adds_the_id_length_and_the_id)
{
	static const uint8_t exp[] = {0xd9, 0x01, 0x02, 0x02, 'U', 'i', 'd', 0x10, 0x20};
	ndefConstBuffer8 id = {(const uint8_t *)"id", 2};
	ndefConstBuffer8 none = {NULL, 0};
	ndefRecord r;

	record(&r, "U", (const uint8_t *)"\x10\x20", 2);
	zassert_equal(ndefRecordSetId(&r, &id), ERR_NONE);
	zassert_equal(ndefRecordGetHeaderLength(&r), 4 + 1 + 2);
	expect_bytes(out, encode(&r), exp, sizeof(exp));

	zassert_equal(ndefRecordSetId(&r, &none), ERR_NONE);
	zassert_equal(r.header & 0x08, 0, "IL cleared with the id");
}

ZTEST(nfc_ndef, test_header_encoder_needs_7_bytes_of_room)
{
	uint8_t small[6];
	ndefBuffer b = {small, sizeof(small)};
	ndefRecord r;

	record(&r, "T", (const uint8_t *)"\x01", 1);
	zassert_equal(ndefRecordEncodeHeader(&r, &b), ERR_NOMEM);
	zassert_equal(b.length, NDEF_RECORD_HEADER_LEN);
}

ZTEST(nfc_ndef, test_record_encoder_reports_the_length_it_needs)
{
	uint8_t small[6];
	ndefBuffer b = {small, sizeof(small)};
	ndefRecord r;

	record(&r, "T", (const uint8_t *)"\xaa\xbb\xcc", 3);
	zassert_equal(ndefRecordEncode(&r, &b), ERR_NOMEM);
	zassert_equal(b.length, 7);
}

ZTEST(nfc_ndef, test_codec_rejects_null_arguments)
{
	ndefRecord r;
	ndefBuffer b = {NULL, 10};
	ndefConstBuffer cb = {NULL, 3};
	ndefConstBuffer8 bad8 = {NULL, 3};

	zassert_equal(ndefRecordReset(NULL), ERR_PARAM);
	zassert_equal(ndefRecordEncode(NULL, &b), ERR_PARAM);
	zassert_equal(ndefRecordReset(&r), ERR_NONE);
	zassert_equal(ndefRecordEncode(&r, &b), ERR_PARAM, "NULL buffer");
	zassert_equal(ndefRecordDecode(&cb, &r), ERR_PARAM);
	zassert_equal(ndefRecordSetType(&r, 1, &bad8), ERR_PARAM, "NULL with a length");
	zassert_equal(ndefRecordSetPayload(&r, &cb), ERR_PARAM);
}

/* ---- decoding ---- */

ZTEST(nfc_ndef, test_short_record_decodes_header_type_and_payload)
{
	static const uint8_t in[] = {0xd1, 0x01, 0x03, 'T', 0xaa, 0xbb, 0xcc};
	ndefConstBuffer b = {in, sizeof(in)};
	ndefRecord r;

	zassert_equal(ndefRecordDecode(&b, &r), ERR_NONE);
	zassert_equal(r.header, 0xd1);
	zassert_equal(ndefHeaderTNF(&r), NDEF_TNF_RTD_WELL_KNOWN_TYPE);
	zassert_true(ndefHeaderIsSetSR(&r));
	zassert_false(ndefHeaderIsSetIL(&r));
	zassert_equal(r.typeLength, 1);
	zassert_equal_ptr(r.type, &in[3]);
	zassert_equal(r.idLength, 0);
	zassert_equal(r.bufPayload.length, 3);
	zassert_equal_ptr(r.bufPayload.buffer, &in[4]);
}

ZTEST(nfc_ndef, test_standard_record_decodes_the_4_byte_length)
{
	static uint8_t in[7 + 300] = {0x81, 0x01, 0x00, 0x00, 0x01, 0x2c, 'T'};
	ndefConstBuffer b = {in, sizeof(in)};
	ndefRecord r;

	zassert_equal(ndefRecordDecode(&b, &r), ERR_NONE);
	zassert_equal(r.header & 0xc0, 0x80, "MB, not ME");
	zassert_equal(r.bufPayload.length, 300);
	zassert_equal_ptr(r.bufPayload.buffer, &in[7]);
}

ZTEST(nfc_ndef, test_id_length_and_id_are_decoded_when_il_is_set)
{
	static const uint8_t in[] = {0xd9, 0x01, 0x02, 0x02, 'U', 'i', 'd', 0x10, 0x20};
	ndefConstBuffer b = {in, sizeof(in)};
	ndefRecord r;

	zassert_equal(ndefRecordDecode(&b, &r), ERR_NONE);
	zassert_equal(r.idLength, 2);
	zassert_equal_ptr(r.id, &in[5]);
	zassert_equal_ptr(r.bufPayload.buffer, &in[7]);
}

ZTEST(nfc_ndef, test_truncated_record_is_a_protocol_error)
{
	static const uint8_t in[] = {0xd9, 0x01, 0x02, 0x02, 'U', 'i', 'd', 0x10, 0x20};
	static const uint8_t std[] = {0xc1, 0x01, 0x00, 0x00, 0x00, 0x01, 'T', 0x55};
	ndefRecord r;

	for (uint32_t n = 0; n < sizeof(in); n++) {
		ndefConstBuffer b = {in, n};

		zassert_equal(ndefRecordDecode(&b, &r), ERR_PROTO, "%u bytes", n);
	}
	for (uint32_t n = 2; n < sizeof(std); n++) {
		ndefConstBuffer b = {std, n};

		zassert_equal(ndefRecordDecode(&b, &r), ERR_PROTO, "standard, %u bytes", n);
	}
}

/* ---- messages ---- */

static ndefRecord rec[3];

static void three_records(ndefMessage *m)
{
	memset(m, 0, sizeof(*m));
	record(&rec[0], "T", (const uint8_t *)"\x01", 1);
	record(&rec[1], "U", (const uint8_t *)"\x02\x03", 2);
	record(&rec[2], "Sp", (const uint8_t *)"\x04\x05\x06", 3);
	for (int i = 0; i < 3; i++) {
		zassert_equal(ndefMessageAppend(m, &rec[i]), ERR_NONE);
	}
}

ZTEST(nfc_ndef, test_append_sets_mb_on_the_first_and_me_on_the_last_record)
{
	ndefMessage m;

	three_records(&m);
	zassert_equal(rec[0].header & 0xc0, 0x80, "first: MB");
	zassert_equal(rec[1].header & 0xc0, 0x00, "middle: neither");
	zassert_equal(rec[2].header & 0xc0, 0x40, "last: ME");
	zassert_equal(m.info.recordCount, 3);
	zassert_equal(m.info.length, 5 + 6 + 8);
	zassert_equal_ptr(ndefMessageGetFirstRecord(&m), &rec[0]);
	zassert_equal_ptr(rec[0].next, &rec[1]);
	zassert_is_null(rec[2].next);
}

ZTEST(nfc_ndef, test_single_record_message_has_mb_and_me)
{
	ndefMessage m = {0};
	ndefRecord r;

	record(&r, "T", (const uint8_t *)"\x01", 1);
	zassert_equal(ndefMessageAppend(&m, &r), ERR_NONE);
	zassert_equal(r.header & 0xc0, 0xc0);
}

ZTEST(nfc_ndef, test_message_encodes_the_records_back_to_back)
{
	static const uint8_t exp[] = {
		0x91, 0x01, 0x01, 'T', 0x01,
		0x11, 0x01, 0x02, 'U', 0x02, 0x03,
		0x51, 0x02, 0x03, 'S', 'p', 0x04, 0x05, 0x06,
	};
	ndefMessage m;
	ndefBuffer b = {out, sizeof(out)};

	three_records(&m);
	zassert_equal(ndefMessageEncode(&m, &b), ERR_NONE);
	expect_bytes(out, b.length, exp, sizeof(exp));
}

ZTEST(nfc_ndef, test_message_encoder_reports_the_length_it_needs)
{
	ndefMessage m;
	uint8_t small[10];
	ndefBuffer b = {small, sizeof(small)};

	three_records(&m);
	zassert_equal(ndefMessageEncode(&m, &b), ERR_NOMEM);
	zassert_equal(b.length, 19);
}

ZTEST(nfc_ndef, test_message_decode_returns_the_encoded_records)
{
	ndefMessage m;
	ndefMessage d;
	ndefBuffer b = {out, sizeof(out)};
	ndefConstBuffer cb;
	const ndefRecord *r;
	static const char *const types[] = {"T", "U", "Sp"};
	static const uint32_t lens[] = {1, 2, 3};
	int i = 0;

	three_records(&m);
	zassert_equal(ndefMessageEncode(&m, &b), ERR_NONE);
	cb.buffer = out;
	cb.length = b.length;

	zassert_equal(ndefMessageDecode(&cb, &d), ERR_NONE);
	zassert_equal(d.info.recordCount, 3);
	zassert_equal(d.info.length, b.length);
	for (r = ndefMessageGetFirstRecord(&d); r != NULL; r = ndefMessageGetNextRecord(r), i++) {
		zassert_true(i < 3);
		zassert_equal(r->typeLength, strlen(types[i]));
		zassert_mem_equal(r->type, types[i], r->typeLength);
		zassert_equal(r->bufPayload.length, lens[i]);
		zassert_equal(r->bufPayload.buffer[0], (uint8_t)(1 + (i == 0 ? 0 : i == 1 ? 1 : 3)));
	}
	zassert_equal(i, 3);
}

/* ---- WLC records ---- */

static ndefType type;
static ndefRecord wlc;

static uint32_t encode_cap(const ndefTypeRtdWlcCapability *cap)
{
	zassert_equal(ndefRtdWlcCapabilityInit(&type, cap), ERR_NONE);
	zassert_equal(ndefTypeToRecord(&type, &wlc), ERR_NONE);
	return encode(&wlc);
}

static uint32_t encode_stai(const ndefTypeRtdWlcStatusInfo *stai)
{
	zassert_equal(ndefRtdWlcStatusInfoInit(&type, stai), ERR_NONE);
	zassert_equal(ndefTypeToRecord(&type, &wlc), ERR_NONE);
	return encode(&wlc);
}

ZTEST(nfc_ndef, test_wlc_capability_record_is_wlccap_with_6_payload_bytes)
{
	static const ndefTypeRtdWlcCapability cap = {
		.wlcProtocolVersion = 0x20,
		.wlcConfigModeReq = 1,
		.wlcConfigWaitTimeRetry = 5,
		.wlcConfigNegoWait = 1,
		.wlcConfigRdConf = 0,
		.capWtIntRfu = 0,
		.capWtInt = 9,
		.ndefRdWt = 0x1e,
		.ndefWriteToInt = 3,
		.ndefWriteWtInt = 0x0a,
	};
	static const uint8_t exp[] = {
		0xd1, 0x06, 0x06, 'W', 'L', 'C', 'C', 'A', 'P',
		0x20,
		0x56,                      /* 01 0101 1 0: mode 1, retry 5, nego 1, rd 0 */
		0x09,
		0x1e, 0x03, 0x0a,
	};

	expect_bytes(out, encode_cap(&cap), exp, sizeof(exp));
}

ZTEST(nfc_ndef, test_wlc_capability_fields_are_packed_at_their_bit_positions)
{
	static const ndefTypeRtdWlcCapability all = {
		.wlcConfigModeReq = 3, .wlcConfigWaitTimeRetry = 0xf, .wlcConfigNegoWait = 1,
		.wlcConfigRdConf = 1, .capWtIntRfu = 7, .capWtInt = 0x1f,
	};
	static const ndefTypeRtdWlcCapability rd_only = {.wlcConfigRdConf = 1};
	static const ndefTypeRtdWlcCapability rfu_only = {.capWtIntRfu = 1};

	encode_cap(&all);
	zassert_equal(out[10], 0xff);
	zassert_equal(out[11], 0xff);
	encode_cap(&rd_only);
	zassert_equal(out[10], 0x01);
	encode_cap(&rfu_only);
	zassert_equal(out[11], 0x20, "RFU starts at b5");
}

ZTEST(nfc_ndef, test_wlc_capability_values_are_cut_to_their_field_width)
{
	static const ndefTypeRtdWlcCapability wide = {
		.wlcConfigModeReq = 4, .wlcConfigWaitTimeRetry = 0x10, .wlcConfigNegoWait = 2,
		.wlcConfigRdConf = 2, .capWtIntRfu = 8, .capWtInt = 0x20,
	};

	encode_cap(&wide);
	zassert_equal(out[10], 0x00, "config 0x%02x", out[10]);
	zassert_equal(out[11], 0x00, "cap wt int 0x%02x", out[11]);
}

ZTEST(nfc_ndef, test_wlc_status_record_carries_only_the_announced_fields)
{
	static const ndefTypeRtdWlcStatusInfo stai = {
		.controlByte1 = 0x05, /* battery level, receive voltage */
		.batteryLevel = 0x32,
		.receivePower = 0x11,
		.receiveVoltage = 0x22,
		.receiveCurrent = 0x33,
	};
	static const uint8_t exp[] = {
		0xd1, 0x07, 0x03, 'W', 'L', 'C', 'S', 'T', 'A', 'I', 0x05, 0x32, 0x22,
	};

	expect_bytes(out, encode_stai(&stai), exp, sizeof(exp));
}

ZTEST(nfc_ndef, test_wlc_status_fields_follow_the_control_byte_bit_order)
{
	static const ndefTypeRtdWlcStatusInfo stai = {
		.controlByte1 = 0xbf, /* all but RFU (b6) */
		.batteryLevel = 0x01,
		.receivePower = 0x02,
		.receiveVoltage = 0x03,
		.receiveCurrent = 0x04,
		.temperatureBattery = 0x05,
		.temperatureWlcl = 0x06,
		.rfu = 0xee,
		.controlByte2 = 0x07,
	};
	static const uint8_t exp[] = {
		0xd1, 0x07, 0x08, 'W', 'L', 'C', 'S', 'T', 'A', 'I',
		0xbf, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	};

	expect_bytes(out, encode_stai(&stai), exp, sizeof(exp));
}

ZTEST(nfc_ndef, test_wlc_status_without_fields_is_the_control_byte_alone)
{
	static const ndefTypeRtdWlcStatusInfo stai = {.controlByte1 = 0, .batteryLevel = 9};
	static const uint8_t exp[] = {0xd1, 0x07, 0x01, 'W', 'L', 'C', 'S', 'T', 'A', 'I', 0x00};

	expect_bytes(out, encode_stai(&stai), exp, sizeof(exp));
}

ZTEST(nfc_ndef, test_wlc_record_builders_check_the_type)
{
	static const ndefTypeRtdWlcCapability cap = {0};
	static const ndefTypeRtdWlcStatusInfo stai = {0};

	zassert_equal(ndefRtdWlcCapabilityInit(&type, &cap), ERR_NONE);
	zassert_equal(ndefRtdWlcStatusInfoToRecord(&type, &wlc), ERR_PARAM);
	zassert_equal(ndefRtdWlcStatusInfoInit(&type, &stai), ERR_NONE);
	zassert_equal(ndefRtdWlcCapabilityToRecord(&type, &wlc), ERR_PARAM);
	zassert_equal(ndefRtdWlcCapabilityInit(NULL, &cap), ERR_PARAM);
	zassert_equal(ndefRtdWlcStatusInfoInit(&type, NULL), ERR_PARAM);
	zassert_equal(ndefTypeToRecord(NULL, &wlc), ERR_PARAM);
}
