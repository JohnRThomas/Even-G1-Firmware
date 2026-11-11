/*
 * ST25DV04KC emulator; both device selects reach the one chip. Only user area 1 is modelled.
 * After an EEPROM write the chip NAKs for tW per 16-byte row. A GPO pulse is shorter than a tick,
 * so a connected gpio_emul input sees low then high within one call.
 * Not modelled: other RF commands, areas 2..4 and RF protection, LOCK_* effects, the mailbox
 * watchdog, RF_MNGT effects, RFSwitchOff/On, energy harvesting output, low-power-down pin.
 */
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

#include "harness.h"
#include "st25dv_emul.h"

#define SYS_REG_COUNT   0x24
#define SYS_ENDA1       0x0005
#define SYS_ENDA2       0x0007
#define SYS_ENDA3       0x0009
#define SYS_RO_FIRST    0x0010  /* read-only from I2C */
#define PWD_LEN         8
#define PWD_CMD_LEN     (2 * PWD_LEN + 1)
#define PWD_PRESENT     0x09
#define PWD_WRITE       0x07
#define ROW_SIZE        16      /* EEPROM row */
#define SEQ_WRITE_MAX   256
#define FACTORY_USER    512     /* ST25DV04KC */

struct st25dv_emul_data {
  /* EEPROM: kept across a reboot */
  uint8_t user[ST25DV_EMUL_USER_MAX];
  size_t user_size;
  uint8_t sys[SYS_REG_COUNT];
  uint8_t password[PWD_LEN];

  /* volatile, from gpo_ctrl on: cleared at power-on */
  uint8_t gpo_ctrl;
  uint8_t eh_ctrl;
  uint8_t rf_mngt;
  bool session;
  uint8_t it_sts;
  uint8_t mb_ctrl;
  uint8_t mb_len;
  uint8_t mailbox[ST25DV_EMUL_MB_SIZE];
  uint16_t ptr[2];        /* [1]: system area */
  int64_t busy_until;
  bool field;

  bool nak;
  int fail_next;
  uint32_t write_time_ms;
  const struct device *gpo_port;
  uint8_t gpo_pin;

  int xfers;
  struct st25dv_emul_xfer log[ST25DV_EMUL_LOG];
};

static struct st25dv_emul_data chip;

static void factory_eeprom(void)
{
  static const uint8_t uid[8] = {0x78, 0x56, 0x34, 0x12, 0x01, ST25DV_EMUL_IC_REF_04KC,
               0x02, 0xe0};
  uint8_t enda = FACTORY_USER / 32 - 1;

  memset(chip.user, 0, sizeof(chip.user));
  chip.user_size = FACTORY_USER;
  memset(chip.sys, 0, sizeof(chip.sys));
  chip.sys[ST25DV_EMUL_SYS_GPO1] =
    ST25DV_EMUL_GPO1_GPO_EN | ST25DV_EMUL_GPO1_FIELD_CHANGE_EN;
  chip.sys[ST25DV_EMUL_SYS_GPO2] = 0x0c;
  chip.sys[ST25DV_EMUL_SYS_EH_MODE] = 0x01;
  chip.sys[SYS_ENDA1] = enda;
  chip.sys[SYS_ENDA2] = enda;
  chip.sys[SYS_ENDA3] = enda;
  chip.sys[ST25DV_EMUL_SYS_I2C_CFG] = 0x1a;
  chip.sys[ST25DV_EMUL_SYS_MEM_SIZE] = FACTORY_USER / 4 - 1;
  chip.sys[ST25DV_EMUL_SYS_MEM_SIZE + 1] = 0;
  chip.sys[ST25DV_EMUL_SYS_BLK_SIZE] = 0x03;
  chip.sys[ST25DV_EMUL_SYS_IC_REF] = ST25DV_EMUL_IC_REF_04KC;
  memcpy(&chip.sys[ST25DV_EMUL_SYS_UID], uid, sizeof(uid));
  chip.sys[ST25DV_EMUL_SYS_IC_REV] = ST25DV_EMUL_IC_REV;
  memset(chip.password, 0, sizeof(chip.password));
}

/* power-on, at init and before every test: the dynamic registers take their static images */
static void st25dv_emul_reset(bool keep_eeprom)
{
  if (!keep_eeprom) {
    factory_eeprom();
  }
  memset(&chip.gpo_ctrl, 0, sizeof(chip) - offsetof(struct st25dv_emul_data, gpo_ctrl));
  chip.gpo_ctrl = chip.sys[ST25DV_EMUL_SYS_GPO1] & ST25DV_EMUL_GPO1_GPO_EN;
  chip.eh_ctrl = ST25DV_EMUL_VCC_ON;
  if ((chip.sys[ST25DV_EMUL_SYS_EH_MODE] & 0x01) == 0) {
    chip.eh_ctrl |= ST25DV_EMUL_EH_EN | ST25DV_EMUL_EH_ON;
  }
  chip.rf_mngt = chip.sys[ST25DV_EMUL_SYS_RF_MNGT] & 0x03;
  chip.write_time_ms = ST25DV_EMUL_WRITE_TIME_MS;
}

G1_UNIT_RESET_HOOK(st25dv_emul_reset);

static void gpo_pulse(void)
{
  if (!(chip.gpo_ctrl & ST25DV_EMUL_GPO1_GPO_EN) || chip.gpo_port == NULL) {
    return;
  }
  /* pin may not be an input yet */
  (void)gpio_emul_input_set(chip.gpo_port, chip.gpo_pin, 0);
  (void)gpio_emul_input_set(chip.gpo_port, chip.gpo_pin, 1);
}

static void rf_event(uint8_t gpo1_enable, uint8_t it_bit)
{
  if (!(chip.sys[ST25DV_EMUL_SYS_GPO1] & gpo1_enable)) {
    return;
  }
  chip.it_sts |= it_bit;
  gpo_pulse();
}

static bool mb_free(void)
{
  return !(chip.mb_ctrl & (ST25DV_EMUL_HOST_PUT_MSG | ST25DV_EMUL_RF_PUT_MSG));
}

static void mb_disable(void)
{
  chip.mb_ctrl = 0;
  chip.mb_len = 0;
}

/* MB_EN needs mailbox mode (FTM); enabling starts with an empty mailbox */
static int mb_enable(bool on)
{
  if (!on) {
    mb_disable();
    return 0;
  }
  if (!(chip.sys[ST25DV_EMUL_SYS_FTM] & ST25DV_EMUL_FTM_MB_MODE)) {
    return -EACCES;
  }
  if (!(chip.mb_ctrl & ST25DV_EMUL_MB_EN)) {
    chip.mb_ctrl = ST25DV_EMUL_MB_EN;
    chip.mb_len = 0;
  }
  return 0;
}

/* a message from the host (HOST_PUT_MSG | HOST_CURRENT_MSG) or the reader (RF_*) */
static void mb_put(const uint8_t *msg, size_t len, uint8_t flags)
{
  memcpy(chip.mailbox, msg, len);
  chip.mb_len = (uint8_t)(len - 1);
  chip.mb_ctrl &= ST25DV_EMUL_MB_EN | ST25DV_EMUL_HOST_MISS_MSG | ST25DV_EMUL_RF_MISS_MSG;
  chip.mb_ctrl |= flags;
}

static uint8_t read_sys(uint16_t addr)
{
  if (addr < SYS_REG_COUNT) {
    return chip.sys[addr];
  }
  if (addr >= ST25DV_EMUL_SYS_I2C_PWD && addr < ST25DV_EMUL_SYS_I2C_PWD + PWD_LEN) {
    return chip.session ? chip.password[addr - ST25DV_EMUL_SYS_I2C_PWD] : 0xff;
  }
  return 0xff;
}

/* a dynamic register without side effects, 0xff at any other address */
uint8_t st25dv_emul_get_dyn(uint16_t addr)
{
  switch (addr) {
  case ST25DV_EMUL_DYN_GPO_CTRL:
    return chip.gpo_ctrl;
  case ST25DV_EMUL_DYN_EH_CTRL:
    return chip.eh_ctrl | (chip.field ? ST25DV_EMUL_FIELD_ON : 0);
  case ST25DV_EMUL_DYN_RF_MNGT:
    return chip.rf_mngt;
  case ST25DV_EMUL_DYN_I2C_SSO:
    return chip.session ? 0x01 : 0x00;
  case ST25DV_EMUL_DYN_IT_STS:
    return chip.it_sts;
  case ST25DV_EMUL_DYN_MB_CTRL:
    return chip.mb_ctrl;
  case ST25DV_EMUL_DYN_MB_LEN:
    return chip.mb_len;
  default:
    return 0xff;
  }
}

static uint8_t read_user(uint16_t addr, bool *rf_msg_done)
{
  uint8_t value;

  if (addr < chip.user_size) {
    return chip.user[addr];
  }
  if (addr >= ST25DV_EMUL_MAILBOX && addr < ST25DV_EMUL_MAILBOX + ST25DV_EMUL_MB_SIZE) {
    uint16_t offset = addr - ST25DV_EMUL_MAILBOX;

    if (!(chip.mb_ctrl & ST25DV_EMUL_MB_EN) || offset > chip.mb_len) {
      return 0xff;
    }
    if ((chip.mb_ctrl & ST25DV_EMUL_RF_PUT_MSG) && offset == chip.mb_len) {
      *rf_msg_done = true;
    }
    return chip.mailbox[offset];
  }
  value = st25dv_emul_get_dyn(addr);
  if (addr == ST25DV_EMUL_DYN_IT_STS) {
    /* cleared by the read */
    chip.it_sts = 0;
  }
  return value;
}

static void program(int64_t now, int rows)
{
  chip.busy_until = now + (int64_t)chip.write_time_ms * rows;
  if (chip.sys[ST25DV_EMUL_SYS_GPO2] & ST25DV_EMUL_GPO2_I2C_WRITE_EN) {
    gpo_pulse();
  }
}

/* password, validation code, password again */
static int write_password(const uint8_t *cmd, size_t len, int64_t now)
{
  if (len != PWD_CMD_LEN) {
    return -EIO;
  }
  if (cmd[PWD_LEN] == PWD_PRESENT) {
    if (memcmp(cmd, &cmd[PWD_LEN + 1], PWD_LEN) == 0) {
      chip.session = memcmp(cmd, chip.password, PWD_LEN) == 0;
    }
    return 0;
  }
  if (cmd[PWD_LEN] == PWD_WRITE) {
    if (!chip.session || (chip.mb_ctrl & ST25DV_EMUL_MB_EN)) {
      return -EIO;
    }
    if (memcmp(cmd, &cmd[PWD_LEN + 1], PWD_LEN) == 0) {
      memcpy(chip.password, cmd, PWD_LEN);
      program(now, 1);
    }
    return 0;
  }
  return -EIO;
}

static int write_sys(uint16_t addr, const uint8_t *bytes, size_t len, int64_t now)
{
  if (addr == ST25DV_EMUL_SYS_I2C_PWD) {
    return write_password(bytes, len, now);
  }
  if (len != 1 || !chip.session || addr >= SYS_RO_FIRST) {
    return -EIO;
  }
  chip.sys[addr] = bytes[0];
  switch (addr) {
  case ST25DV_EMUL_SYS_GPO1:
    chip.gpo_ctrl = bytes[0] & ST25DV_EMUL_GPO1_GPO_EN;
    break;
  case ST25DV_EMUL_SYS_EH_MODE:
    if ((bytes[0] & 0x01) == 0) {
      chip.eh_ctrl |= ST25DV_EMUL_EH_EN | ST25DV_EMUL_EH_ON;
    }
    break;
  case ST25DV_EMUL_SYS_RF_MNGT:
    chip.rf_mngt = bytes[0] & 0x03;
    break;
  case ST25DV_EMUL_SYS_FTM:
    if (!(bytes[0] & ST25DV_EMUL_FTM_MB_MODE)) {
      mb_disable();
    }
    break;
  default:
    break;
  }
  program(now, 1);
  return 0;
}

static int write_dyn(uint16_t addr, uint8_t value)
{
  switch (addr) {
  case ST25DV_EMUL_DYN_GPO_CTRL:
    chip.gpo_ctrl = value & ST25DV_EMUL_GPO1_GPO_EN;
    return 0;
  case ST25DV_EMUL_DYN_EH_CTRL:
    chip.eh_ctrl &= ~(ST25DV_EMUL_EH_EN | ST25DV_EMUL_EH_ON);
    if (value & ST25DV_EMUL_EH_EN) {
      chip.eh_ctrl |= ST25DV_EMUL_EH_EN | ST25DV_EMUL_EH_ON;
    }
    return 0;
  case ST25DV_EMUL_DYN_RF_MNGT:
    chip.rf_mngt = value & 0x03;
    return 0;
  case ST25DV_EMUL_DYN_MB_CTRL:
    /* ignored outside mailbox mode */
    (void)mb_enable(value & ST25DV_EMUL_MB_EN);
    return 0;
  default:
    return -EIO;
  }
}

static int write_user(uint16_t addr, const uint8_t *bytes, size_t len, int64_t now)
{
  if (addr < chip.user_size) {
    uint8_t prot = chip.sys[ST25DV_EMUL_SYS_I2CSS] & 0x03;

    if ((chip.mb_ctrl & ST25DV_EMUL_MB_EN) || len > SEQ_WRITE_MAX ||
        addr + len > chip.user_size || ((prot == 1 || prot == 3) && !chip.session)) {
      return -EIO;
    }
    memcpy(&chip.user[addr], bytes, len);
    program(now, (int)((addr + len - 1) / ROW_SIZE - addr / ROW_SIZE + 1));
    return 0;
  }
  if (addr >= ST25DV_EMUL_DYN_GPO_CTRL && addr <= ST25DV_EMUL_DYN_MB_LEN) {
    return len == 1 ? write_dyn(addr, bytes[0]) : -EIO;
  }
  if (addr == ST25DV_EMUL_MAILBOX) {
    if (!(chip.mb_ctrl & ST25DV_EMUL_MB_EN) || !mb_free() ||
        len > ST25DV_EMUL_MB_SIZE) {
      return -EIO;
    }
    mb_put(bytes, len, ST25DV_EMUL_HOST_PUT_MSG | ST25DV_EMUL_HOST_CURRENT_MSG);
    return 0;
  }
  return -EIO;
}

/* one I2C transaction, ended by a STOP */
static int transfer(struct i2c_msg *msgs, int num_msgs, bool sys, struct st25dv_emul_xfer *log)
{
  int64_t now = k_uptime_get();
  uint16_t *ptr = &chip.ptr[sys ? 1 : 0];
  uint8_t written[2 + SEQ_WRITE_MAX + 1];
  size_t written_len = 0; /* address bytes included */
  bool too_long = false;
  bool rf_msg_done = false;
  int ret = 0;

  if (chip.nak || now < chip.busy_until) {
    return -EIO;
  }
  if (chip.fail_next > 0) {
    chip.fail_next--;
    return -EIO;
  }
  for (int index = 0; index < num_msgs; index++) {
    struct i2c_msg *msg = &msgs[index];

    if (msg->flags & I2C_MSG_READ) {
      if (written_len == 1) {
        return -EIO; /* half an address */
      }
      if (written_len > 2) {
        /* restart after data: no STOP, nothing programmed */
        written_len = 2;
      }
      for (uint32_t pos = 0; pos < msg->len; pos++) {
        msg->buf[pos] = sys ? read_sys(*ptr)
                : read_user(*ptr, &rf_msg_done);
        (*ptr)++;
      }
      if (log) {
        log->rd_len += msg->len;
      }
      continue;
    }
    for (uint32_t pos = 0; pos < msg->len; pos++) {
      if (written_len < sizeof(written)) {
        written[written_len] = msg->buf[pos];
      } else {
        too_long = true;
      }
      if (log && written_len < ST25DV_EMUL_LOG_BYTES) {
        log->wr[written_len] = msg->buf[pos];
      }
      written_len++;
      if (written_len == 2) {
        *ptr = sys_get_be16(written);
      }
    }
    if (log) {
      log->wr_len = (uint16_t)MIN(written_len, UINT16_MAX);
    }
  }

  if (written_len == 1) {
    ret = -EIO;
  } else if (written_len > 2) {
    uint16_t addr = sys_get_be16(written);
    size_t len = written_len - 2;

    if (too_long) {
      ret = -EIO;
    } else {
      ret = sys ? write_sys(addr, &written[2], len, now)
          : write_user(addr, &written[2], len, now);
    }
    if (ret == 0) {
      *ptr = (uint16_t)(addr + len);
    }
  }
  if (rf_msg_done) {
    /* host read the RF message to its end: mailbox free */
    chip.mb_ctrl &= ~ST25DV_EMUL_RF_PUT_MSG;
  }
  return ret;
}

static int st25dv_emul_transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs,
        int addr)
{
  struct st25dv_emul_xfer *log = NULL;
  int ret;

  ARG_UNUSED(target);
  if (chip.xfers < ST25DV_EMUL_LOG) {
    log = &chip.log[chip.xfers];
    memset(log, 0, sizeof(*log));
    log->addr = (uint8_t)addr;
  }
  chip.xfers++;
  ret = transfer(msgs, num_msgs, addr == ST25DV_EMUL_ADDR_SYS, log);
  if (log) {
    log->ret = ret;
  }
  return ret;
}

static const struct i2c_emul_api st25dv_emul_bus_api = {
  .transfer = st25dv_emul_transfer,
};

static int st25dv_emul_init(const struct emul *target, const struct device *parent)
{
  ARG_UNUSED(parent);
  ARG_UNUSED(target);
  st25dv_emul_reset(false);
  return 0;
}

EMUL_DT_DEFINE(DT_NODELABEL(st25dv), st25dv_emul_init, &chip, NULL, &st25dv_emul_bus_api,
         NULL);
EMUL_DT_DEFINE(DT_NODELABEL(st25dv_sys), st25dv_emul_init, &chip, NULL,
         &st25dv_emul_bus_api, NULL);

void st25dv_emul_rf_field(bool on)
{
  if (chip.field == on) {
    return;
  }
  chip.field = on;
  rf_event(ST25DV_EMUL_GPO1_FIELD_CHANGE_EN,
     on ? ST25DV_EMUL_IT_FIELD_RISING : ST25DV_EMUL_IT_FIELD_FALLING);
}

int st25dv_emul_rf_put_msg(const uint8_t *msg, size_t len)
{
  if (len == 0 || len > ST25DV_EMUL_MB_SIZE) {
    return -EINVAL;
  }
  if (!(chip.mb_ctrl & ST25DV_EMUL_MB_EN)) {
    return -EACCES;
  }
  if (!mb_free()) {
    return -EBUSY;
  }
  mb_put(msg, len, ST25DV_EMUL_RF_PUT_MSG | ST25DV_EMUL_RF_CURRENT_MSG);
  rf_event(ST25DV_EMUL_GPO1_RF_PUT_MSG_EN, ST25DV_EMUL_IT_RF_PUT_MSG);
  return 0;
}

int st25dv_emul_rf_get_msg(uint8_t *buf, size_t cap)
{
  size_t len;

  if (!(chip.mb_ctrl & ST25DV_EMUL_MB_EN) || !(chip.mb_ctrl & ST25DV_EMUL_HOST_PUT_MSG)) {
    return -ENOMSG;
  }
  len = (size_t)chip.mb_len + 1;
  memcpy(buf, chip.mailbox, MIN(len, cap));
  chip.mb_ctrl &= ~ST25DV_EMUL_HOST_PUT_MSG;
  rf_event(ST25DV_EMUL_GPO1_RF_GET_MSG_EN, ST25DV_EMUL_IT_RF_GET_MSG);
  return (int)len;
}

void st25dv_emul_enable_mailbox(void)
{
  chip.sys[ST25DV_EMUL_SYS_FTM] |= ST25DV_EMUL_FTM_MB_MODE;
  (void)mb_enable(true);
}

void st25dv_emul_set_uid(const uint8_t uid[8])
{
  memcpy(&chip.sys[ST25DV_EMUL_SYS_UID], uid, 8);
}

void st25dv_emul_set_ic_rev(uint8_t rev)
{
  chip.sys[ST25DV_EMUL_SYS_IC_REV] = rev;
}

void st25dv_emul_set_mem_size(uint16_t mem_size, uint8_t blk_size)
{
  size_t bytes = ((size_t)mem_size + 1) * ((size_t)blk_size + 1);

  chip.sys[ST25DV_EMUL_SYS_MEM_SIZE] = mem_size & 0xff;
  chip.sys[ST25DV_EMUL_SYS_MEM_SIZE + 1] = mem_size >> 8;
  chip.sys[ST25DV_EMUL_SYS_BLK_SIZE] = blk_size;
  chip.user_size = MIN(bytes, (size_t)ST25DV_EMUL_USER_MAX);
}

void st25dv_emul_set_nak(bool nak)
{
  chip.nak = nak;
}

void st25dv_emul_fail_next(int count)
{
  chip.fail_next = count;
}

void st25dv_emul_set_write_time_ms(uint32_t ms)
{
  chip.write_time_ms = ms;
}

uint8_t *st25dv_emul_user_mem(void)
{
  return chip.user;
}

uint8_t st25dv_emul_get_sys(uint16_t addr)
{
  if (addr >= ST25DV_EMUL_SYS_I2C_PWD && addr < ST25DV_EMUL_SYS_I2C_PWD + PWD_LEN) {
    return chip.password[addr - ST25DV_EMUL_SYS_I2C_PWD];
  }
  return addr < SYS_REG_COUNT ? chip.sys[addr] : 0xff;
}

void st25dv_emul_set_sys(uint16_t addr, uint8_t value)
{
  if (addr < SYS_REG_COUNT) {
    chip.sys[addr] = value;
  }
}

const uint8_t *st25dv_emul_mailbox(void)
{
  return chip.mailbox;
}

bool st25dv_emul_session_open(void)
{
  return chip.session;
}

void st25dv_emul_set_password(const uint8_t pwd[8])
{
  memcpy(chip.password, pwd, PWD_LEN);
}

void st25dv_emul_connect_gpo(const struct device *port, uint8_t pin)
{
  chip.gpo_port = port;
  chip.gpo_pin = pin;
  (void)gpio_emul_input_set(port, pin, 1); /* idle high (pulled up) */
}

int st25dv_emul_xfers(void)
{
  return chip.xfers;
}

const struct st25dv_emul_xfer *st25dv_emul_xfer(int index)
{
  __ASSERT(index >= 0 && index < MIN(chip.xfers, ST25DV_EMUL_LOG), "transfer %d not logged",
     index);
  return &chip.log[index];
}
