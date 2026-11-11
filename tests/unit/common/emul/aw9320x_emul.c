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
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>

#include "aw9320x_emul.h"
#include "harness.h"

#define SFR_BASE   0x4000
#define SFR_WORDS  (0x1000 / 4)
#define SYS_BASE   0xff00
#define SYS_WORDS  (0x100 / 4)

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

struct aw9320x_emul_data {
	uint8_t sram[AW9320X_EMUL_SRAM_SIZE];
	uint32_t sfr[SFR_WORDS];
	uint32_t sys[SYS_WORDS];
	uint8_t flash[AW9320X_EMUL_FLASH_SIZE];
	uint32_t irqsrc;
	uint32_t chip_id;
	uint32_t fw_version;
	uint16_t pointer;

	enum aw9320x_emul_state state;
	int64_t init_due;        /* -1: none */
	uint32_t mode;
	bool host_wake;          /* since entering deep sleep */
	bool touched;
	int32_t touch_diff;
	bool int_low;

	bool nak;
	int nak_remaining;
	bool nak_after_armed;
	int nak_after_cmds;
	int nak_after_n;
	int fail_cmd;            /* -1: none */
	bool ack_irq;

	int transfers;
	int writes;
	uint16_t write_addr[AW9320X_EMUL_WRITE_LOG];
	uint32_t write_val[AW9320X_EMUL_WRITE_LOG];
	int cmds_done;
	struct aw9320x_emul_cmd cmd_log[AW9320X_EMUL_CMD_LOG];
	int isp_ops[256];
	int soft_resets;
};

static struct aw9320x_emul_data aw9320x_data;

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint16_t be16(const uint8_t *p)
{
	return (uint16_t)(p[0] << 8 | p[1]);
}

static void put_be32(uint8_t *p, uint32_t v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

static void put_be16(uint8_t *p, uint16_t v)
{
	p[0] = v >> 8;
	p[1] = v;
}

/* no side effects (IRQSRC not cleared) */
static uint32_t value_of(const struct aw9320x_emul_data *d, uint16_t addr)
{
	addr &= ~3u;
	if (addr == AW9320X_EMUL_REG_IRQSRC) {
		return d->irqsrc;
	}
	if (addr == AW9320X_EMUL_REG_CHIPID) {
		return d->chip_id;
	}
	if (addr == AW9320X_EMUL_REG_ISPGO) {
		return 0; /* ISP operations complete at once */
	}
	if (addr == AW9320X_EMUL_REG_DIFF_CH0 && d->state == AW9320X_EMUL_APP) {
		return d->touched ? (uint32_t)d->touch_diff : 0;
	}
	if (addr < AW9320X_EMUL_SRAM_SIZE) {
		return be32(&d->sram[addr]);
	}
	if (addr >= SFR_BASE && addr < SFR_BASE + SFR_WORDS * 4) {
		return d->sfr[(addr - SFR_BASE) / 4];
	}
	if (addr >= SYS_BASE) {
		return d->sys[(addr - SYS_BASE) / 4];
	}
	return 0;
}

static void store(struct aw9320x_emul_data *d, uint16_t addr, uint32_t val)
{
	addr &= ~3u;
	if (addr < AW9320X_EMUL_SRAM_SIZE) {
		put_be32(&d->sram[addr], val);
	} else if (addr >= SFR_BASE && addr < SFR_BASE + SFR_WORDS * 4) {
		d->sfr[(addr - SFR_BASE) / 4] = val;
	} else if (addr >= SYS_BASE) {
		d->sys[(addr - SYS_BASE) / 4] = val;
	}
}

static void drive_int(struct aw9320x_emul_data *d)
{
	gpio_flags_t flags;

	if (gpio_emul_flags_get(int_gpio.port, int_gpio.pin, &flags) == 0 && (flags & GPIO_INPUT)) {
		gpio_emul_input_set(int_gpio.port, int_gpio.pin, d->int_low ? 0 : 1);
	}
}

/* call last: a GPIO callback may switch threads */
static void update_int(struct aw9320x_emul_data *d)
{
	bool low = d->irqsrc != 0;

	if (low != d->int_low) {
		d->int_low = low;
		drive_int(d);
	}
}

static bool fw_valid(const struct aw9320x_emul_data *d)
{
	return be32(&d->flash[AW9320X_EMUL_CHECK_EN_ADDR]) == AW9320X_EMUL_CHECK_EN;
}

static void set_wst(struct aw9320x_emul_data *d)
{
	uint32_t wst = d->mode == AW9320X_EMUL_CMD_ACTIVE  ? AW9320X_EMUL_WST_ACTIVE
		       : d->mode == AW9320X_EMUL_CMD_SLEEP ? AW9320X_EMUL_WST_SLEEP
							   : AW9320X_EMUL_WST_DEEPSLEEP;

	store(d, AW9320X_EMUL_REG_WST, wst);
}

static void init_done(struct aw9320x_emul_data *d)
{
	d->init_due = -1;
	if (!fw_valid(d)) {
		d->state = AW9320X_EMUL_BOOT_FAILED;
		store(d, AW9320X_EMUL_REG_ERR_CODE, ERR_FW_CHECK);
		return;
	}
	d->state = AW9320X_EMUL_APP;
	d->mode = AW9320X_EMUL_CMD_SLEEP;
	d->host_wake = false;
	set_wst(d);
	store(d, AW9320X_EMUL_REG_FIRMWARE, d->fw_version);
	d->irqsrc |= AW9320X_EMUL_IRQ_INIT;
}

static void advance(struct aw9320x_emul_data *d, int64_t now)
{
	if (d->state == AW9320X_EMUL_INIT && d->init_due >= 0 && now >= d->init_due) {
		init_done(d);
	}
}

static void start_init(struct aw9320x_emul_data *d, int64_t now)
{
	d->state = AW9320X_EMUL_INIT;
	d->init_due = now + AW9320X_EMUL_TINIT_MS;
}

static void soft_reset(struct aw9320x_emul_data *d, int64_t now)
{
	d->soft_resets++;
	memset(d->sram, 0, sizeof(d->sram));
	memset(d->sfr, 0, sizeof(d->sfr));
	memset(d->sys, 0, sizeof(d->sys));
	d->irqsrc = 0;
	start_init(d, now);
}

static void enter_boot_loader(struct aw9320x_emul_data *d)
{
	d->state = AW9320X_EMUL_BOOT_LOADER;
	d->init_due = -1;
	store(d, AW9320X_EMUL_REG_UPDATA_DIS,
	      (value_of(d, AW9320X_EMUL_REG_UPDATA_DIS) & ~UPDATA_DIS_JUMP) | 0x01000000);
	store(d, AW9320X_EMUL_REG_BL_ACTIVE, 1);
	store(d, AW9320X_EMUL_REG_BT_VERSION, AW9320X_EMUL_BT_VERSION);
	store(d, AW9320X_EMUL_REG_BT_DATE, AW9320X_EMUL_BT_DATE);
	if (d->ack_irq) {
		d->irqsrc |= AW9320X_EMUL_IRQ_ACK;
	}
}

static void flash_program(struct aw9320x_emul_data *d, uint32_t off, const uint8_t *src,
			  size_t len)
{
	for (size_t i = 0; i < len; i++) {
		d->flash[off + i] &= src[i];
	}
}

static void flash_erase_sector(struct aw9320x_emul_data *d, uint32_t off)
{
	memset(&d->flash[off & ~(AW9320X_EMUL_SECTOR_SIZE - 1u)], 0xff, AW9320X_EMUL_SECTOR_SIZE);
}

static void isp_go(struct aw9320x_emul_data *d)
{
	uint32_t cmd = value_of(d, AW9320X_EMUL_REG_ISPCMD) & 0xff;
	uint32_t off = value_of(d, AW9320X_EMUL_REG_ISPADR) & ~3u;
	uint8_t w[4];

	d->isp_ops[cmd]++;
	if (off >= AW9320X_EMUL_FLASH_SIZE) {
		return;
	}
	switch (cmd) {
	case AW9320X_EMUL_ISP_READ:
		store(d, AW9320X_EMUL_REG_ISPRDATA, be32(&d->flash[off]));
		break;
	case AW9320X_EMUL_ISP_PROGRAM:
		put_be32(w, value_of(d, AW9320X_EMUL_REG_ISPWDATA));
		flash_program(d, off, w, sizeof(w));
		break;
	case AW9320X_EMUL_ISP_ERASE:
		flash_erase_sector(d, off);
		break;
	default:
		break;
	}
}

static uint32_t pack_checksum(uint8_t module, uint8_t command, uint16_t length,
			      const uint8_t *value)
{
	uint32_t sum = module + command + length;

	for (uint32_t i = 0; i < length; i += 4) {
		sum += be32(&value[i]);
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

static void parse_packet(struct aw9320x_emul_data *d)
{
	struct aw9320x_emul_cmd c = { 0 };
	uint32_t pkt = value_of(d, AW9320X_EMUL_REG_CMD_PACK) - AW9320X_EMUL_SRAM_BASE;
	uint8_t *ack = &d->sram[AW9320X_EMUL_ACK_ADDR];
	const uint8_t *p;
	const uint8_t *value;
	int32_t off;

	c.status = AW9320X_EMUL_STATUS_BAD_PACKET;
	if (pkt <= AW9320X_EMUL_SRAM_SIZE - PACK_FIXED) {
		p = &d->sram[pkt];
		value = p + PACK_FIXED;
		c.module = p[8];
		c.command = p[9];
		c.length = be16(&p[10]);
		if (be16(&p[0]) == PACK_HEADER && be16(&p[2]) == PACK_FIXED + c.length &&
		    c.length <= AW9320X_EMUL_SRAM_SIZE - PACK_FIXED - pkt &&
		    be32(&p[4]) == pack_checksum(c.module, c.command, c.length, value)) {
			c.status = AW9320X_EMUL_STATUS_UNKNOWN;
			if (d->cmds_done == d->fail_cmd) {
				c.status = AW9320X_EMUL_STATUS_INJECTED;
			} else if (c.module == MODULE_UPDATE && c.command == UPDATE_START) {
				c.status = 0;
			} else if (c.module == MODULE_FLASH && c.command == FLASH_ERASE_SECTOR &&
				   c.length == 4) {
				c.addr = be32(value);
				off = flash_offset(c.addr, 1);
				c.status = AW9320X_EMUL_STATUS_BAD_ADDR;
				if (off >= 0) {
					flash_erase_sector(d, off);
					c.status = 0;
				}
			} else if (c.module == MODULE_UPDATE && c.command == UPDATE_TRANSFER &&
				   c.length >= 8) {
				c.seq = be32(value);
				c.addr = be32(value + 4);
				off = flash_offset(c.addr, c.length - 8);
				c.status = AW9320X_EMUL_STATUS_BAD_ADDR;
				if (off >= 0) {
					flash_program(d, off, value + 8, c.length - 8);
					c.status = 0;
				}
			}
		}
	}
	if (d->cmds_done < AW9320X_EMUL_CMD_LOG) {
		d->cmd_log[d->cmds_done] = c;
	}
	d->cmds_done++;

	put_be16(&ack[0], PACK_HEADER);
	put_be16(&ack[2], PACK_FIXED + 8);
	ack[8] = c.module;
	ack[9] = c.command;
	put_be16(&ack[10], 8);
	put_be32(&ack[12], c.status);
	put_be32(&ack[16], c.seq);
	put_be32(&ack[4], pack_checksum(c.module, c.command, 8, &ack[12]));
	if (d->ack_irq) {
		d->irqsrc |= AW9320X_EMUL_IRQ_ACK;
	}
}

static void set_mode(struct aw9320x_emul_data *d, uint32_t mode)
{
	if (d->mode == AW9320X_EMUL_CMD_DEEPSLEEP && !d->host_wake) {
		return; /* asleep until HOSTCTRL1 wake */
	}
	d->mode = mode;
	if (mode == AW9320X_EMUL_CMD_DEEPSLEEP) {
		d->host_wake = false;
	}
	set_wst(d);
}

static void cmd_write(struct aw9320x_emul_data *d, uint32_t val)
{
	switch (d->state) {
	case AW9320X_EMUL_APP:
		if (val == AW9320X_EMUL_CMD_ONLINE_UPGRADE) {
			enter_boot_loader(d);
		} else if (val >= AW9320X_EMUL_CMD_ACTIVE && val <= AW9320X_EMUL_CMD_DEEPSLEEP) {
			set_mode(d, val);
		}
		break;
	case AW9320X_EMUL_BOOT_FAILED:
	case AW9320X_EMUL_BOOT_LOADER:
		if (val == AW9320X_EMUL_CMD_ONLINE_UPGRADE) {
			enter_boot_loader(d);
		} else if (val == AW9320X_EMUL_CMD_PARSE && d->state == AW9320X_EMUL_BOOT_LOADER) {
			parse_packet(d);
		}
		break;
	default:
		break;
	}
}

static void mcfg_write(struct aw9320x_emul_data *d, uint32_t val, int64_t now)
{
	if (val & MCFG_CPU_HALT) {
		d->state = AW9320X_EMUL_HALTED;
		d->init_due = -1;
	} else if (d->state == AW9320X_EMUL_HALTED) {
		/* CPU restart: boot loader unless the jump is blocked */
		if (value_of(d, AW9320X_EMUL_REG_UPDATA_DIS) & UPDATA_DIS_JUMP) {
			d->state = AW9320X_EMUL_BOOT_LOADER;
		} else {
			start_init(d, now);
		}
	}
}

static void reg_write(struct aw9320x_emul_data *d, uint16_t addr, uint32_t val, int64_t now)
{
	if (d->writes < AW9320X_EMUL_WRITE_LOG) {
		d->write_addr[d->writes] = addr;
		d->write_val[d->writes] = val;
	}
	d->writes++;
	advance(d, now);

	switch (addr) {
	case AW9320X_EMUL_REG_CHIPID:
	case AW9320X_EMUL_REG_IRQSRC:
		break; /* read-only */
	case AW9320X_EMUL_REG_RSTNALL:
		store(d, addr, val);
		if (val == RSTNALL_KEY) {
			soft_reset(d, now);
		}
		break;
	case AW9320X_EMUL_REG_MCFG:
		store(d, addr, val);
		mcfg_write(d, val, now);
		break;
	case AW9320X_EMUL_REG_CMD:
		store(d, addr, val);
		cmd_write(d, val);
		break;
	case AW9320X_EMUL_REG_HOSTCTRL1:
		store(d, addr, val);
		if (val & HOSTCTRL_WAKE) {
			d->host_wake = true;
		}
		break;
	case AW9320X_EMUL_REG_ISPGO:
		store(d, addr, val);
		if (val & 1) {
			isp_go(d);
		}
		break;
	default:
		store(d, addr, val);
		break;
	}
}

static uint32_t reg_read(struct aw9320x_emul_data *d, uint16_t addr, int64_t now)
{
	uint32_t v;

	advance(d, now);
	v = value_of(d, addr);
	if ((addr & ~3u) == AW9320X_EMUL_REG_IRQSRC) {
		d->irqsrc = 0;
	}
	return v;
}

static bool nak_now(struct aw9320x_emul_data *d)
{
	if (d->nak) {
		return true;
	}
	if (d->nak_after_armed && d->cmds_done >= d->nak_after_cmds) {
		d->nak_after_armed = false;
		d->nak_remaining = d->nak_after_n;
	}
	if (d->nak_remaining > 0) {
		d->nak_remaining--;
		return true;
	}
	return false;
}

static int aw9320x_emul_transfer(const struct emul *target, struct i2c_msg *msgs, int num_msgs,
				 int addr)
{
	struct aw9320x_emul_data *d = target->data;
	int64_t now = k_uptime_get();
	int ret = 0;

	ARG_UNUSED(addr);
	d->transfers++;
	if (nak_now(d)) {
		return -EIO;
	}
	for (int i = 0; i < num_msgs && ret == 0; i++) {
		struct i2c_msg *m = &msgs[i];

		if (m->flags & I2C_MSG_READ) {
			uint32_t w = 0;

			for (uint32_t j = 0; j < m->len; j++) {
				if (j % 4 == 0) {
					w = reg_read(d, d->pointer + j, now);
				}
				m->buf[j] = w >> (8 * (3 - j % 4));
			}
			continue;
		}
		if (m->len < 2) {
			ret = -EIO;
			break;
		}
		d->pointer = be16(m->buf);
		for (uint32_t k = 2; k + 4 <= m->len; k += 4) {
			reg_write(d, d->pointer + (k - 2), be32(&m->buf[k]), now);
		}
	}
	update_int(d);
	return ret;
}

static const struct i2c_emul_api aw9320x_emul_bus_api = {
	.transfer = aw9320x_emul_transfer,
};

static void power_on(struct aw9320x_emul_data *d)
{
	memset(d, 0, sizeof(*d));
	d->chip_id = AW9320X_EMUL_CHIP_ID;
	d->fw_version = AW9320X_EMUL_FW_VERSION;
	d->touch_diff = AW9320X_EMUL_TOUCH_DIFF;
	d->ack_irq = true;
	d->fail_cmd = -1;
	memset(d->flash, 0xff, sizeof(d->flash));
	put_be32(&d->flash[AW9320X_EMUL_CHECK_EN_ADDR], AW9320X_EMUL_CHECK_EN);
	/* init done, interrupt not yet read */
	d->state = AW9320X_EMUL_INIT;
	init_done(d);
}

static int aw9320x_emul_init(const struct emul *target, const struct device *parent)
{
	ARG_UNUSED(parent);
	power_on(target->data);
	return 0;
}

EMUL_DT_DEFINE(DT_NODELABEL(aw93203), aw9320x_emul_init, &aw9320x_data, NULL,
	       &aw9320x_emul_bus_api, NULL);

static struct aw9320x_emul_data *data_of(const struct emul *target)
{
	struct aw9320x_emul_data *d = target->data;

	advance(d, k_uptime_get());
	return d;
}

void aw9320x_emul_set_chip_id(const struct emul *target, uint32_t id)
{
	data_of(target)->chip_id = id;
}

void aw9320x_emul_set_fw_version(const struct emul *target, uint32_t version)
{
	struct aw9320x_emul_data *d = data_of(target);

	d->fw_version = version;
	if (d->state == AW9320X_EMUL_APP) {
		store(d, AW9320X_EMUL_REG_FIRMWARE, version);
	}
}

void aw9320x_emul_corrupt_fw(const struct emul *target)
{
	struct aw9320x_emul_data *d = data_of(target);

	memset(&d->flash[AW9320X_EMUL_CHECK_EN_ADDR], 0xff, 4);
}

bool aw9320x_emul_touch(const struct emul *target, bool down)
{
	struct aw9320x_emul_data *d = data_of(target);
	uint32_t bit = down ? AW9320X_EMUL_IRQ_PROX : AW9320X_EMUL_IRQ_PROX_EXIT;
	bool latched = d->state == AW9320X_EMUL_APP && d->mode == AW9320X_EMUL_CMD_ACTIVE &&
		       (value_of(d, AW9320X_EMUL_REG_IRQEN) & bit);

	d->touched = down;
	if (latched) {
		d->irqsrc |= bit;
	}
	update_int(d);
	return latched;
}

void aw9320x_emul_set_touch_diff(const struct emul *target, int32_t diff)
{
	data_of(target)->touch_diff = diff;
}

void aw9320x_emul_set_nak(const struct emul *target, bool nak)
{
	data_of(target)->nak = nak;
}

void aw9320x_emul_nak_next(const struct emul *target, int n)
{
	data_of(target)->nak_remaining = n;
}

void aw9320x_emul_nak_after_cmds(const struct emul *target, int cmds, int n)
{
	struct aw9320x_emul_data *d = data_of(target);

	d->nak_after_armed = true;
	d->nak_after_cmds = cmds;
	d->nak_after_n = n;
}

void aw9320x_emul_fail_cmd(const struct emul *target, int k)
{
	data_of(target)->fail_cmd = k;
}

void aw9320x_emul_set_ack_irq(const struct emul *target, bool enabled)
{
	data_of(target)->ack_irq = enabled;
}

uint32_t aw9320x_emul_get_reg(const struct emul *target, uint16_t addr)
{
	return value_of(data_of(target), addr);
}

void aw9320x_emul_set_reg(const struct emul *target, uint16_t addr, uint32_t val)
{
	struct aw9320x_emul_data *d = data_of(target);

	if ((addr & ~3u) == AW9320X_EMUL_REG_IRQSRC) {
		d->irqsrc = val;
	} else if ((addr & ~3u) == AW9320X_EMUL_REG_CHIPID) {
		d->chip_id = val;
	} else {
		store(d, addr, val);
	}
	update_int(d);
}

void aw9320x_emul_read_sram(const struct emul *target, uint16_t addr, uint8_t *buf, size_t len)
{
	struct aw9320x_emul_data *d = data_of(target);

	__ASSERT(addr + len <= AW9320X_EMUL_SRAM_SIZE, "SRAM read beyond 2000h");
	memcpy(buf, &d->sram[addr], len);
}

void aw9320x_emul_write_sram(const struct emul *target, uint16_t addr, const uint8_t *buf,
			     size_t len)
{
	struct aw9320x_emul_data *d = data_of(target);

	__ASSERT(addr + len <= AW9320X_EMUL_SRAM_SIZE, "SRAM write beyond 2000h");
	memcpy(&d->sram[addr], buf, len);
}

int aw9320x_emul_writes(const struct emul *target)
{
	return ((struct aw9320x_emul_data *)target->data)->writes;
}

uint16_t aw9320x_emul_write_addr(const struct emul *target, int i)
{
	struct aw9320x_emul_data *d = target->data;

	__ASSERT(i >= 0 && i < MIN(d->writes, AW9320X_EMUL_WRITE_LOG), "write %d not logged", i);
	return d->write_addr[i];
}

uint32_t aw9320x_emul_write_val(const struct emul *target, int i)
{
	struct aw9320x_emul_data *d = target->data;

	__ASSERT(i >= 0 && i < MIN(d->writes, AW9320X_EMUL_WRITE_LOG), "write %d not logged", i);
	return d->write_val[i];
}

int aw9320x_emul_find_write(const struct emul *target, uint16_t addr, int from)
{
	struct aw9320x_emul_data *d = target->data;

	for (int i = MAX(from, 0); i < MIN(d->writes, AW9320X_EMUL_WRITE_LOG); i++) {
		if (d->write_addr[i] == addr) {
			return i;
		}
	}
	return -1;
}

int aw9320x_emul_count_writes(const struct emul *target, uint16_t addr, uint32_t val)
{
	struct aw9320x_emul_data *d = target->data;
	int n = 0;

	for (int i = 0; i < MIN(d->writes, AW9320X_EMUL_WRITE_LOG); i++) {
		if (d->write_addr[i] == addr && d->write_val[i] == val) {
			n++;
		}
	}
	return n;
}

int aw9320x_emul_transfers(const struct emul *target)
{
	return ((struct aw9320x_emul_data *)target->data)->transfers;
}

int aw9320x_emul_cmds(const struct emul *target)
{
	return ((struct aw9320x_emul_data *)target->data)->cmds_done;
}

struct aw9320x_emul_cmd aw9320x_emul_cmd(const struct emul *target, int i)
{
	struct aw9320x_emul_data *d = target->data;

	__ASSERT(i >= 0 && i < MIN(d->cmds_done, AW9320X_EMUL_CMD_LOG), "command %d not logged", i);
	return d->cmd_log[i];
}

const uint8_t *aw9320x_emul_flash(const struct emul *target)
{
	return ((struct aw9320x_emul_data *)target->data)->flash;
}

uint32_t aw9320x_emul_flash_word(const struct emul *target, uint32_t addr)
{
	struct aw9320x_emul_data *d = target->data;

	__ASSERT(addr + 4 <= AW9320X_EMUL_FLASH_SIZE, "flash word beyond 4000h");
	return be32(&d->flash[addr]);
}

int aw9320x_emul_isp_ops(const struct emul *target, uint32_t cmd)
{
	return ((struct aw9320x_emul_data *)target->data)->isp_ops[cmd & 0xff];
}

int aw9320x_emul_soft_resets(const struct emul *target)
{
	return ((struct aw9320x_emul_data *)target->data)->soft_resets;
}

enum aw9320x_emul_state aw9320x_emul_state(const struct emul *target)
{
	return data_of(target)->state;
}

bool aw9320x_emul_int_asserted(const struct emul *target)
{
	struct aw9320x_emul_data *d = data_of(target);

	update_int(d);
	return d->int_low;
}

void aw9320x_emul_sync_int(const struct emul *target)
{
	struct aw9320x_emul_data *d = data_of(target);

	d->int_low = d->irqsrc != 0;
	drive_int(d);
}

static void aw9320x_emul_reset(bool keep_media)
{
	ARG_UNUSED(keep_media);
	power_on(&aw9320x_data);
	drive_int(&aw9320x_data);
}

G1_UNIT_RESET_HOOK(aw9320x_emul_reset);
