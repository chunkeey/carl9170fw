/*
 * carl9170user - userspace testing utility for ar9170 devices
 *
 * Abstraction Layer for FW/HW command interface
 *
 * Copyright 2009, 2010 Christian Lamparter <chunkeey@googlemail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "libusb.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "carlu.h"
#include "usb.h"
#include "debug.h"
#include "fwcmd.h"
#include "eeprom.h"

int carlu_cmd_echo(struct carlu *ar, const uint32_t message)
{
	uint32_t _message;
	int ret;

	ret = carlusb_cmd(ar, CARL9170_CMD_ECHO,
			     (uint8_t *)&message, sizeof(message),
			     (uint8_t *)&_message, sizeof(_message));

	if (ret == 0)
		ret = (message == _message) ? 0 : -EIO;

	return ret;
}

int carlu_cmd_reboot(struct carlu *ar)
{
	int err;

	err = carlusb_cmd(ar, CARL9170_CMD_REBOOT,
			  NULL, 0, NULL, 0);

	if (err == -ETIMEDOUT)
		return 0;

	return err ? err : -1;
}

int carlu_cmd_mem_dump(struct carlu *ar, const uint32_t start,
			const unsigned int len, void *_buf)
{
#define RW	8	/* number of words to read at once */
#define RB	(sizeof(uint32_t) * RW)
	uint8_t *buf = _buf;
	unsigned int i, j, block;
	int err;
	__le32 offsets[RW];

	for (i = 0; i < (len + RB - 1) / RB; i++) {
		block = min_t(unsigned int, (len - RB * i) / sizeof(uint32_t), RW);
		for (j = 0; j < block; j++)
			offsets[j] = cpu_to_le32(start + RB * i + 4 * j);

		err = carlusb_cmd(ar, CARL9170_CMD_RREG,
				    (void *) &offsets, block * sizeof(uint32_t),
				    (void *) buf + RB * i, RB);

		if (err)
			return err;
	}

#undef RW
#undef RB

	return 0;
}

int carlu_cmd_write_mem(struct carlu *ar, const uint32_t addr,
			const uint32_t val)
{
	int err;
	__le32 msg, block[2] = { addr, val };

	err = carlusb_cmd(ar, CARL9170_CMD_WREG,
			  (void *) &block, sizeof(block),
			  (void *) &msg, sizeof(msg));
	return err;
}

int carlu_cmd_read_eeprom(struct carlu *ar)
{

	int err;

	err = carlu_cmd_mem_dump(ar, AR9170_EEPROM_START, sizeof(ar->eeprom),
				  &ar->eeprom);

#ifndef __CHECKER__
	/* don't want to handle trailing remains */
	BUILD_BUG_ON(sizeof(ar->eeprom) % 8);
#endif

	if (ar->eeprom.length == cpu_to_le16(0xffff))
		return -ENODATA;

	return 0;
}
