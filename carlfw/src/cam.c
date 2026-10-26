/*
 * carl9170 firmware - used by the ar9170 wireless device
 *
 * Security Engine
 *
 * Copyright (c) 2000-2005 ZyDAS Technology Corporation
 * Copyright (c) 2007-2009 Atheros Communications, Inc.
 * Copyright	2009	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2009-2011	Christian Lamparter <chunkeey@googlemail.com>
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
 * with this program; If not, see <http://www.gnu.org/licenses/>.
 */

#include "carl9170.h"
#include "cam.h"
#include <string.h>

#ifdef CONFIG_CARL9170FW_SECURITY_ENGINE
static void disable_cam_user(const uint16_t userId)
{
	if (userId <= 31)
		andl(AR9170_MAC_REG_CAM_ROLL_CALL_TBL_L, (~((uint32_t) 1 << userId)));
	else if (userId <= 63)
		andl(AR9170_MAC_REG_CAM_ROLL_CALL_TBL_H, (~((uint32_t) 1 << (userId - 32))));
}

static void enable_cam_user(const uint16_t userId)
{
	if (userId <= 31)
		orl(AR9170_MAC_REG_CAM_ROLL_CALL_TBL_L, (((uint32_t) 1) << userId));
	else if (userId <= 63)
		orl(AR9170_MAC_REG_CAM_ROLL_CALL_TBL_H, (((uint32_t) 1) << (userId - 32)));
}

#define CAM_TIMEOUT	10000

static bool wait_for_cam_read_ready(void)
{
	unsigned int timeout = CAM_TIMEOUT;

	while (((get(AR9170_MAC_REG_CAM_STATE) & AR9170_MAC_CAM_STATE_READ_PENDING) == 0) &&
	       --timeout) {
		/* wait */
	}
	return timeout != 0;
}

static bool wait_for_cam_write_ready(void)
{
	unsigned int timeout = CAM_TIMEOUT;

	while (((get(AR9170_MAC_REG_CAM_STATE) & AR9170_MAC_CAM_STATE_WRITE_PENDING) == 0) &&
	       --timeout) {
		/* wait */
	}
	return timeout != 0;
}

static bool HW_CAM_Avail(void)
{
	unsigned int timeout = CAM_TIMEOUT;
	uint32_t tmpValue;

	do {
		tmpValue = get(AR9170_MAC_REG_CAM_MODE);
	} while ((tmpValue & AR9170_MAC_CAM_HOST_PENDING) && --timeout);
	return timeout != 0;
}

static bool HW_CAM_Write128(const uint32_t address, const uint32_t *data)
{
	if (!HW_CAM_Avail())
		return false;

	set(AR9170_MAC_REG_CAM_DATA0, data[0]);
	set(AR9170_MAC_REG_CAM_DATA1, data[1]);
	set(AR9170_MAC_REG_CAM_DATA2, data[2]);
	set(AR9170_MAC_REG_CAM_DATA3, data[3]);

	set(AR9170_MAC_REG_CAM_ADDR, address | AR9170_MAC_CAM_ADDR_WRITE);

	/*
	 * CAM address register already written; no register-level abort
	 * exists for in-flight writes. On timeout, zero the data registers
	 * and issue an invalidating write to the same address so the slot
	 * does not decrypt frames with partial (garbage) key material.
	 */
	if (!wait_for_cam_write_ready()) {
		set(AR9170_MAC_REG_CAM_DATA0, 0);
		set(AR9170_MAC_REG_CAM_DATA1, 0);
		set(AR9170_MAC_REG_CAM_DATA2, 0);
		set(AR9170_MAC_REG_CAM_DATA3, 0);
		set(AR9170_MAC_REG_CAM_ADDR, address | AR9170_MAC_CAM_ADDR_WRITE);
		return false;
	}
	return true;
}

static bool HW_CAM_Read128(const uint32_t address, uint32_t *data)
{
	memset(data, 0, 4 * sizeof(uint32_t));

	if (!HW_CAM_Avail())
		return false;
	set(AR9170_MAC_REG_CAM_ADDR, address);

	if (!wait_for_cam_read_ready())
		return false;
	if (!HW_CAM_Avail())
		return false;
	data[0] = get(AR9170_MAC_REG_CAM_DATA0);
	data[1] = get(AR9170_MAC_REG_CAM_DATA1);
	data[2] = get(AR9170_MAC_REG_CAM_DATA2);
	data[3] = get(AR9170_MAC_REG_CAM_DATA3);
	return true;
}

void set_key(const struct carl9170_set_key_cmd *key)
{
	uint32_t data[4];
	uint16_t row, wordId, nibbleId, i;

	if (key->user > (AR9170_CAM_MAX_USER + 3))
		return ;

	if (key->keyId > 1)
		return ;

	/* Disable Key */
	disable_cam_user(key->user);

	/* Set encrypt type */
	if (key->user >= AR9170_CAM_MAX_USER) {
		/* default */
		row = DEFAULT_ENCRY_TYPE;
		wordId = 0;
		nibbleId = (key->user - AR9170_CAM_MAX_USER) & 0x7;
	} else {
		row = ENCRY_TYPE_START_ADDR + (key->user >> 5);
		wordId = (key->user >> 3) & 0x3;
		nibbleId = key->user & 0x7;
	}

	/*
	 * Re-enable the slot before any early return after the disable
	 * above. A CAM engine timeout mid-install must not leave the slot
	 * permanently disabled — at minimum, open/unencrypted frames must
	 * still pass so the driver can retry the full key install.
	 */
	if (!HW_CAM_Read128(row, data)) {
		enable_cam_user(key->user);
		return;
	}
	data[wordId] &= (~(0xf << ((uint32_t) nibbleId * 4)));
	data[wordId] |= (key->type << ((uint32_t) nibbleId * 4));
	if (!HW_CAM_Write128(row, data)) {
		enable_cam_user(key->user);
		return;
	}

	/* Set MAC address */
	if (key->user < AR9170_CAM_MAX_USER) {
		uint16_t byteId;
		wordId = (key->user >> 2) & 0x3;
		byteId = key->user & 0x3;
		row = (key->user >> 4) * 6;

		for (i = 0; i < 6; i++) {
			if (!HW_CAM_Read128(row + i, data)) {
				enable_cam_user(key->user);
				return;
			}
			data[wordId] &= (~(0xff << ((uint32_t) byteId * 8)));
			data[wordId] |= (key->macAddr[i] << ((uint32_t) byteId * 8));
			if (!HW_CAM_Write128(row + i, data)) {
				enable_cam_user(key->user);
				return;
			}
		}
	}

	/* Set key */
	row = KEY_START_ADDR + (key->user * 2) + key->keyId;

	if (!HW_CAM_Write128(row, key->key)) {
		enable_cam_user(key->user);
		return;
	}

	/* Enable Key */
	enable_cam_user(key->user);
}

void disable_key(const struct carl9170_disable_key_cmd *key)
{
	disable_cam_user(key->user);
}

#endif /* CONFIG_CARL9170FW_SECURITY_ENGINE */
