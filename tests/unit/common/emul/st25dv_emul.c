/*
 * ST25DV04KC emulator; both device selects share one chip state. Only user area 1 is modelled.
 * After an EEPROM write the chip NAKs for tW per 16-byte row. A GPO pulse is shorter than a tick,
 * so a connected gpio_emul input sees low then high within one call.
 * Not modelled: other RF commands, areas 2..4 and RF protection, LOCK_* effects, the mailbox
 * watchdog, RF_MNGT effects, RFSwitchOff/On, energy harvesting output, low-power-down pin.
 */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
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

	/* volatile */
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
	int gpo_pulses;
	const struct device *gpo_port;
	uint8_t gpo_pin;

	int xfers;
	struct st25dv_emul_xfer log[ST25DV_EMUL_LOG];
};

static struct st25dv_emul_data st25dv_data;

static void factory_eeprom(struct st25dv_emul_data *d)
{
	static const uint8_t uid[8] = {0x78, 0x56, 0x34, 0x12, 0x01, ST25DV_EMUL_IC_REF_04KC,
				       0x02, 0xe0};
	uint8_t enda = FACTORY_USER / 32 - 1;

	memset(d->user, 0, sizeof(d->user));
	d->user_size = FACTORY_USER;
	memset(d->sys, 0, sizeof(d->sys));
	d->sys[ST25DV_EMUL_SYS_GPO1] = ST25DV_EMUL_GPO1_GPO_EN | ST25DV_EMUL_GPO1_FIELD_CHANGE_EN;
	d->sys[ST25DV_EMUL_SYS_GPO2] = 0x0c;
	d->sys[ST25DV_EMUL_SYS_EH_MODE] = 0x01;
	d->sys[SYS_ENDA1] = enda;
	d->sys[SYS_ENDA2] = enda;
	d->sys[SYS_ENDA3] = enda;
	d->sys[ST25DV_EMUL_SYS_I2C_CFG] = 0x1a;
	d->sys[ST25DV_EMUL_SYS_MEM_SIZE] = FACTORY_USER / 4 - 1;
	d->sys[ST25DV_EMUL_SYS_MEM_SIZE + 1] = 0;
	d->sys[ST25DV_EMUL_SYS_BLK_SIZE] = 0x03;
	d->sys[ST25DV_EMUL_SYS_IC_REF] = ST25DV_EMUL_IC_REF_04KC;
	memcpy(&d->sys[ST25DV_EMUL_SYS_UID], uid, sizeof(uid));
	d->sys[ST25DV_EMUL_SYS_IC_REV] = ST25DV_EMUL_IC_REV;
	memset(d->password, 0, sizeof(d->password));
}

/* dynamic registers take their static images */
static void power_on(struct st25dv_emul_data *d, bool keep_eeprom)
{
	if (!keep_eeprom) {
		factory_eeprom(d);
	}
	d->gpo_ctrl = d->sys[ST25DV_EMUL_SYS_GPO1] & ST25DV_EMUL_GPO1_GPO_EN;
	d->eh_ctrl = ST25DV_EMUL_VCC_ON;
	if ((d->sys[ST25DV_EMUL_SYS_EH_MODE] & 0x01) == 0) {
		d->eh_ctrl |= ST25DV_EMUL_EH_EN | ST25DV_EMUL_EH_ON;
	}
	d->rf_mngt = d->sys[ST25DV_EMUL_SYS_RF_MNGT] & 0x03;
	d->session = false;
	d->it_sts = 0;
	d->mb_ctrl = 0;
	d->mb_len = 0;
	memset(d->mailbox, 0, sizeof(d->mailbox));
	d->ptr[0] = 0;
	d->ptr[1] = 0;
	d->busy_until = 0;
	d->field = false;
	d->nak = false;
	d->fail_next = 0;
	d->write_time_ms = ST25DV_EMUL_WRITE_TIME_MS;
	d->gpo_pulses = 0;
	d->gpo_port = NULL;
	d->xfers = 0;
	memset(d->log, 0, sizeof(d->log));
}

static void gpo_pulse(struct st25dv_emul_data *d)
{
	if (!(d->gpo_ctrl & ST25DV_EMUL_GPO1_GPO_EN)) {
		return;
	}
	d->gpo_pulses++;
	if (d->gpo_port != NULL) {
		/* pin may not be an input yet */
		(void)gpio_emul_input_set(d->gpo_port, d->gpo_pin, 0);
		(void)gpio_emul_input_set(d->gpo_port, d->gpo_pin, 1);
	}
}

static void rf_event(struct st25dv_emul_data *d, uint8_t gpo1_enable, uint8_t it_bit)
{
	if (!(d->sys[ST25DV_EMUL_SYS_GPO1] & gpo1_enable)) {
		return;
	}
	d->it_sts |= it_bit;
	gpo_pulse(d);
}

static bool mb_free(const struct st25dv_emul_data *d)
{
	return !(d->mb_ctrl & (ST25DV_EMUL_HOST_PUT_MSG | ST25DV_EMUL_RF_PUT_MSG));
}

static void mb_disable(struct st25dv_emul_data *d)
{
	d->mb_ctrl = 0;
	d->mb_len = 0;
}

static uint8_t read_sys(struct st25dv_emul_data *d, uint16_t a)
{
	if (a < SYS_REG_COUNT) {
		return d->sys[a];
	}
	if (a >= ST25DV_EMUL_SYS_I2C_PWD && a < ST25DV_EMUL_SYS_I2C_PWD + PWD_LEN) {
		return d->session ? d->password[a - ST25DV_EMUL_SYS_I2C_PWD] : 0xff;
	}
	return 0xff;
}

static uint8_t read_user(struct st25dv_emul_data *d, uint16_t a, bool *rf_msg_done)
{
	uint8_t v;

	if (a < d->user_size) {
		return d->user[a];
	}
	switch (a) {
	case ST25DV_EMUL_DYN_GPO_CTRL:
		return d->gpo_ctrl;
	case ST25DV_EMUL_DYN_EH_CTRL:
		return d->eh_ctrl | (d->field ? ST25DV_EMUL_FIELD_ON : 0);
	case ST25DV_EMUL_DYN_RF_MNGT:
		return d->rf_mngt;
	case ST25DV_EMUL_DYN_I2C_SSO:
		return d->session ? 0x01 : 0x00;
	case ST25DV_EMUL_DYN_IT_STS:
		v = d->it_sts;
		d->it_sts = 0;
		return v;
	case ST25DV_EMUL_DYN_MB_CTRL:
		return d->mb_ctrl;
	case ST25DV_EMUL_DYN_MB_LEN:
		return d->mb_len;
	default:
		break;
	}
	if (a >= ST25DV_EMUL_MAILBOX && a < ST25DV_EMUL_MAILBOX + ST25DV_EMUL_MB_SIZE) {
		uint16_t i = a - ST25DV_EMUL_MAILBOX;

		if (!(d->mb_ctrl & ST25DV_EMUL_MB_EN) || i > d->mb_len) {
			return 0xff;
		}
		if ((d->mb_ctrl & ST25DV_EMUL_RF_PUT_MSG) && i == d->mb_len) {
			*rf_msg_done = true;
		}
		return d->mailbox[i];
	}
	return 0xff;
}

static void program(struct st25dv_emul_data *d, int64_t now, int rows)
{
	d->busy_until = now + (int64_t)d->write_time_ms * rows;
	if (d->sys[ST25DV_EMUL_SYS_GPO2] & ST25DV_EMUL_GPO2_I2C_WRITE_EN) {
		gpo_pulse(d);
	}
}

static int write_password(struct st25dv_emul_data *d, const uint8_t *b, size_t n, int64_t now)
{
	if (n != PWD_CMD_LEN) {
		return -EIO;
	}
	if (b[PWD_LEN] == PWD_PRESENT) {
		if (memcmp(b, &b[PWD_LEN + 1], PWD_LEN) == 0) {
			d->session = memcmp(b, d->password, PWD_LEN) == 0;
		}
		return 0;
	}
	if (b[PWD_LEN] == PWD_WRITE) {
		if (!d->session || (d->mb_ctrl & ST25DV_EMUL_MB_EN)) {
			return -EIO;
		}
		if (memcmp(b, &b[PWD_LEN + 1], PWD_LEN) == 0) {
			memcpy(d->password, b, PWD_LEN);
			program(d, now, 1);
		}
		return 0;
	}
	return -EIO;
}

static int write_sys(struct st25dv_emul_data *d, uint16_t a, const uint8_t *b, size_t n,
		     int64_t now)
{
	if (a == ST25DV_EMUL_SYS_I2C_PWD) {
		return write_password(d, b, n, now);
	}
	if (n != 1 || !d->session || a >= SYS_RO_FIRST) {
		return -EIO;
	}
	d->sys[a] = b[0];
	switch (a) {
	case ST25DV_EMUL_SYS_GPO1:
		d->gpo_ctrl = b[0] & ST25DV_EMUL_GPO1_GPO_EN;
		break;
	case ST25DV_EMUL_SYS_EH_MODE:
		if ((b[0] & 0x01) == 0) {
			d->eh_ctrl |= ST25DV_EMUL_EH_EN | ST25DV_EMUL_EH_ON;
		}
		break;
	case ST25DV_EMUL_SYS_RF_MNGT:
		d->rf_mngt = b[0] & 0x03;
		break;
	case ST25DV_EMUL_SYS_FTM:
		if (!(b[0] & ST25DV_EMUL_FTM_MB_MODE)) {
			mb_disable(d);
		}
		break;
	default:
		break;
	}
	program(d, now, 1);
	return 0;
}

static int write_dyn(struct st25dv_emul_data *d, uint16_t a, uint8_t v)
{
	switch (a) {
	case ST25DV_EMUL_DYN_GPO_CTRL:
		d->gpo_ctrl = v & ST25DV_EMUL_GPO1_GPO_EN;
		return 0;
	case ST25DV_EMUL_DYN_EH_CTRL:
		d->eh_ctrl &= ~(ST25DV_EMUL_EH_EN | ST25DV_EMUL_EH_ON);
		if (v & ST25DV_EMUL_EH_EN) {
			d->eh_ctrl |= ST25DV_EMUL_EH_EN | ST25DV_EMUL_EH_ON;
		}
		return 0;
	case ST25DV_EMUL_DYN_RF_MNGT:
		d->rf_mngt = v & 0x03;
		return 0;
	case ST25DV_EMUL_DYN_MB_CTRL:
		if (!(v & ST25DV_EMUL_MB_EN)) {
			mb_disable(d);
		} else if ((d->sys[ST25DV_EMUL_SYS_FTM] & ST25DV_EMUL_FTM_MB_MODE) &&
			   !(d->mb_ctrl & ST25DV_EMUL_MB_EN)) {
			d->mb_ctrl = ST25DV_EMUL_MB_EN;
			d->mb_len = 0;
		}
		return 0;
	default:
		return -EIO;
	}
}

static int write_user(struct st25dv_emul_data *d, uint16_t a, const uint8_t *b, size_t n,
		      int64_t now)
{
	if (a < d->user_size) {
		uint8_t prot = d->sys[ST25DV_EMUL_SYS_I2CSS] & 0x03;

		if ((d->mb_ctrl & ST25DV_EMUL_MB_EN) || n > SEQ_WRITE_MAX ||
		    a + n > d->user_size || ((prot == 1 || prot == 3) && !d->session)) {
			return -EIO;
		}
		memcpy(&d->user[a], b, n);
		program(d, now, (int)((a + n - 1) / ROW_SIZE - a / ROW_SIZE + 1));
		return 0;
	}
	if (a >= ST25DV_EMUL_DYN_GPO_CTRL && a <= ST25DV_EMUL_DYN_MB_LEN) {
		return n == 1 ? write_dyn(d, a, b[0]) : -EIO;
	}
	if (a == ST25DV_EMUL_MAILBOX) {
		if (!(d->mb_ctrl & ST25DV_EMUL_MB_EN) || !mb_free(d) || n > ST25DV_EMUL_MB_SIZE) {
			return -EIO;
		}
		memcpy(d->mailbox, b, n);
		d->mb_len = (uint8_t)(n - 1);
		d->mb_ctrl = (d->mb_ctrl & (ST25DV_EMUL_MB_EN | ST25DV_EMUL_HOST_MISS_MSG |
					    ST25DV_EMUL_RF_MISS_MSG)) |
			     ST25DV_EMUL_HOST_PUT_MSG | ST25DV_EMUL_HOST_CURRENT_MSG;
		return 0;
	}
	return -EIO;
}

static int st25dv_emul_transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs,
				int addr)
{
	struct st25dv_emul_data *d = target->data;
	int64_t now = k_uptime_get();
	bool sys = addr == ST25DV_EMUL_ADDR_SYS;
	struct st25dv_emul_xfer *log = NULL;
	uint8_t wbuf[2 + SEQ_WRITE_MAX + 1];
	size_t wlen = 0;      /* address bytes included */
	bool wtoo_long = false;
	bool rf_msg_done = false;
	int ret = 0;

	if (d->xfers < ST25DV_EMUL_LOG) {
		log = &d->log[d->xfers];
		memset(log, 0, sizeof(*log));
		log->addr = (uint8_t)addr;
	}
	d->xfers++;

	if (d->nak || now < d->busy_until) {
		ret = -EIO;
		goto out;
	}
	if (d->fail_next > 0) {
		d->fail_next--;
		ret = -EIO;
		goto out;
	}

	for (int i = 0; i < num_msgs; i++) {
		struct i2c_msg *m = &msgs[i];

		if (m->flags & I2C_MSG_READ) {
			uint16_t *p = &d->ptr[sys ? 1 : 0];

			if (wlen == 1) {
				ret = -EIO; /* half an address */
				goto out;
			}
			if (wlen > 2) {
				/* restart after data: no STOP, nothing programmed */
				wlen = 2;
			}
			for (uint32_t j = 0; j < m->len; j++) {
				m->buf[j] = sys ? read_sys(d, *p) : read_user(d, *p, &rf_msg_done);
				(*p)++;
			}
			if (log) {
				log->rd_len += m->len;
			}
			continue;
		}
		for (uint32_t j = 0; j < m->len; j++) {
			if (wlen < sizeof(wbuf)) {
				wbuf[wlen] = m->buf[j];
			} else {
				wtoo_long = true;
			}
			if (log && wlen < ST25DV_EMUL_LOG_BYTES) {
				log->wr[wlen] = m->buf[j];
			}
			wlen++;
			if (wlen == 2) {
				d->ptr[sys ? 1 : 0] = (uint16_t)(wbuf[0] << 8 | wbuf[1]);
			}
		}
		if (log) {
			log->wr_len = (uint16_t)MIN(wlen, UINT16_MAX);
		}
	}

	/* STOP */
	if (wlen == 1) {
		ret = -EIO;
	} else if (wlen > 2) {
		uint16_t a = (uint16_t)(wbuf[0] << 8 | wbuf[1]);
		size_t n = wlen - 2;

		if (wtoo_long) {
			ret = -EIO;
		} else {
			ret = sys ? write_sys(d, a, &wbuf[2], n, now)
				  : write_user(d, a, &wbuf[2], n, now);
		}
		if (ret == 0) {
			d->ptr[sys ? 1 : 0] = (uint16_t)(a + n);
		}
	}
	if (rf_msg_done) {
		/* host read the RF message to its end: mailbox free */
		d->mb_ctrl &= ~ST25DV_EMUL_RF_PUT_MSG;
	}

out:
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
	power_on(&st25dv_data, false);
	return 0;
}

EMUL_DT_DEFINE(DT_NODELABEL(st25dv), st25dv_emul_init, &st25dv_data, NULL, &st25dv_emul_bus_api,
	       NULL);
EMUL_DT_DEFINE(DT_NODELABEL(st25dv_sys), st25dv_emul_init, &st25dv_data, NULL,
	       &st25dv_emul_bus_api, NULL);

void st25dv_emul_rf_field(const struct emul *target, bool on)
{
	struct st25dv_emul_data *d = target->data;

	if (d->field == on) {
		return;
	}
	d->field = on;
	rf_event(d, ST25DV_EMUL_GPO1_FIELD_CHANGE_EN,
		 on ? ST25DV_EMUL_IT_FIELD_RISING : ST25DV_EMUL_IT_FIELD_FALLING);
}

int st25dv_emul_rf_set_mb_en(const struct emul *target, bool on)
{
	struct st25dv_emul_data *d = target->data;

	if (!on) {
		mb_disable(d);
		return 0;
	}
	if (!(d->sys[ST25DV_EMUL_SYS_FTM] & ST25DV_EMUL_FTM_MB_MODE)) {
		return -EACCES;
	}
	if (!(d->mb_ctrl & ST25DV_EMUL_MB_EN)) {
		d->mb_ctrl = ST25DV_EMUL_MB_EN;
		d->mb_len = 0;
	}
	return 0;
}

int st25dv_emul_rf_put_msg(const struct emul *target, const uint8_t *data, size_t len)
{
	struct st25dv_emul_data *d = target->data;

	if (len == 0 || len > ST25DV_EMUL_MB_SIZE) {
		return -EINVAL;
	}
	if (!(d->mb_ctrl & ST25DV_EMUL_MB_EN)) {
		return -EACCES;
	}
	if (!mb_free(d)) {
		return -EBUSY;
	}
	memcpy(d->mailbox, data, len);
	d->mb_len = (uint8_t)(len - 1);
	d->mb_ctrl = (d->mb_ctrl & (ST25DV_EMUL_MB_EN | ST25DV_EMUL_HOST_MISS_MSG |
				    ST25DV_EMUL_RF_MISS_MSG)) |
		     ST25DV_EMUL_RF_PUT_MSG | ST25DV_EMUL_RF_CURRENT_MSG;
	rf_event(d, ST25DV_EMUL_GPO1_RF_PUT_MSG_EN, ST25DV_EMUL_IT_RF_PUT_MSG);
	return 0;
}

int st25dv_emul_rf_get_msg(const struct emul *target, uint8_t *buf, size_t cap)
{
	struct st25dv_emul_data *d = target->data;
	size_t n;

	if (!(d->mb_ctrl & ST25DV_EMUL_MB_EN) || !(d->mb_ctrl & ST25DV_EMUL_HOST_PUT_MSG)) {
		return -ENOMSG;
	}
	n = (size_t)d->mb_len + 1;
	memcpy(buf, d->mailbox, MIN(n, cap));
	d->mb_ctrl &= ~ST25DV_EMUL_HOST_PUT_MSG;
	rf_event(d, ST25DV_EMUL_GPO1_RF_GET_MSG_EN, ST25DV_EMUL_IT_RF_GET_MSG);
	return (int)n;
}

void st25dv_emul_enable_mailbox(const struct emul *target)
{
	struct st25dv_emul_data *d = target->data;

	d->sys[ST25DV_EMUL_SYS_FTM] |= ST25DV_EMUL_FTM_MB_MODE;
	(void)st25dv_emul_rf_set_mb_en(target, true);
}

void st25dv_emul_set_ic_ref(const struct emul *target, uint8_t v)
{
	((struct st25dv_emul_data *)target->data)->sys[ST25DV_EMUL_SYS_IC_REF] = v;
}

void st25dv_emul_set_uid(const struct emul *target, const uint8_t uid[8])
{
	memcpy(&((struct st25dv_emul_data *)target->data)->sys[ST25DV_EMUL_SYS_UID], uid, 8);
}

void st25dv_emul_set_ic_rev(const struct emul *target, uint8_t v)
{
	((struct st25dv_emul_data *)target->data)->sys[ST25DV_EMUL_SYS_IC_REV] = v;
}

void st25dv_emul_set_mem_size(const struct emul *target, uint16_t mem_size, uint8_t blk_size)
{
	struct st25dv_emul_data *d = target->data;
	size_t bytes = ((size_t)mem_size + 1) * ((size_t)blk_size + 1);

	d->sys[ST25DV_EMUL_SYS_MEM_SIZE] = mem_size & 0xff;
	d->sys[ST25DV_EMUL_SYS_MEM_SIZE + 1] = mem_size >> 8;
	d->sys[ST25DV_EMUL_SYS_BLK_SIZE] = blk_size;
	d->user_size = MIN(bytes, (size_t)ST25DV_EMUL_USER_MAX);
}

void st25dv_emul_set_nak(const struct emul *target, bool nak)
{
	((struct st25dv_emul_data *)target->data)->nak = nak;
}

void st25dv_emul_fail_next(const struct emul *target, int n)
{
	((struct st25dv_emul_data *)target->data)->fail_next = n;
}

void st25dv_emul_set_write_time_ms(const struct emul *target, uint32_t ms)
{
	((struct st25dv_emul_data *)target->data)->write_time_ms = ms;
}

uint8_t *st25dv_emul_user_mem(const struct emul *target)
{
	return ((struct st25dv_emul_data *)target->data)->user;
}

size_t st25dv_emul_user_size(const struct emul *target)
{
	return ((struct st25dv_emul_data *)target->data)->user_size;
}

uint8_t st25dv_emul_get_sys(const struct emul *target, uint16_t addr)
{
	struct st25dv_emul_data *d = target->data;

	if (addr >= ST25DV_EMUL_SYS_I2C_PWD && addr < ST25DV_EMUL_SYS_I2C_PWD + PWD_LEN) {
		return d->password[addr - ST25DV_EMUL_SYS_I2C_PWD];
	}
	return addr < SYS_REG_COUNT ? d->sys[addr] : 0xff;
}

void st25dv_emul_set_sys(const struct emul *target, uint16_t addr, uint8_t v)
{
	struct st25dv_emul_data *d = target->data;

	if (addr < SYS_REG_COUNT) {
		d->sys[addr] = v;
	}
}

uint8_t st25dv_emul_get_dyn(const struct emul *target, uint16_t addr)
{
	struct st25dv_emul_data *d = target->data;

	switch (addr) {
	case ST25DV_EMUL_DYN_GPO_CTRL:
		return d->gpo_ctrl;
	case ST25DV_EMUL_DYN_EH_CTRL:
		return d->eh_ctrl | (d->field ? ST25DV_EMUL_FIELD_ON : 0);
	case ST25DV_EMUL_DYN_RF_MNGT:
		return d->rf_mngt;
	case ST25DV_EMUL_DYN_I2C_SSO:
		return d->session ? 0x01 : 0x00;
	case ST25DV_EMUL_DYN_IT_STS:
		return d->it_sts;
	case ST25DV_EMUL_DYN_MB_CTRL:
		return d->mb_ctrl;
	case ST25DV_EMUL_DYN_MB_LEN:
		return d->mb_len;
	default:
		return 0;
	}
}

const uint8_t *st25dv_emul_mailbox(const struct emul *target)
{
	return ((struct st25dv_emul_data *)target->data)->mailbox;
}

bool st25dv_emul_session_open(const struct emul *target)
{
	return ((struct st25dv_emul_data *)target->data)->session;
}

void st25dv_emul_set_password(const struct emul *target, const uint8_t pwd[8])
{
	memcpy(((struct st25dv_emul_data *)target->data)->password, pwd, PWD_LEN);
}

int st25dv_emul_gpo_pulses(const struct emul *target)
{
	return ((struct st25dv_emul_data *)target->data)->gpo_pulses;
}

void st25dv_emul_connect_gpo(const struct emul *target, const struct device *port, uint8_t pin)
{
	struct st25dv_emul_data *d = target->data;

	d->gpo_port = port;
	d->gpo_pin = pin;
	(void)gpio_emul_input_set(port, pin, 1); /* idle high (pulled up) */
}

int st25dv_emul_xfers(const struct emul *target)
{
	return ((struct st25dv_emul_data *)target->data)->xfers;
}

const struct st25dv_emul_xfer *st25dv_emul_xfer(const struct emul *target, int i)
{
	struct st25dv_emul_data *d = target->data;

	__ASSERT(i >= 0 && i < MIN(d->xfers, ST25DV_EMUL_LOG), "transfer %d not logged", i);
	return &d->log[i];
}

static void st25dv_emul_reset(bool keep_media)
{
	power_on(&st25dv_data, keep_media);
}

G1_UNIT_RESET_HOOK(st25dv_emul_reset);
