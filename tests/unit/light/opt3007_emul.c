/*
 * OPT3007 (OPT3001-compatible) emulator. Time is k_uptime_get() at each transfer; no threads or
 * timers. A conversion gives the result the test set, else the reading at 0 lux. Not modelled:
 * light levels, limit comparison (FH, FL, INT pin), noise, bus timeout, general-call reset.
 */
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

#include "opt3007_emul.h"

#define CFG_RN_SHIFT 12
#define CFG_CT       BIT(11)
#define CFG_M_SHIFT  9
#define CFG_M_MASK   (BIT(10) | BIT(9))
#define CFG_OVF      BIT(8)
#define CFG_CRF      BIT(7)
#define CFG_FH       BIT(6)
#define CFG_FL       BIT(5)
#define CFG_L        BIT(4)
#define CFG_ME       BIT(2)
#define CFG_RO_MASK  (CFG_OVF | CFG_CRF | CFG_FH | CFG_FL | CFG_L)

#define RN_AUTO      0xc
#define M_SHUTDOWN   0
#define M_SINGLE     1

struct opt3007_emul_data opt3007_emul;

static void power_on(struct opt3007_emul_data *data)
{
  memset(data, 0, sizeof(*data));
  data->config = OPT3007_EMUL_CONFIG_RESET;
  data->high_limit = 0xbfff;
  data->mfg_id = OPT3007_EMUL_MFG_ID;
  data->device_id = OPT3007_EMUL_DEVICE_ID;
  data->conv_end = -1;
}

static unsigned int mode(const struct opt3007_emul_data *data)
{
  return (data->config & CFG_M_MASK) >> CFG_M_SHIFT;
}

static unsigned int range(const struct opt3007_emul_data *data)
{
  return data->config >> CFG_RN_SHIFT;
}

static int64_t conv_ms(const struct opt3007_emul_data *data)
{
  return (data->config & CFG_CT) ? OPT3007_EMUL_CONV_800_MS : OPT3007_EMUL_CONV_100_MS;
}

/* at 0 lux (datasheet Equation 2) the mantissa is 0 and only a manual range's exponent shows */
static uint16_t conversion(const struct opt3007_emul_data *data)
{
  if (data->fixed_result) {
    return data->fixed;
  }
  return range(data) < RN_AUTO && !(data->config & CFG_ME) ? range(data) << 12 : 0;
}

/* latch conversions that ended by now */
static void advance(struct opt3007_emul_data *data, int64_t now)
{
  if (data->conv_end < 0 || now < data->conv_end || mode(data) == M_SHUTDOWN) {
    return;
  }
  data->result = conversion(data);
  data->config = (data->config | CFG_CRF) & ~CFG_OVF;
  if (mode(data) == M_SINGLE) {
    data->config &= ~CFG_M_MASK;
    data->conv_end = -1;
  } else {
    /* continuous: end of the conversion now running */
    data->conv_end += ((now - data->conv_end) / conv_ms(data) + 1) * conv_ms(data);
  }
}

static uint16_t *reg_of(struct opt3007_emul_data *data, uint8_t reg)
{
  switch (reg) {
  case OPT3007_EMUL_REG_RESULT:
    return &data->result;
  case OPT3007_EMUL_REG_CONFIG:
    return &data->config;
  case OPT3007_EMUL_REG_LOW_LIMIT:
    return &data->low_limit;
  case OPT3007_EMUL_REG_HIGH_LIMIT:
    return &data->high_limit;
  case OPT3007_EMUL_REG_MFG_ID:
    return &data->mfg_id;
  case OPT3007_EMUL_REG_DEVICE_ID:
    return &data->device_id;
  default:
    return NULL;
  }
}

static uint16_t reg_read(struct opt3007_emul_data *data, uint8_t reg, int64_t now)
{
  uint16_t *value = reg_of(data, reg);
  uint16_t result;

  advance(data, now);
  result = value ? *value : 0;
  if (reg == OPT3007_EMUL_REG_CONFIG) {
    data->config &= ~CFG_CRF;
  }
  return result;
}

static void reg_write(struct opt3007_emul_data *data, uint8_t reg, uint16_t val, int64_t now)
{
  if (data->writes < OPT3007_EMUL_WRITE_LOG) {
    data->write_reg[data->writes] = reg;
    data->write_val[data->writes] = val;
  }
  data->writes++;
  advance(data, now);

  if (reg == OPT3007_EMUL_REG_CONFIG) {
    data->config = (data->config & CFG_RO_MASK) | (val & ~CFG_RO_MASK);
    if (mode(data) == M_SHUTDOWN) {
      data->conv_end = -1;
    } else {
      data->config &= ~CFG_CRF;
      data->conv_end = now + conv_ms(data) +
           (range(data) >= RN_AUTO ? OPT3007_EMUL_AUTO_FIRST_MS : 0);
    }
  } else if (reg == OPT3007_EMUL_REG_LOW_LIMIT || reg == OPT3007_EMUL_REG_HIGH_LIMIT) {
    /* the result and the IDs are read-only */
    *reg_of(data, reg) = val;
  }
}

static int opt3007_emul_transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs,
         int addr)
{
  struct opt3007_emul_data *data = target->data;
  int64_t now = k_uptime_get();

  ARG_UNUSED(addr);
  data->transfers++;
  if (data->nak) {
    return -EIO;
  }
  for (int index = 0; index < num_msgs; index++) {
    struct i2c_msg *msg = &msgs[index];

    if (msg->flags & I2C_MSG_READ) {
      uint8_t value[2];

      /* the register MSB first, then 0xff */
      sys_put_be16(reg_read(data, data->pointer, now), value);
      for (uint32_t pos = 0; pos < msg->len; pos++) {
        msg->buf[pos] = pos < sizeof(value) ? value[pos] : 0xff;
      }
      continue;
    }
    if (msg->len == 0) {
      return -EIO;
    }
    data->pointer = msg->buf[0];
    if (msg->len >= 3) {
      reg_write(data, data->pointer, sys_get_be16(&msg->buf[1]), now);
    }
  }
  return 0;
}

static const struct i2c_emul_api opt3007_emul_bus_api = {
  .transfer = opt3007_emul_transfer,
};

static int opt3007_emul_init(const struct emul *target, const struct device *parent)
{
  ARG_UNUSED(parent);
  power_on(target->data);
  return 0;
}

EMUL_DT_DEFINE(DT_NODELABEL(opt3007), opt3007_emul_init, &opt3007_emul, NULL,
         &opt3007_emul_bus_api, NULL);

void opt3007_emul_set_result(uint16_t result)
{
  /* earlier conversions keep the old result */
  advance(&opt3007_emul, k_uptime_get());
  opt3007_emul.fixed = result;
  opt3007_emul.fixed_result = true;
}

static void opt3007_emul_reset(bool keep_media)
{
  /* a reboot power-cycles the sensor too */
  ARG_UNUSED(keep_media);
  power_on(&opt3007_emul);
}

G1_UNIT_RESET_HOOK(opt3007_emul_reset);
