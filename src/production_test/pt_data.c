/*
 * Production-test link layer: selects the transport (NFC mailbox or lpuart),
 * reads one request, runs the PT protocol and writes the response back.
 */
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#include "../aw9320x/aw9320x.h"
#include "../main.h"
#include "../nfc/st25dv.h"
#include "../sensors_audio/sensors_audio.h"
#include "../threads/threads.h"
#include "g1_log.h"
#include "production_test.h"

/* current production-test transport */
static struct {
  void *nfc;                       /* st25dv context */
  const struct device *uart;       /* lpuart */
  uint8_t type;                    /* enum pt_comm_type */
} pt_comm;

#define PT_COMM_DEV() ((pt_comm.type == PT_COMM_NFC) ? pt_comm.nfc : (const void *)pt_comm.uart)

/* the largest reply (0x1b, 249 bytes) with the lpuart frame header and checksum */
#define PT_RESP_BUF_SIZE 256
static uint8_t pt_resp_buf[PT_RESP_BUF_SIZE];
static uint8_t pt_req_buf[256];

int setUARTDevice(const struct device *dev) {
  if (dev == NULL) {
    return -1;
  }

  pt_comm.uart = dev;
  pt_comm.type = PT_COMM_UART;

  return 0;
}

int setNFCDevice(void *nfc) {
  if (nfc == NULL) {
    return -1;
  }

  pt_comm.nfc = nfc;
  pt_comm.type = PT_COMM_NFC;

  return 0;
}

int lock_sensor_mutex(void) {
  if (pt_comm.nfc != NULL) {
    eeprom_st25dv_lock(pt_comm.nfc);
  }

  return 0;
}

int unlock_sensor_mutex(void) {
  if (pt_comm.nfc != NULL) {
    eeprom_st25dv_unlock(pt_comm.nfc);
  }

  return 0;
}

/* true when the last of the len bytes is the 8-bit sum of the bytes before it */
static bool pt_req_has_sum(const uint8_t *req, uint32_t len) {
  uint8_t sum = 0;

  for (uint32_t i = 0; (i + 1U) < len; i++) {
    sum += req[i];
  }
  return req[len - 1U] == sum;
}

int process_pt_data(void *arg) {
  uint8_t *p;
  uint8_t cmd;
  int ret;

  if ((pt_comm.type == PT_COMM_NONE) || ((pt_comm.type == PT_COMM_NFC) && (pt_comm.nfc == NULL)) ||
      ((pt_comm.type == PT_COMM_UART) && (pt_comm.uart == NULL))) {
    printk("comm para is error\n");
    return -1;
  }

  uint32_t resp_len = 0;
  uint32_t req_len = 0;
  uint8_t *req = pt_req_buf;
  uint8_t *resp = pt_resp_buf;

  pt_comm_busy_set();
  if (comm_read(pt_comm.type, PT_COMM_DEV(), &req, &req_len) != 0) {
    pt_comm_busy_clear();
    return -2;
  }

  /*
   * The mailbox read gives the length - 1, which drops the sum of a framed request
   * (cmd 01 02 n data sum). The case's poll, 13 00 ch 00 level lid, ends in data: without a
   * valid sum the last byte counts.
   */
  if ((pt_comm.type == PT_COMM_NFC) && (req_len < UINT8_MAX) && !pt_req_has_sum(pt_req_buf, req_len + 1U)) {
    req_len++;
  }

  if (process_pt_protocol(pt_comm.type, pt_req_buf, (uint8_t)req_len, &resp, (uint8_t *)&resp_len, arg) != 0) {
    printk("process_pt_protocol is failed\n");
    pt_comm_busy_clear();
    return -3;
  }

  ret = comm_write(pt_comm.type, PT_COMM_DEV(), resp, (uint8_t)resp_len);
  if (ret != 0) {
    printk("comm_write is failed\n");
    pt_comm_busy_clear();
    return -4;
  }
  pt_comm_busy_clear();

  /* the reply itself starts behind the lpuart frame header */
  p = resp;
  if (pt_comm.type != PT_COMM_NFC) {
    p += 4;
  }

  /*
   * A reply flagged in byte 9 goes out again a second later with the flag cleared, on the transport
   * it came in on.
   *
   * FIXME: the flag is not told apart from data. Byte 9 of the battery reply (get_battery_info())
   * is also the low byte of the charge current, so 128 mA is sent twice, the second time as 0 mA.
   * The test is not limited to the battery reply either: any reply with 0x80 in byte 9 is resent
   * with that byte cleared, and a reply shorter than 10 bytes is tested on what an earlier reply
   * left in the buffer.
   */
  if (p[9] == 0x80) {
    k_msleep(1000);
    p[9] = 0;
    resp_len--;
    pt_resp_checksum(&resp, (uint8_t *)&resp_len);
    pt_comm_busy_set();
    comm_write(pt_comm.type, PT_COMM_DEV(), resp, (uint8_t)resp_len);
    pt_comm_busy_clear();
  }

  cmd = p[0];
  if ((cmd == 0x1b) && p[8]) {
    erase_audio_buffer();
  }
  if ((cmd == 0x13) && ((p[4] & ~4U) == 0) && enable_charger()) {
    ret = -5;
  }
  if (cmd == 0x06) {
    if (p[2] == 2) {
      can_enter_low_power = 1;
      set_box_status(1, true);
      nfc_deadline_set(10000);
    } else {
      printk("enter into long trip \n");
      k_msleep(10000);
      change_work_mode(7);
    }
  }
  if (cmd == 0x0b) {
    G1_LOG_ERR("sys reboot because process_pt_data cmd PTP_DEVICE_RESTART\n");
    k_msleep(300);
    sys_reboot(SYS_REBOOT_COLD);
  }
  if ((cmd == 0x3c) && (p[4] == 0)) {
    G1_LOG_ERR("sys reboot because process_pt_data cmd %d and ACK_SUCC\n", cmd);
    k_msleep(300);
    sys_reboot(SYS_REBOOT_COLD);
  }
  if ((cmd == 0x01) && (p[4] == 0)) {
    G1_LOG_ERR("sys reboot because process_pt_data cmd ACK_SUCC\n");
    k_msleep(300);
    sys_reboot(SYS_REBOOT_COLD);
  }

  return ret;
}
