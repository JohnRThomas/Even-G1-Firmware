/*
 * nfc/nfc_wlc.c: the NDEF record codec (nfc_ndef) and the NDEF file of the Type 5 tag (nfc_wlc).
 * Expected bytes follow the NFC Forum NDEF 1.0, T5T and WLC 2.0 layouts.
 */
#include <zephyr/drivers/adc/adc_emul.h>

#include "harness.h"
#include "st25dv_emul.h"

#include "nfc/nfc_wlc.c"

/* before every nfc test: nfc_wlc.c as at boot, the tag driver bound, 0 mV on the field pin */
static void nfc_wlc_reset(bool keep_media)
{
  ARG_UNUSED(keep_media);
  memset(&wlc_listener, 0, sizeof(wlc_listener));
  wlc_listener.state = 1;
  wlc_info_sent = 1;
  memset(&nfc_charge, 0, sizeof(nfc_charge));
  nfc_tick_start = 0;
  nfc_field_mv = 0;
  memset(&CCFileStruct, 0, sizeof(CCFileStruct));
  nfc_field_tick = 0;
  wlcCapabilityItem = 0;
  wlcStatusInfoItem = 0;
  wlc_wait_ms = 0;
  nfc_charge_tick = 0;
  wlc_tag_configured = 0;
  ndefRecordPoolIndex = 0;
  nfc_tag_type_ok = 0;
  nfc_field_on = 0;
  wlcCapabilityTemp = 0;
  wlc_field_present = 0;
  nfc_low_field = 0;
  wlc_last_battery = 0;
  global_st25dv_context_ptr = eeprom_st25dv_init();
  adc_nfc_init();
  /* adc0 channel 3 */
  zassert_ok(adc_emul_const_value_set(DEVICE_DT_GET(DT_NODELABEL(adc0)), 3, 0));
}

G1_UNIT_RESET_HOOK(nfc_wlc_reset);

ZTEST_SUITE(nfc_ndef, NULL, NULL, NULL, NULL, NULL);

static uint8_t out[512];

static uint32_t encode(const ndefRecord *rec)
{
  ndefBuffer buf = {out, sizeof(out)};

  zassert_equal(ndefRecordEncode(rec, &buf), ERR_NONE);
  return buf.length;
}

/* a well-known type record */
static void record(ndefRecord *rec, const char *type, const uint8_t *payload, uint32_t len)
{
  ndefConstBuffer8 type_buf = {(const uint8_t *)type, (uint8_t)strlen(type)};
  ndefConstBuffer payload_buf = {payload, len};

  zassert_equal(ndefRecordReset(rec), ERR_NONE);
  zassert_equal(ndefRecordSetType(rec, NDEF_TNF_RTD_WELL_KNOWN_TYPE, &type_buf), ERR_NONE);
  zassert_equal(ndefRecordSetPayload(rec, &payload_buf), ERR_NONE);
}

/* ---- records ---- */

ZTEST(nfc_ndef, test_empty_record_is_mb_me_sr_tnf_empty_with_zero_lengths)
{
  ndefRecord rec;

  zassert_equal(ndefRecordReset(&rec), ERR_NONE);
  expect_bytes(out, encode(&rec), "d0 00 00");
}

ZTEST(nfc_ndef, test_short_record_has_a_1_byte_payload_length)
{
  ndefRecord rec;

  record(&rec, "T", (const uint8_t *)"\xaa\xbb\xcc", 3);
  zassert_equal(ndefRecordGetHeaderLength(&rec), 4);
  zassert_equal(ndefRecordGetLength(&rec), 7);
  expect_bytes(out, encode(&rec), "d1 01 03 54 aa bb cc"); /* type "T" */
}

ZTEST(nfc_ndef, test_255_byte_payload_is_still_a_short_record)
{
  static uint8_t payload[255];
  ndefRecord rec;

  record(&rec, "T", payload, sizeof(payload));
  zassert_equal(encode(&rec), 3 + 1 + 255);
  zassert_equal(out[0], 0xd1, "SR set");
  zassert_equal(out[2], 0xff);
}

ZTEST(nfc_ndef, test_256_byte_payload_needs_a_4_byte_big_endian_length)
{
  static uint8_t payload[300];
  ndefRecord rec;

  record(&rec, "T", payload, sizeof(payload));
  zassert_equal(ndefRecordGetHeaderLength(&rec), 7);
  zassert_equal(encode(&rec), 7 + 300);
  expect_bytes(out, 7, "c1 01 00 00 01 2c 54");

  record(&rec, "T", payload, 256);
  zassert_equal(encode(&rec), 7 + 256);
  zassert_equal(out[0], 0xc1, "SR clear");
  zassert_mem_equal(&out[2], "\x00\x00\x01\x00", 4);
}

ZTEST(nfc_ndef, test_id_sets_il_and_adds_the_id_length_and_the_id)
{
  ndefConstBuffer8 id = {(const uint8_t *)"id", 2};
  ndefConstBuffer8 none = {NULL, 0};
  ndefRecord rec;

  record(&rec, "U", (const uint8_t *)"\x10\x20", 2);
  zassert_equal(ndefRecordSetId(&rec, &id), ERR_NONE);
  zassert_equal(ndefRecordGetHeaderLength(&rec), 4 + 1 + 2);
  expect_bytes(out, encode(&rec), "d9 01 02 02 55 69 64 10 20"); /* type "U", id "id" */

  zassert_equal(ndefRecordSetId(&rec, &none), ERR_NONE);
  zassert_equal(rec.header & 0x08, 0, "IL cleared with the id");
}

ZTEST(nfc_ndef, test_header_encoder_needs_7_bytes_of_room)
{
  uint8_t small[6];
  ndefBuffer buf = {small, sizeof(small)};
  ndefRecord rec;

  record(&rec, "T", (const uint8_t *)"\x01", 1);
  zassert_equal(ndefRecordEncodeHeader(&rec, &buf), ERR_NOMEM);
  zassert_equal(buf.length, NDEF_RECORD_HEADER_LEN);
}

ZTEST(nfc_ndef, test_record_encoder_reports_the_length_it_needs)
{
  uint8_t small[6];
  ndefBuffer buf = {small, sizeof(small)};
  ndefRecord rec;

  record(&rec, "T", (const uint8_t *)"\xaa\xbb\xcc", 3);
  zassert_equal(ndefRecordEncode(&rec, &buf), ERR_NOMEM);
  zassert_equal(buf.length, 7);
}

ZTEST(nfc_ndef, test_codec_rejects_null_arguments)
{
  ndefRecord rec;
  ndefBuffer buf = {NULL, 10};
  ndefConstBuffer const_buf = {NULL, 3};
  ndefConstBuffer8 bad8 = {NULL, 3};

  zassert_equal(ndefRecordReset(NULL), ERR_PARAM);
  zassert_equal(ndefRecordEncode(NULL, &buf), ERR_PARAM);
  zassert_equal(ndefRecordReset(&rec), ERR_NONE);
  zassert_equal(ndefRecordEncode(&rec, &buf), ERR_PARAM, "NULL buffer");
  zassert_equal(ndefRecordDecode(&const_buf, &rec), ERR_PARAM);
  zassert_equal(ndefRecordSetType(&rec, 1, &bad8), ERR_PARAM, "NULL with a length");
  zassert_equal(ndefRecordSetPayload(&rec, &const_buf), ERR_PARAM);
}

/* ---- decoding ---- */

ZTEST(nfc_ndef, test_short_record_decodes_header_type_and_payload)
{
  static const uint8_t in[] = {0xd1, 0x01, 0x03, 'T', 0xaa, 0xbb, 0xcc};
  ndefConstBuffer buf = {in, sizeof(in)};
  ndefRecord rec;

  zassert_equal(ndefRecordDecode(&buf, &rec), ERR_NONE);
  zassert_equal(rec.header, 0xd1);
  zassert_equal(ndefHeaderTNF(&rec), NDEF_TNF_RTD_WELL_KNOWN_TYPE);
  zassert_true(ndefHeaderIsSetSR(&rec));
  zassert_false(ndefHeaderIsSetIL(&rec));
  zassert_equal(rec.typeLength, 1);
  zassert_equal_ptr(rec.type, &in[3]);
  zassert_equal(rec.idLength, 0);
  zassert_equal(rec.bufPayload.length, 3);
  zassert_equal_ptr(rec.bufPayload.buffer, &in[4]);
}

ZTEST(nfc_ndef, test_standard_record_decodes_the_4_byte_length)
{
  static uint8_t in[7 + 300] = {0x81, 0x01, 0x00, 0x00, 0x01, 0x2c, 'T'};
  ndefConstBuffer buf = {in, sizeof(in)};
  ndefRecord rec;

  zassert_equal(ndefRecordDecode(&buf, &rec), ERR_NONE);
  zassert_equal(rec.header & 0xc0, 0x80, "MB, not ME");
  zassert_equal(rec.bufPayload.length, 300);
  zassert_equal_ptr(rec.bufPayload.buffer, &in[7]);
}

ZTEST(nfc_ndef, test_id_length_and_id_are_decoded_when_il_is_set)
{
  static const uint8_t in[] = {0xd9, 0x01, 0x02, 0x02, 'U', 'i', 'd', 0x10, 0x20};
  ndefConstBuffer buf = {in, sizeof(in)};
  ndefRecord rec;

  zassert_equal(ndefRecordDecode(&buf, &rec), ERR_NONE);
  zassert_equal(rec.idLength, 2);
  zassert_equal_ptr(rec.id, &in[5]);
  zassert_equal_ptr(rec.bufPayload.buffer, &in[7]);
}

ZTEST(nfc_ndef, test_truncated_record_is_a_protocol_error)
{
  static const uint8_t in[] = {0xd9, 0x01, 0x02, 0x02, 'U', 'i', 'd', 0x10, 0x20};
  static const uint8_t std[] = {0xc1, 0x01, 0x00, 0x00, 0x00, 0x01, 'T', 0x55};
  ndefRecord rec;

  for (uint32_t len = 0; len < sizeof(in); len++) {
    ndefConstBuffer buf = {in, len};

    zassert_equal(ndefRecordDecode(&buf, &rec), ERR_PROTO, "%u bytes", len);
  }
  for (uint32_t len = 2; len < sizeof(std); len++) {
    ndefConstBuffer buf = {std, len};

    zassert_equal(ndefRecordDecode(&buf, &rec), ERR_PROTO, "standard, %u bytes", len);
  }
}

/* ---- messages ---- */

static ndefRecord recs[3];

static void three_records(ndefMessage *msg)
{
  memset(msg, 0, sizeof(*msg));
  record(&recs[0], "T", (const uint8_t *)"\x01", 1);
  record(&recs[1], "U", (const uint8_t *)"\x02\x03", 2);
  record(&recs[2], "Sp", (const uint8_t *)"\x04\x05\x06", 3);
  for (int index = 0; index < 3; index++) {
    zassert_equal(ndefMessageAppend(msg, &recs[index]), ERR_NONE);
  }
}

ZTEST(nfc_ndef, test_append_sets_mb_on_the_first_and_me_on_the_last_record)
{
  ndefMessage msg;

  three_records(&msg);
  zassert_equal(recs[0].header & 0xc0, 0x80, "first: MB");
  zassert_equal(recs[1].header & 0xc0, 0x00, "middle: neither");
  zassert_equal(recs[2].header & 0xc0, 0x40, "last: ME");
  zassert_equal(msg.info.recordCount, 3);
  zassert_equal(msg.info.length, 5 + 6 + 8);
  zassert_equal_ptr(ndefMessageGetFirstRecord(&msg), &recs[0]);
  zassert_equal_ptr(recs[0].next, &recs[1]);
  zassert_is_null(recs[2].next);
}

ZTEST(nfc_ndef, test_single_record_message_has_mb_and_me)
{
  ndefMessage msg = {0};
  ndefRecord rec;

  record(&rec, "T", (const uint8_t *)"\x01", 1);
  zassert_equal(ndefMessageAppend(&msg, &rec), ERR_NONE);
  zassert_equal(rec.header & 0xc0, 0xc0);
}

ZTEST(nfc_ndef, test_message_encodes_the_records_back_to_back)
{
  ndefMessage msg;
  ndefBuffer buf = {out, sizeof(out)};

  three_records(&msg);
  zassert_equal(ndefMessageEncode(&msg, &buf), ERR_NONE);
  /* "T" 01, "U" 02 03, "Sp" 04 05 06 */
  expect_bytes(out, buf.length,
         "91 01 01 54 01 11 01 02 55 02 03 51 02 03 53 70 04 05 06");
}

ZTEST(nfc_ndef, test_message_encoder_reports_the_length_it_needs)
{
  ndefMessage msg;
  uint8_t small[10];
  ndefBuffer buf = {small, sizeof(small)};

  three_records(&msg);
  zassert_equal(ndefMessageEncode(&msg, &buf), ERR_NOMEM);
  zassert_equal(buf.length, 19);
}

ZTEST(nfc_ndef, test_message_decode_returns_the_encoded_records)
{
  static const struct {
    const char *type;
    uint32_t len;
    uint8_t first;
  } want[] = {{"T", 1, 0x01}, {"U", 2, 0x02}, {"Sp", 3, 0x04}};
  ndefMessage msg;
  ndefMessage decoded;
  ndefBuffer buf = {out, sizeof(out)};
  ndefConstBuffer encoded;
  const ndefRecord *rec;
  int index = 0;

  three_records(&msg);
  zassert_equal(ndefMessageEncode(&msg, &buf), ERR_NONE);
  encoded.buffer = out;
  encoded.length = buf.length;

  zassert_equal(ndefMessageDecode(&encoded, &decoded), ERR_NONE);
  zassert_equal(decoded.info.recordCount, 3);
  zassert_equal(decoded.info.length, buf.length);
  for (rec = ndefMessageGetFirstRecord(&decoded); rec != NULL;
       rec = ndefMessageGetNextRecord(rec), index++) {
    zassert_true(index < 3);
    zassert_equal(rec->typeLength, strlen(want[index].type));
    zassert_mem_equal(rec->type, want[index].type, rec->typeLength);
    zassert_equal(rec->bufPayload.length, want[index].len);
    zassert_equal(rec->bufPayload.buffer[0], want[index].first);
  }
  zassert_equal(index, 3);
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

  /* "WLCCAP"; 56h = 01 0101 1 0: mode 1, retry 5, nego 1, rd 0 */
  expect_bytes(out, encode_cap(&cap), "d1 06 06 57 4c 43 43 41 50 20 56 09 1e 03 0a");
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
    .controlByte1 = 0x05, .batteryLevel = 0x32, .receivePower = 0x11,
    .receiveVoltage = 0x22, .receiveCurrent = 0x33,
  };

  /* "WLCSTAI"; control byte 05h: battery level and receive voltage */
  expect_bytes(out, encode_stai(&stai), "d1 07 03 57 4c 43 53 54 41 49 05 32 22");
}

ZTEST(nfc_ndef, test_wlc_status_fields_follow_the_control_byte_bit_order)
{
  static const ndefTypeRtdWlcStatusInfo stai = {
    .controlByte1 = 0xbf, .batteryLevel = 0x01, .receivePower = 0x02,
    .receiveVoltage = 0x03, .receiveCurrent = 0x04, .temperatureBattery = 0x05,
    .temperatureWlcl = 0x06, .rfu = 0xee, .controlByte2 = 0x07,
  };

  /* control byte bfh: all but RFU (b6) */
  expect_bytes(out, encode_stai(&stai),
         "d1 07 08 57 4c 43 53 54 41 49 bf 01 02 03 04 05 06 07");
}

ZTEST(nfc_ndef, test_wlc_status_without_fields_is_the_control_byte_alone)
{
  static const ndefTypeRtdWlcStatusInfo stai = {.controlByte1 = 0, .batteryLevel = 9};

  expect_bytes(out, encode_stai(&stai), "d1 07 01 57 4c 43 53 54 41 49 00");
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

/* ---- Type 5 tag (nfc_wlc) ---- */

/* the tag's user memory from address 0 holds the bytes of hex */
static void tag_holds(const char *hex)
{
  g1_hex(hex, st25dv_emul_user_mem(), ST25DV_EMUL_USER_MAX);
}

ZTEST(nfc_wlc, test_tag_size_is_blocks_times_block_size)
{
  zassert_equal(BSP_NFCTAG_GetByteSize(), 512, "ST25DV04KC");
  st25dv_emul_set_mem_size(0x01ff, 0x03);
  zassert_equal(BSP_NFCTAG_GetByteSize(), 2048, "ST25DV16KC");
  st25dv_emul_set_mem_size(0x07ff, 0x03);
  zassert_equal(BSP_NFCTAG_GetByteSize(), 8192, "ST25DV64KC");
}

ZTEST(nfc_wlc, test_blank_tag_is_not_formatted)
{
  zassert_equal(NfcType5_NDEFDetection(), NDEF_ERROR_NOT_FORMATED);
  zassert_equal(CCFileStruct.State, TT5_NO_NDEF);
}

ZTEST(nfc_wlc, test_cc_with_an_empty_ndef_tlv_is_an_initialized_tag)
{
  tag_holds("e1 40 40 01 03 00");

  zassert_equal(NfcType5_NDEFDetection(), NDEF_OK);
  zassert_equal(CCFileStruct.State, TT5_INITIALIZED);
  zassert_equal(CCFileStruct.NDEF_offset, 4);
  zassert_equal(CCFileStruct.MemorySize, 0x40);
}

ZTEST(nfc_wlc, test_wrong_magic_or_version_is_not_formatted)
{
  tag_holds("e3 40 40 01 03 00");
  zassert_equal(NfcType5_NDEFDetection(), NDEF_ERROR_NOT_FORMATED, "magic");
  tag_holds("e1 80 40 01");
  zassert_equal(NfcType5_NDEFDetection(), NDEF_ERROR_NOT_FORMATED, "version 2.0");
  tag_holds("e1 44 40 01");
  zassert_equal(NfcType5_NDEFDetection(), NDEF_ERROR_NOT_FORMATED, "no read access");
}

/*
 * CC byte 1 (NFC Forum T5T): version in b7..b4, read access in b3..b2, write access in b1..b0.
 * 43h is version 1.0, read always, write 3: only read access decides whether the tag is
 * formatted, so the CC is kept and the NDEF TLV goes after it.
 */
ZTEST(nfc_wlc, test_cc_with_write_access_bits_set_is_a_formatted_tag)
{
  tag_holds("e1 43 40 01 03 00");
  zassert_equal(NfcType5_NDEFDetection(), NDEF_OK);

  zassert_equal(nfc_write_ndef((const uint8_t *)"\x12\x34", 2), NDEF_OK);
  expect_bytes(st25dv_emul_user_mem(), 9, "e1 43 40 01 03 02 12 34 fe");
}

ZTEST(nfc_wlc, test_8_byte_cc_puts_the_ndef_tlv_at_offset_8)
{
  tag_holds("e2 40 00 01 00 00 00 40 03 00");

  zassert_equal(NfcType5_NDEFDetection(), NDEF_OK);
  zassert_equal(CCFileStruct.NDEF_offset, 8);
  zassert_equal(CCFileStruct.ExtMemorySize, 0x0040, "MLEN from bytes 6..7, big endian");
  zassert_equal(CCFileStruct.State, TT5_INITIALIZED);
}

/* DEFECT: a proprietary TLV is skipped by L only, without its T and L bytes. */
ZTEST_EXPECT_FAIL(nfc_wlc, test_proprietary_tlv_is_skipped_to_the_ndef_tlv);
ZTEST(nfc_wlc, test_proprietary_tlv_is_skipped_to_the_ndef_tlv)
{
  tag_holds("e1 40 40 01 fd 02 55 55 03 00");

  zassert_equal(NfcType5_NDEFDetection(), NDEF_OK);
  zassert_equal(CCFileStruct.NDEF_offset, 8);
}

ZTEST(nfc_wlc, test_tt5_init_writes_the_cc_and_an_empty_ndef_tlv)
{
  CCFileStruct.MagicNumber = 0xe1;
  CCFileStruct.Version = 0x40;
  CCFileStruct.MemorySize = 0x40;
  CCFileStruct.TT5Tag = 0x01;

  zassert_equal(NfcType5_TT5Init(), NDEF_OK);
  expect_bytes(st25dv_emul_user_mem(), 6, "e1 40 40 01 03 00");
  zassert_equal(CCFileStruct.NDEF_offset, 4);
}

ZTEST(nfc_wlc, test_ndef_on_a_blank_tag_formats_it_then_writes_the_tlv)
{
  zassert_equal(nfc_write_ndef((const uint8_t *)"\xab\xcd\xef", 3), NDEF_OK);
  expect_bytes(st25dv_emul_user_mem(), 10, "e1 40 40 01 03 03 ab cd ef fe");
}

ZTEST(nfc_wlc, test_ndef_on_a_formatted_tag_keeps_its_cc)
{
  tag_holds("e1 40 40 05 03 00");

  zassert_equal(nfc_write_ndef((const uint8_t *)"\x12\x34", 2), NDEF_OK);
  expect_bytes(st25dv_emul_user_mem(), 9, "e1 40 40 05 03 02 12 34 fe");
}

ZTEST(nfc_wlc, test_ndef_of_255_bytes_or_more_has_a_3_byte_tlv_length)
{
  static uint8_t msg[255];

  g1_fill_pattern(msg, sizeof(msg), 0);
  tag_holds("e1 40 40 01 03 00");

  zassert_equal(nfc_write_ndef(msg, sizeof(msg)), NDEF_OK);
  expect_bytes(&st25dv_emul_user_mem()[4], 4, "03 ff 00 ff");
  zassert_mem_equal(&st25dv_emul_user_mem()[8], msg, sizeof(msg));
  zassert_equal(st25dv_emul_user_mem()[8 + 255], 0xfe);
}

/* DEFECT: the chip takes 256 bytes per write; the failure returns NDEF_OK. */
ZTEST_EXPECT_FAIL(nfc_wlc, test_ndef_longer_than_256_bytes_is_written);
ZTEST(nfc_wlc, test_ndef_longer_than_256_bytes_is_written)
{
  static uint8_t msg[300];

  g1_fill_pattern(msg, sizeof(msg), 0);
  tag_holds("e1 40 40 01 03 00");

  nfc_write_ndef(msg, sizeof(msg));
  expect_bytes(&st25dv_emul_user_mem()[4], 4, "03 ff 01 2c");
  zassert_mem_equal(&st25dv_emul_user_mem()[8], msg, sizeof(msg));
  zassert_equal(st25dv_emul_user_mem()[8 + 300], 0xfe);
}

/* DEFECT: NDEF_ERROR_MEMORY_TAG is returned as NDEF_OK. */
ZTEST_EXPECT_FAIL(nfc_wlc, test_ndef_larger_than_the_tag_is_reported_as_an_error);
ZTEST(nfc_wlc, test_ndef_larger_than_the_tag_is_reported_as_an_error)
{
  static uint8_t msg[600];

  tag_holds("e1 40 40 01 03 00");

  zassert_equal(NfcType5_WriteNDEF(sizeof(msg), msg), NDEF_ERROR_MEMORY_TAG);
  zassert_equal(st25dv_emul_user_mem()[5], 0x00, "nothing written");
  zassert_not_equal(nfc_write_ndef(msg, sizeof(msg)), NDEF_OK);
}
