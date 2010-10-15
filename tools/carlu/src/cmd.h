/*
 * carl9170user - userspace testing utility for ar9170 devices
 *
 * register/memory/command access functions
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

#ifndef __CARL9170USER_CMD_H
#define __CARL9170USER_CMD_H

#include "carlu.h"

int carlu_cmd_echo(struct carlu *ar, const uint32_t message);
int carlu_cmd_reboot(struct carlu *ar);
int carlu_cmd_read_eeprom(struct carlu *ar);
int carlu_cmd_mem_dump(struct carlu *ar, const uint32_t start,
			const unsigned int len, void *_buf);
int carlu_cmd_write_mem(struct carlu *ar, const uint32_t addr,
			const uint32_t val);

struct carl9170_cmd *carlu_cmd_buf(struct carlu *ar,
	const enum carl9170_cmd_oids cmd, const unsigned int len);
#endif /* __CARL9170USER_CMD_H */
