/*
 * AW93203 emulator: 16-bit register address, 32-bit words, MSB first, address + 4 per further word.
 * The firmware counts as valid when its check-enable word holds AW9320X_EMUL_CHECK_EN; the checksum
 * is not recomputed. A failed check leaves the boot loader running with error 09h and no interrupt.
 * In deep sleep REG_CMD is ignored until HOSTCTRL1 bit 0 wakes the CPU. IRQSRC clears on read; INT
 * is low while any bit is set. Access keys are stored, not enforced.
 * Not modelled: AFE scan timing, doze mode, gestures, GPIO mode, NVR, boot loader checksum, write /
 * erase durations, bus timeout.
 */
#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

#include "aw9320x_emul.h"

#define SFR_BASE   0x4000
#define SYS_BASE   0xff00

#define RSTNALL_KEY      0x3c
#define MCFG_CPU_HALT    BIT(16)
#define HOSTCTRL_WAKE    BIT(0)
#define UPDATA_DIS_JUMP  0xff000000u
#define ERR_FW_CHECK     0x09
#define PACK_HEADER      0x0002
#define PACK_FIXED       12

#define MODULE_UPDATE    0x01
#define MODULE_FLASH     0x02
#define UPDATE_START     0x01
#define UPDATE_TRANSFER  0x02
#define FLASH_ERASE_SECTOR 0x02

/* INT is wired to sw0 */
static const struct gpio_dt_spec int_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(sw0), gpios);

static struct aw9320x_emul_data aw9320x_data;

/* the SFR or SYS register word at addr, NULL elsewhere (SRAM is bytes) */
static uint32_t *word_of(struct aw9320x_emul_data *data, uint16_t addr)
{
  if (addr >= SFR_BASE && addr < SFR_BASE + sizeof(data->sfr)) {
    return &data->sfr[(addr - SFR_BASE) / 4];
  }
  if (addr >= SYS_BASE) {
    return &data->sys[(addr - SYS_BASE) / 4];
  }
  return NULL;
}

/* no side effects (IRQSRC not cleared) */
static uint32_t value_of(struct aw9320x_emul_data *data, uint16_t addr)
{
  uint32_t *word;

  addr &= ~3u;
  if (addr == AW9320X_EMUL_REG_IRQSRC) {
    return data->irqsrc;
  }
  if (addr == AW9320X_EMUL_REG_CHIPID) {
    return data->chip_id;
  }
  if (addr == AW9320X_EMUL_REG_ISPGO) {
    return 0; /* ISP operations complete at once */
  }
  if (addr == AW9320X_EMUL_REG_DIFF_CH0 && data->state == AW9320X_EMUL_APP) {
    return data->touched ? (uint32_t)data->touch_diff : 0;
  }
  if (addr < AW9320X_EMUL_SRAM_SIZE) {
    return sys_get_be32(&data->sram[addr]);
  }
  word = word_of(data, addr);
  return word ? *word : 0;
}

static void store(struct aw9320x_emul_data *data, uint16_t addr, uint32_t val)
{
  uint32_t *word;

  addr &= ~3u;
  if (addr < AW9320X_EMUL_SRAM_SIZE) {
    sys_put_be32(val, &data->sram[addr]);
    return;
  }
  word = word_of(data, addr);
  if (word) {
    *word = val;
  }
}

static void drive_int(struct aw9320x_emul_data *data)
{
  gpio_flags_t flags;

  if (gpio_emul_flags_get(int_gpio.port, int_gpio.pin, &flags) == 0 && (flags & GPIO_INPUT)) {
    gpio_emul_input_set(int_gpio.port, int_gpio.pin, data->int_low ? 0 : 1);
  }
}

/* call last: a GPIO callback may switch threads */
static void update_int(struct aw9320x_emul_data *data)
{
  bool low = data->irqsrc != 0;

  if (low != data->int_low) {
    data->int_low = low;
    drive_int(data);
  }
}

static bool fw_valid(const struct aw9320x_emul_data *data)
{
  return sys_get_be32(&data->flash[AW9320X_EMUL_CHECK_EN_ADDR]) == AW9320X_EMUL_CHECK_EN;
}

static void set_wst(struct aw9320x_emul_data *data)
{
  uint32_t wst = data->mode == AW9320X_EMUL_CMD_ACTIVE  ? AW9320X_EMUL_WST_ACTIVE
           : data->mode == AW9320X_EMUL_CMD_SLEEP ? AW9320X_EMUL_WST_SLEEP
                    : AW9320X_EMUL_WST_DEEPSLEEP;

  store(data, AW9320X_EMUL_REG_WST, wst);
}

static void init_done(struct aw9320x_emul_data *data)
{
  data->init_due = -1;
  if (!fw_valid(data)) {
    data->state = AW9320X_EMUL_BOOT_FAILED;
    store(data, AW9320X_EMUL_REG_ERR_CODE, ERR_FW_CHECK);
    return;
  }
  data->state = AW9320X_EMUL_APP;
  data->mode = AW9320X_EMUL_CMD_SLEEP;
  data->host_wake = false;
  set_wst(data);
  store(data, AW9320X_EMUL_REG_FIRMWARE, data->fw_version);
  data->irqsrc |= AW9320X_EMUL_IRQ_INIT;
}

static void advance(struct aw9320x_emul_data *data, int64_t now)
{
  if (data->state == AW9320X_EMUL_INIT && data->init_due >= 0 && now >= data->init_due) {
    init_done(data);
  }
}

static void start_init(struct aw9320x_emul_data *data, int64_t now)
{
  data->state = AW9320X_EMUL_INIT;
  data->init_due = now + AW9320X_EMUL_TINIT_MS;
}

static void soft_reset(struct aw9320x_emul_data *data, int64_t now)
{
  data->soft_resets++;
  memset(data->sram, 0, sizeof(data->sram));
  memset(data->sfr, 0, sizeof(data->sfr));
  memset(data->sys, 0, sizeof(data->sys));
  data->irqsrc = 0;
  start_init(data, now);
}

static void enter_boot_loader(struct aw9320x_emul_data *data)
{
  data->state = AW9320X_EMUL_BOOT_LOADER;
  data->init_due = -1;
  store(data, AW9320X_EMUL_REG_UPDATA_DIS,
        (value_of(data, AW9320X_EMUL_REG_UPDATA_DIS) & ~UPDATA_DIS_JUMP) | 0x01000000);
  store(data, AW9320X_EMUL_REG_BL_ACTIVE, 1);
  store(data, AW9320X_EMUL_REG_BT_VERSION, AW9320X_EMUL_BT_VERSION);
  store(data, AW9320X_EMUL_REG_BT_DATE, AW9320X_EMUL_BT_DATE);
  if (data->ack_irq) {
    data->irqsrc |= AW9320X_EMUL_IRQ_ACK;
  }
}

/* NOR programming only clears bits */
static void flash_program(struct aw9320x_emul_data *data, uint32_t offset, const uint8_t *src,
        size_t len)
{
  for (size_t index = 0; index < len; index++) {
    data->flash[offset + index] &= src[index];
  }
}

static void flash_erase_sector(struct aw9320x_emul_data *data, uint32_t offset)
{
  memset(&data->flash[offset & ~(AW9320X_EMUL_SECTOR_SIZE - 1u)], 0xff,
         AW9320X_EMUL_SECTOR_SIZE);
}

static void isp_go(struct aw9320x_emul_data *data)
{
  uint32_t cmd = value_of(data, AW9320X_EMUL_REG_ISPCMD) & 0xff;
  uint32_t offset = value_of(data, AW9320X_EMUL_REG_ISPADR) & ~3u;
  uint8_t word[4];

  data->isp_ops[cmd]++;
  if (offset >= AW9320X_EMUL_FLASH_SIZE) {
    return;
  }
  switch (cmd) {
  case AW9320X_EMUL_ISP_READ:
    store(data, AW9320X_EMUL_REG_ISPRDATA, sys_get_be32(&data->flash[offset]));
    break;
  case AW9320X_EMUL_ISP_PROGRAM:
    sys_put_be32(value_of(data, AW9320X_EMUL_REG_ISPWDATA), word);
    flash_program(data, offset, word, sizeof(word));
    break;
  case AW9320X_EMUL_ISP_ERASE:
    flash_erase_sector(data, offset);
    break;
  default:
    break;
  }
}

static uint32_t pack_checksum(uint8_t module, uint8_t command, uint16_t length,
            const uint8_t *value)
{
  uint32_t sum = module + command + length;

  for (uint32_t pos = 0; pos < length; pos += 4) {
    sum += sys_get_be32(&value[pos]);
  }
  return ~sum + 1;
}

static int32_t flash_offset(uint32_t addr, uint32_t len)
{
  if (addr < AW9320X_EMUL_FLASH_BASE ||
      addr - AW9320X_EMUL_FLASH_BASE + len > AW9320X_EMUL_FLASH_SIZE) {
    return -1;
  }
  return (int32_t)(addr - AW9320X_EMUL_FLASH_BASE);
}

static void parse_packet(struct aw9320x_emul_data *data)
{
  struct aw9320x_emul_cmd cmd = { 0 };
  uint32_t pkt = value_of(data, AW9320X_EMUL_REG_CMD_PACK) - AW9320X_EMUL_SRAM_BASE;
  uint8_t *ack = &data->sram[AW9320X_EMUL_ACK_ADDR];
  const uint8_t *packet;
  const uint8_t *value;
  int32_t offset;

  cmd.status = AW9320X_EMUL_STATUS_BAD_PACKET;
  if (pkt <= AW9320X_EMUL_SRAM_SIZE - PACK_FIXED) {
    packet = &data->sram[pkt];
    value = packet + PACK_FIXED;
    cmd.module = packet[8];
    cmd.command = packet[9];
    cmd.length = sys_get_be16(&packet[10]);
    if (sys_get_be16(&packet[0]) == PACK_HEADER &&
        sys_get_be16(&packet[2]) == PACK_FIXED + cmd.length &&
        cmd.length <= AW9320X_EMUL_SRAM_SIZE - PACK_FIXED - pkt &&
        sys_get_be32(&packet[4]) ==
          pack_checksum(cmd.module, cmd.command, cmd.length, value)) {
      cmd.status = AW9320X_EMUL_STATUS_UNKNOWN;
      if (data->cmds == data->fail_cmd) {
        cmd.status = AW9320X_EMUL_STATUS_INJECTED;
      } else if (cmd.module == MODULE_UPDATE && cmd.command == UPDATE_START) {
        cmd.status = 0;
      } else if (cmd.module == MODULE_FLASH &&
           cmd.command == FLASH_ERASE_SECTOR && cmd.length == 4) {
        cmd.addr = sys_get_be32(value);
        offset = flash_offset(cmd.addr, 1);
        cmd.status = AW9320X_EMUL_STATUS_BAD_ADDR;
        if (offset >= 0) {
          flash_erase_sector(data, offset);
          cmd.status = 0;
        }
      } else if (cmd.module == MODULE_UPDATE && cmd.command == UPDATE_TRANSFER &&
           cmd.length >= 8) {
        cmd.seq = sys_get_be32(value);
        cmd.addr = sys_get_be32(value + 4);
        offset = flash_offset(cmd.addr, cmd.length - 8);
        cmd.status = AW9320X_EMUL_STATUS_BAD_ADDR;
        if (offset >= 0) {
          flash_program(data, offset, value + 8, cmd.length - 8);
          cmd.status = 0;
        }
      }
    }
  }
  if (data->cmds < AW9320X_EMUL_CMD_LOG) {
    data->cmd_log[data->cmds] = cmd;
  }
  data->cmds++;

  sys_put_be16(PACK_HEADER, &ack[0]);
  sys_put_be16(PACK_FIXED + 8, &ack[2]);
  ack[8] = cmd.module;
  ack[9] = cmd.command;
  sys_put_be16(8, &ack[10]);
  sys_put_be32(cmd.status, &ack[12]);
  sys_put_be32(cmd.seq, &ack[16]);
  sys_put_be32(pack_checksum(cmd.module, cmd.command, 8, &ack[12]), &ack[4]);
  if (data->ack_irq) {
    data->irqsrc |= AW9320X_EMUL_IRQ_ACK;
  }
}

static void set_mode(struct aw9320x_emul_data *data, uint32_t mode)
{
  if (data->mode == AW9320X_EMUL_CMD_DEEPSLEEP && !data->host_wake) {
    return; /* asleep until HOSTCTRL1 wake */
  }
  data->mode = mode;
  if (mode == AW9320X_EMUL_CMD_DEEPSLEEP) {
    data->host_wake = false;
  }
  set_wst(data);
}

static void cmd_write(struct aw9320x_emul_data *data, uint32_t val)
{
  switch (data->state) {
  case AW9320X_EMUL_APP:
    if (val == AW9320X_EMUL_CMD_ONLINE_UPGRADE) {
      enter_boot_loader(data);
    } else if (val >= AW9320X_EMUL_CMD_ACTIVE && val <= AW9320X_EMUL_CMD_DEEPSLEEP) {
      set_mode(data, val);
    }
    break;
  case AW9320X_EMUL_BOOT_FAILED:
  case AW9320X_EMUL_BOOT_LOADER:
    if (val == AW9320X_EMUL_CMD_ONLINE_UPGRADE) {
      enter_boot_loader(data);
    } else if (val == AW9320X_EMUL_CMD_PARSE &&
         data->state == AW9320X_EMUL_BOOT_LOADER) {
      parse_packet(data);
    }
    break;
  default:
    break;
  }
}

static void mcfg_write(struct aw9320x_emul_data *data, uint32_t val, int64_t now)
{
  if (val & MCFG_CPU_HALT) {
    data->state = AW9320X_EMUL_HALTED;
    data->init_due = -1;
  } else if (data->state == AW9320X_EMUL_HALTED) {
    /* CPU restart: boot loader unless the jump is blocked */
    if (value_of(data, AW9320X_EMUL_REG_UPDATA_DIS) & UPDATA_DIS_JUMP) {
      data->state = AW9320X_EMUL_BOOT_LOADER;
    } else {
      start_init(data, now);
    }
  }
}

static void reg_write(struct aw9320x_emul_data *data, uint16_t addr, uint32_t val, int64_t now)
{
  if (data->writes < AW9320X_EMUL_WRITE_LOG) {
    data->write_addr[data->writes] = addr;
    data->write_val[data->writes] = val;
  }
  data->writes++;
  advance(data, now);

  if (addr == AW9320X_EMUL_REG_CHIPID || addr == AW9320X_EMUL_REG_IRQSRC) {
    return; /* read-only */
  }
  store(data, addr, val);
  switch (addr) {
  case AW9320X_EMUL_REG_RSTNALL:
    if (val == RSTNALL_KEY) {
      soft_reset(data, now);
    }
    break;
  case AW9320X_EMUL_REG_MCFG:
    mcfg_write(data, val, now);
    break;
  case AW9320X_EMUL_REG_CMD:
    cmd_write(data, val);
    break;
  case AW9320X_EMUL_REG_HOSTCTRL1:
    if (val & HOSTCTRL_WAKE) {
      data->host_wake = true;
    }
    break;
  case AW9320X_EMUL_REG_ISPGO:
    if (val & 1) {
      isp_go(data);
    }
    break;
  default:
    break;
  }
}

static uint32_t reg_read(struct aw9320x_emul_data *data, uint16_t addr, int64_t now)
{
  uint32_t value;

  advance(data, now);
  value = value_of(data, addr);
  if ((addr & ~3u) == AW9320X_EMUL_REG_IRQSRC) {
    data->irqsrc = 0;
  }
  return value;
}

static bool nak_now(struct aw9320x_emul_data *data)
{
  if (data->nak) {
    return true;
  }
  if (data->nak_after_armed && data->cmds >= data->nak_after_cmds) {
    data->nak_after_armed = false;
    data->nak_remaining = data->nak_after_count;
  }
  if (data->nak_remaining > 0) {
    data->nak_remaining--;
    return true;
  }
  return false;
}

static int aw9320x_emul_transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs,
         int addr)
{
  struct aw9320x_emul_data *data = target->data;
  int64_t now = k_uptime_get();
  int ret = 0;

  ARG_UNUSED(addr);
  data->transfers++;
  if (nak_now(data)) {
    return -EIO;
  }
  for (int index = 0; index < num_msgs && ret == 0; index++) {
    struct i2c_msg *msg = &msgs[index];

    if (msg->flags & I2C_MSG_READ) {
      uint32_t word = 0;

      for (uint32_t pos = 0; pos < msg->len; pos++) {
        if (pos % 4 == 0) {
          word = reg_read(data, data->pointer + pos, now);
        }
        msg->buf[pos] = word >> (8 * (3 - pos % 4));
      }
      continue;
    }
    if (msg->len < 2) {
      ret = -EIO;
      break;
    }
    data->pointer = sys_get_be16(msg->buf);
    for (uint32_t pos = 2; pos + 4 <= msg->len; pos += 4) {
      reg_write(data, data->pointer + (pos - 2), sys_get_be32(&msg->buf[pos]),
          now);
    }
  }
  update_int(data);
  return ret;
}

static const struct i2c_emul_api aw9320x_emul_bus_api = {
  .transfer = aw9320x_emul_transfer,
};

static void power_on(struct aw9320x_emul_data *data)
{
  memset(data, 0, sizeof(*data));
  data->chip_id = AW9320X_EMUL_CHIP_ID;
  data->fw_version = AW9320X_EMUL_FW_VERSION;
  data->touch_diff = AW9320X_EMUL_TOUCH_DIFF;
  data->ack_irq = true;
  data->fail_cmd = -1;
  memset(data->flash, 0xff, sizeof(data->flash));
  sys_put_be32(AW9320X_EMUL_CHECK_EN, &data->flash[AW9320X_EMUL_CHECK_EN_ADDR]);
  /* init done, interrupt not yet read */
  data->state = AW9320X_EMUL_INIT;
  init_done(data);
}

static int aw9320x_emul_init(const struct emul *target, const struct device *parent)
{
  ARG_UNUSED(parent);
  power_on(target->data);
  return 0;
}

EMUL_DT_DEFINE(DT_NODELABEL(aw93203), aw9320x_emul_init, &aw9320x_data, NULL,
         &aw9320x_emul_bus_api, NULL);

struct aw9320x_emul_data *aw9320x_emul(void)
{
  advance(&aw9320x_data, k_uptime_get());
  return &aw9320x_data;
}

void aw9320x_emul_set_fw_version(uint32_t version)
{
  struct aw9320x_emul_data *data = aw9320x_emul();

  data->fw_version = version;
  if (data->state == AW9320X_EMUL_APP) {
    store(data, AW9320X_EMUL_REG_FIRMWARE, version);
  }
}

void aw9320x_emul_corrupt_fw(void)
{
  memset(&aw9320x_emul()->flash[AW9320X_EMUL_CHECK_EN_ADDR], 0xff, 4);
}

/* NAKs the next count transfers once cmds boot loader commands are done */
void aw9320x_emul_nak_after_cmds(int cmds, int count)
{
  struct aw9320x_emul_data *data = aw9320x_emul();

  data->nak_after_armed = true;
  data->nak_after_cmds = cmds;
  data->nak_after_count = count;
}

bool aw9320x_emul_touch(bool down)
{
  struct aw9320x_emul_data *data = aw9320x_emul();
  uint32_t bit = down ? AW9320X_EMUL_IRQ_PROX : AW9320X_EMUL_IRQ_PROX_EXIT;
  bool latched = data->state == AW9320X_EMUL_APP && data->mode == AW9320X_EMUL_CMD_ACTIVE &&
           (value_of(data, AW9320X_EMUL_REG_IRQEN) & bit);

  data->touched = down;
  if (latched) {
    data->irqsrc |= bit;
  }
  update_int(data);
  return latched;
}

bool aw9320x_emul_int_asserted(void)
{
  struct aw9320x_emul_data *data = aw9320x_emul();

  update_int(data);
  return data->int_low;
}

void aw9320x_emul_sync_int(void)
{
  struct aw9320x_emul_data *data = aw9320x_emul();

  data->int_low = data->irqsrc != 0;
  drive_int(data);
}

uint32_t aw9320x_emul_get_reg(uint16_t addr)
{
  return value_of(aw9320x_emul(), addr);
}

void aw9320x_emul_set_reg(uint16_t addr, uint32_t val)
{
  struct aw9320x_emul_data *data = aw9320x_emul();

  if ((addr & ~3u) == AW9320X_EMUL_REG_IRQSRC) {
    data->irqsrc = val;
  } else if ((addr & ~3u) == AW9320X_EMUL_REG_CHIPID) {
    data->chip_id = val;
  } else {
    store(data, addr, val);
  }
  update_int(data);
}

uint32_t aw9320x_emul_flash_word(uint32_t addr)
{
  __ASSERT(addr + 4 <= AW9320X_EMUL_FLASH_SIZE, "flash word beyond 4000h");
  return sys_get_be32(&aw9320x_data.flash[addr]);
}

int aw9320x_emul_find_write(uint16_t addr, int from)
{
  struct aw9320x_emul_data *data = &aw9320x_data;

  for (int index = MAX(from, 0); index < MIN(data->writes, AW9320X_EMUL_WRITE_LOG); index++) {
    if (data->write_addr[index] == addr) {
      return index;
    }
  }
  return -1;
}

int aw9320x_emul_count_writes(uint16_t addr, uint32_t val)
{
  struct aw9320x_emul_data *data = &aw9320x_data;
  int count = 0;

  for (int index = 0; index < MIN(data->writes, AW9320X_EMUL_WRITE_LOG); index++) {
    if (data->write_addr[index] == addr && data->write_val[index] == val) {
      count++;
    }
  }
  return count;
}

static void aw9320x_emul_reset(bool keep_media)
{
  ARG_UNUSED(keep_media);
  power_on(&aw9320x_data);
  drive_int(&aw9320x_data);
}

G1_UNIT_RESET_HOOK(aw9320x_emul_reset);
