/*
 * Flash storage threads: the flash ops worker (audio / quick note / setting
 * stores) and the storage commit thread (settings commit, file download
 * into the external flash).
 */
#include "../main.h"
#include "../notify/demo_image.h"
#include "../sensors_audio/sensors_audio.h"
#include "../threads/threads.h"
#include "g1_log.h"
#include "glasses_state.h"
#include "storage.h"
#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/toolchain.h>

#define FILE_MSG_LEN    200
#define FILE_MSGQ_DEPTH 30U
#define DEMO_IMAGE_ADDR 0x1c0000U
#define DEMO_IMAGE_END  0x200000U

struct file_msg {
  uint8_t data[FILE_MSG_LEN];
  uint8_t len;
};

static uint8_t file_buf[0x1400];
uint8_t sys_settings_dirty;

struct k_sem flash_ops_sem;
uint32_t LAST_FILE_CRC;
static uint32_t file_addr;
static uint32_t file_buf_len;
static uint32_t file_type;
static struct k_msgq file_msgq;
static uint32_t crc32_table[256];
static struct k_thread storage_commit_thread_data;

static struct k_thread flash_ops_thread_data;
static K_THREAD_STACK_DEFINE(storage_commit_stack, 0x1400);
static K_THREAD_STACK_DEFINE(flash_ops_stack, 0x800);

static void flash_ops_thread(void *p1, void *p2, void *p3) {
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);
  G1_LOG_INF("flash op thread startup ...\n");
  while (1) {
    k_sem_take(&flash_ops_sem, K_FOREVER);
    while (getAudioMessageQueueNum() != 0U) {
      audioStreamFileManagerHandler();
    }
    while (getQuickNoteQueueNum() != 0) {
      QuickNoteStoreHandler();
    }
    while (getSettingStorageQueueNum() != 0) {
      SettingStoreHandler();
    }
  }
}

int file_init(void) {
  int ret;

  ret = k_msgq_alloc_init(&file_msgq, sizeof(struct file_msg), FILE_MSGQ_DEPTH);
  if (ret != 0) {
    printk("file init failed\r\n");
  } else {
    printk("file init success\r\n");
  }
  return ret;
}

int enqueue_file(const void *data, size_t len) {
  struct file_msg msg;
  int ret;

  if (len > FILE_MSG_LEN) {
    G1_LOG_ERR("enqueue_file too long %d\n", (int)len);
    return -EINVAL;
  }
  if (k_msgq_num_used_get(&file_msgq) == FILE_MSGQ_DEPTH) {
    k_msgq_get(&file_msgq, &msg, K_NO_WAIT);
    G1_LOG_ERR("enqueue_file is full\n");
  }
  memcpy(&msg, data, len);
  msg.len = len;
  ret = k_msgq_put(&file_msgq, &msg, K_NO_WAIT);
  if (ret != 0) {
    G1_LOG_ERR("enqueue_file failed\r\n\n");
  }
  return ret;
}

int dequeue_file(struct file_msg *out) {
  struct file_msg msg;
  int ret;

  ret = k_msgq_get(&file_msgq, &msg, K_NO_WAIT);
  if (ret != 0) {
    printk("dequeue_file failed\r\n");
  } else {
    memcpy(out, &msg, sizeof(msg));
  }
  return ret;
}

void crc32_init(void) {
  uint32_t i;
  uint32_t j;

  for (i = 0; i < 256U; i++) {
    uint32_t c = i;

    for (j = 0; j < 8U; j++) {
      if ((c & 1U) != 0U) {
        c = (c >> 1U) ^ 0xedb88320U;
      } else {
        c >>= 1U;
      }
    }
    crc32_table[i] = c;
  }
}

uint32_t crc32(uint32_t crc, const uint8_t *buf, int len) {
  const uint8_t *end = buf + len;
  const uint8_t *cur = buf;
  uint32_t value = ~crc;

  while (cur != end) {
    value = crc32_table[(value ^ *cur) & 0xffU] ^ (value >> 8U);
    cur++;
  }
  return ~value;
}

void try_to_save_file(struct GlassesState *gs) {
  struct file_msg msg;
  uint8_t len;
  bool end = false;

  if (dequeue_file(&msg) != 0) {
    return;
  }
  len = msg.len;
  if ((len <= 20) && (msg.data[0] == 0x0d) && (msg.data[1] == 0x0e)) {
    /* end of file */
    end = true;
  } else if (file_addr != 0U) {
    if ((file_type == DEMO_IMAGE_ADDR) && (file_addr >= DEMO_IMAGE_END)) {
      return;
    }
    memcpy(file_buf + file_buf_len, &msg.data[1], len - 1);
    file_buf_len += len - 1;
  } else {
    uint32_t addr = sys_get_be32(&msg.data[1]);

    file_addr = addr;
    LAST_FILE_CRC = crc32(LAST_FILE_CRC, &msg.data[1], 4);
    if (addr == DEMO_IMAGE_ADDR) {
      file_type = addr;
      get_dashboard_state()->dash_buf->type = 0x0c;
      if (get_dashboard_state()->task_status->task != 0x10U) {
        if (gs->dash_buf->data[0] == 0) {
          gs->dash_buf->data[0] = 10;
        }
        update_persist_task_status(get_dashboard_state(), 0x10, 2);
      }
    }
    memcpy(file_buf + file_buf_len, &msg.data[5], len - 5);
    file_buf_len += len - 5;
  }
  if ((file_type == DEMO_IMAGE_ADDR) && (file_addr >= DEMO_IMAGE_END)) {
    return;
  }
  if (file_buf_len >= 0x1000U) {
    write_font(file_addr, file_buf, 0x1000);
    LAST_FILE_CRC = crc32(LAST_FILE_CRC, file_buf, 0x1000);
    G1_LOG_ERR("crc32_value %08X\n", LAST_FILE_CRC);
    file_addr += 0x1000U;
    memcpy(file_buf, file_buf + 0x1000, file_buf_len - 0x1000U);
    file_buf_len -= 0x1000U;
  } else if (end) {
    write_font(file_addr, file_buf, file_buf_len);
    LAST_FILE_CRC = crc32(LAST_FILE_CRC, file_buf, (int)file_buf_len);
    G1_LOG_ERR("crc cal end crc32_value %08X\n", LAST_FILE_CRC);
  } else {
    /* nothing to do */
  }
}

static void storage_commit_thread(void *p1, void *p2, void *p3) {
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);
  struct GlassesState *gs = p1;
  int force_sys = 0;
  uint32_t start;

  file_init();
  crc32_init();
  while (1) {
    k_sem_take(&gs->dashboard_position_sem, K_FOREVER);
    bool saved = false;
    while (k_msgq_num_used_get(&file_msgq) != 0U) {
      try_to_save_file(gs);
      saved = true;
    }
    if ((gs->dashboard_cmd == 0x15) || saved) {
      continue;
    }
    switch (gs->dashboard_cmd) {
      case 1:
        gs->store.write("brightness_level", &gs->panel.panel_brightness_level, sizeof(uint8_t));
        break;
      case 2:
        gs->store.write("3dof_enable", &gs->imu.dof3_enable, sizeof(uint8_t));
        break;
      case 3:
        gs->store.write("display_mode", &gs->display_mode, sizeof(uint8_t));
        break;
      case 7:
        gs->store.write("countdown_ts", gs->countdown_ts, sizeof(struct countdown_ts_context));
        break;
      case 6:
        gs->store.write("dashboard_ts", gs->dashboard_ts, sizeof(struct dashboard_ts_context));
        break;
      case 8:
        gs->store.write("user_sched_info", gs->user_sched_info, sizeof(struct user_schedule_context));
        break;
      case 0x0b:
        gs->store.write("wakeup_angle", &gs->imu.level, sizeof(uint8_t));
        break;
      case 0x16:
        if (file_type == DEMO_IMAGE_ADDR) {
          get_demo_image_source_from_flash();
          if (get_dashboard_state()->is_master == 1) {
            get_dashboard_state()->dash_buf->type = 0x0d;
          } else {
            get_dashboard_state()->dash_buf->type = 0x0c;
          }
        }
        file_type = 0;
        file_buf_len = 0;
        LAST_FILE_CRC = 0;
        file_addr = 0;
        continue;
      case 0x17:
        exit_running_task(gs, 0);
        change_work_mode(1);
        continue;
      case 0x19:
      case 0x1a:
        force_sys = 1;
        break;
      case 0x11:
        k_sleep(K_SECONDS(10));
        change_work_mode(7);
        /* fallthrough */
      default:
        G1_LOG_INF("ignore.\n");
        break;
    }
    do {
      start = k_uptime_get();
      k_sem_take(&gs->dashboard_position_sem, K_MSEC(5000));
    } while ((k_uptime_get() - (int64_t)start) < 5000);
    gs->dashboard_cmd = 0;
    save_usr_settings(gs, 0);
    if (sys_settings_dirty) {
      sys_settings_dirty = 0;
      save_sys_settings(gs, force_sys);
    }
  }
}

void storage_commit_request(int cmd, uint8_t sys_dirty) {
  get_dashboard_state()->dashboard_cmd = cmd;
  if ((sys_settings_dirty != sys_dirty) && (sys_settings_dirty == 0)) {
    sys_settings_dirty = sys_dirty;
  }
  k_sem_give(&get_dashboard_state()->dashboard_position_sem);
}

void start_flash_storage_threads(struct GlassesState *gs) {
  k_sem_init(&flash_ops_sem, 0, 20);
  k_thread_create(&flash_ops_thread_data, flash_ops_stack, K_THREAD_STACK_SIZEOF(flash_ops_stack), flash_ops_thread, gs,
                  NULL, NULL, K_PRIO_COOP(4), 0, K_NO_WAIT);
  k_thread_create(&storage_commit_thread_data, storage_commit_stack, K_THREAD_STACK_SIZEOF(storage_commit_stack),
                  storage_commit_thread, gs, NULL, NULL, K_PRIO_COOP(5), 0, K_NO_WAIT);
}
