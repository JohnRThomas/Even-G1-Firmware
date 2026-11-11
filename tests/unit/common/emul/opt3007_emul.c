/*
 * OPT3007 (OPT3001-compatible) emulator. Time is k_uptime_get() at each transfer; no threads or
 * timers. Not modelled: limit comparison (FH, FL, INT pin), noise, bus timeout, general-call reset.
 */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

#include "harness.h"
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

#define RESULT_MAX_EXP      11
#define RESULT_MANTISSA_MAX 0xfff

struct opt3007_emul_data {
	uint16_t result;
	uint16_t config;
	uint16_t low_limit;
	uint16_t high_limit;
	uint16_t mfg_id;
	uint16_t device_id;
	uint8_t pointer;

	uint32_t centilux;     /* 0.01 lux */
	bool fixed_result;
	uint16_t fixed;
	bool nak;

	int64_t conv_end;      /* uptime ms, -1: none */

	int writes;
	uint8_t write_reg[OPT3007_EMUL_WRITE_LOG];
	uint16_t write_val[OPT3007_EMUL_WRITE_LOG];
	int transfers;
};

static struct opt3007_emul_data opt3007_data;

static void power_on(struct opt3007_emul_data *d)
{
	memset(d, 0, sizeof(*d));
	d->config = OPT3007_EMUL_CONFIG_RESET;
	d->high_limit = 0xbfff;
	d->mfg_id = OPT3007_EMUL_MFG_ID;
	d->device_id = OPT3007_EMUL_DEVICE_ID;
	d->conv_end = -1;
}

static unsigned int mode(const struct opt3007_emul_data *d)
{
	return (d->config & CFG_M_MASK) >> CFG_M_SHIFT;
}

static unsigned int range(const struct opt3007_emul_data *d)
{
	return d->config >> CFG_RN_SHIFT;
}

static int64_t conv_ms(const struct opt3007_emul_data *d)
{
	return (d->config & CFG_CT) ? OPT3007_EMUL_CONV_800_MS : OPT3007_EMUL_CONV_100_MS;
}

/* datasheet Equation 2: lux = 0.01 x 2^E x R */
static uint16_t encode(const struct opt3007_emul_data *d, bool *ovf)
{
	unsigned int rn = range(d);
	uint32_t r;

	*ovf = false;
	if (d->fixed_result) {
		return d->fixed;
	}
	if (rn >= RN_AUTO) {
		for (unsigned int e = 0; e <= RESULT_MAX_EXP; e++) {
			if ((d->centilux >> e) <= RESULT_MANTISSA_MAX) {
				return (uint16_t)(e << 12 | (d->centilux >> e));
			}
		}
		*ovf = true;
		return (uint16_t)(RESULT_MAX_EXP << 12 | RESULT_MANTISSA_MAX);
	}
	r = d->centilux >> rn;
	if (r > RESULT_MANTISSA_MAX) {
		r = RESULT_MANTISSA_MAX;
		*ovf = true;
	}
	return (uint16_t)(((d->config & CFG_ME) ? 0 : rn) << 12 | r);
}

/* latch conversions that ended by now */
static void advance(struct opt3007_emul_data *d, int64_t now)
{
	bool ovf;

	if (d->conv_end < 0 || now < d->conv_end || mode(d) == M_SHUTDOWN) {
		return;
	}
	d->result = encode(d, &ovf);
	d->config |= CFG_CRF;
	if (ovf) {
		d->config |= CFG_OVF;
	} else {
		d->config &= ~CFG_OVF;
	}
	if (mode(d) == M_SINGLE) {
		d->config &= ~CFG_M_MASK;
		d->conv_end = -1;
	} else {
		/* continuous: end of the conversion now running */
		d->conv_end += ((now - d->conv_end) / conv_ms(d) + 1) * conv_ms(d);
	}
}

static uint16_t reg_read(struct opt3007_emul_data *d, uint8_t reg, int64_t now)
{
	uint16_t v;

	advance(d, now);
	switch (reg) {
	case OPT3007_EMUL_REG_RESULT:
		return d->result;
	case OPT3007_EMUL_REG_CONFIG:
		v = d->config;
		d->config &= ~CFG_CRF;
		return v;
	case OPT3007_EMUL_REG_LOW_LIMIT:
		return d->low_limit;
	case OPT3007_EMUL_REG_HIGH_LIMIT:
		return d->high_limit;
	case OPT3007_EMUL_REG_MFG_ID:
		return d->mfg_id;
	case OPT3007_EMUL_REG_DEVICE_ID:
		return d->device_id;
	default:
		return 0;
	}
}

static void reg_write(struct opt3007_emul_data *d, uint8_t reg, uint16_t val, int64_t now)
{
	if (d->writes < OPT3007_EMUL_WRITE_LOG) {
		d->write_reg[d->writes] = reg;
		d->write_val[d->writes] = val;
	}
	d->writes++;
	advance(d, now);

	switch (reg) {
	case OPT3007_EMUL_REG_CONFIG:
		d->config = (d->config & CFG_RO_MASK) | (val & ~CFG_RO_MASK);
		if (mode(d) == M_SHUTDOWN) {
			d->conv_end = -1;
		} else {
			d->config &= ~CFG_CRF;
			d->conv_end = now + conv_ms(d) +
				      (range(d) >= RN_AUTO ? OPT3007_EMUL_AUTO_FIRST_MS : 0);
		}
		break;
	case OPT3007_EMUL_REG_LOW_LIMIT:
		d->low_limit = val;
		break;
	case OPT3007_EMUL_REG_HIGH_LIMIT:
		d->high_limit = val;
		break;
	default:
		/* result and IDs are read-only */
		break;
	}
}

static int opt3007_emul_transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs,
				 int addr)
{
	struct opt3007_emul_data *d = target->data;
	int64_t now = k_uptime_get();

	ARG_UNUSED(addr);
	d->transfers++;
	if (d->nak) {
		return -EIO;
	}
	for (int i = 0; i < num_msgs; i++) {
		struct i2c_msg *m = &msgs[i];

		if (m->flags & I2C_MSG_READ) {
			uint16_t v = reg_read(d, d->pointer, now);

			for (uint32_t j = 0; j < m->len; j++) {
				m->buf[j] = j == 0 ? v >> 8 : j == 1 ? v & 0xff : 0xff;
			}
			continue;
		}
		if (m->len == 0) {
			return -EIO;
		}
		d->pointer = m->buf[0];
		if (m->len >= 3) {
			reg_write(d, d->pointer, (uint16_t)(m->buf[1] << 8 | m->buf[2]), now);
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

EMUL_DT_DEFINE(DT_NODELABEL(opt3007), opt3007_emul_init, &opt3007_data, NULL,
	       &opt3007_emul_bus_api, NULL);

void opt3007_emul_set_lux_centi(const struct emul *target, uint32_t centilux)
{
	struct opt3007_emul_data *d = target->data;

	/* earlier conversions saw the old light */
	advance(d, k_uptime_get());
	d->centilux = centilux;
	d->fixed_result = false;
}

void opt3007_emul_set_result(const struct emul *target, uint16_t result)
{
	struct opt3007_emul_data *d = target->data;

	advance(d, k_uptime_get());
	d->fixed = result;
	d->fixed_result = true;
}

void opt3007_emul_set_ids(const struct emul *target, uint16_t mfg_id, uint16_t device_id)
{
	struct opt3007_emul_data *d = target->data;

	d->mfg_id = mfg_id;
	d->device_id = device_id;
}

void opt3007_emul_set_nak(const struct emul *target, bool nak)
{
	struct opt3007_emul_data *d = target->data;

	d->nak = nak;
}

uint16_t opt3007_emul_get_reg(const struct emul *target, uint8_t reg)
{
	struct opt3007_emul_data *d = target->data;

	advance(d, k_uptime_get());
	switch (reg) {
	case OPT3007_EMUL_REG_RESULT:
		return d->result;
	case OPT3007_EMUL_REG_CONFIG:
		return d->config;
	case OPT3007_EMUL_REG_LOW_LIMIT:
		return d->low_limit;
	case OPT3007_EMUL_REG_HIGH_LIMIT:
		return d->high_limit;
	case OPT3007_EMUL_REG_MFG_ID:
		return d->mfg_id;
	case OPT3007_EMUL_REG_DEVICE_ID:
		return d->device_id;
	default:
		return 0;
	}
}

void opt3007_emul_set_reg(const struct emul *target, uint8_t reg, uint16_t val)
{
	struct opt3007_emul_data *d = target->data;

	switch (reg) {
	case OPT3007_EMUL_REG_RESULT:
		d->result = val;
		break;
	case OPT3007_EMUL_REG_CONFIG:
		d->config = val;
		break;
	case OPT3007_EMUL_REG_LOW_LIMIT:
		d->low_limit = val;
		break;
	case OPT3007_EMUL_REG_HIGH_LIMIT:
		d->high_limit = val;
		break;
	case OPT3007_EMUL_REG_MFG_ID:
		d->mfg_id = val;
		break;
	case OPT3007_EMUL_REG_DEVICE_ID:
		d->device_id = val;
		break;
	default:
		break;
	}
}

int opt3007_emul_writes(const struct emul *target)
{
	return ((struct opt3007_emul_data *)target->data)->writes;
}

uint8_t opt3007_emul_write_reg(const struct emul *target, int i)
{
	struct opt3007_emul_data *d = target->data;

	__ASSERT(i >= 0 && i < MIN(d->writes, OPT3007_EMUL_WRITE_LOG), "write %d not logged", i);
	return d->write_reg[i];
}

uint16_t opt3007_emul_write_val(const struct emul *target, int i)
{
	struct opt3007_emul_data *d = target->data;

	__ASSERT(i >= 0 && i < MIN(d->writes, OPT3007_EMUL_WRITE_LOG), "write %d not logged", i);
	return d->write_val[i];
}

int opt3007_emul_transfers(const struct emul *target)
{
	return ((struct opt3007_emul_data *)target->data)->transfers;
}

static void opt3007_emul_reset(bool keep_media)
{
	/* a reboot power-cycles the sensor too */
	ARG_UNUSED(keep_media);
	power_on(&opt3007_data);
}

G1_UNIT_RESET_HOOK(opt3007_emul_reset);
