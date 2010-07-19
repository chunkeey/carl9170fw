/*
 * carl9170 firmware - used by the ar9170 wireless device
 *
 * Host interface routines
 *
 * Copyright (c) 2000-2005 ZyDAS Technology Corporation
 * Copyright (c) 2007-2009 Atheros Communications, Inc.
 * Copyright	2009	Johannes Berg <johannes@sipsolutions.net>
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

#include "carl9170.h"
#include "hostif.h"
#include "printf.h"
#include "wl.h"

static bool length_check(struct dma_desc *desc)
{
	volatile struct carl9170_tx_superframe *super = DESC_PAYLOAD(desc);

	if (unlikely(desc->totalLen < sizeof(struct carl9170_tx_superdesc)))
		return false;

	/*
	 * check if the DMA is complete, or clipped.
	 *
	 * NB: The hardware aligns the descriptor length to
	 * a 4 byte boundary. This makes the direct comparison
	 * difficult, or unnecessary complex for a hot-path.
	 */
	if (unlikely(super->s.len > desc->totalLen))
		return false;

	return true;
}

static void handle_download(void)
{
	struct dma_desc *desc;

	/*
	 * Under normal conditions, all completed descs should have
	 * the AR9170_OWN_BITS_SE status flag set.
	 * However there seems to be a undocumented case where the flag
	 * is _SW...
	 */

	for_each_desc_not_bits(desc, &fw.pta.down_queue, AR9170_OWN_BITS_HW) {
		if (unlikely((length_check(desc) == false))) {
			/*
			 * There is no easy way of telling what was lost.
			 *
			 * Therefore we just reclaim the data.
			 * The driver has to have some sort frame
			 * timeout mechanism.
			 */

			dma_reclaim(&fw.pta.down_queue, desc);
			down_trigger();
			continue;
		}

		wlan_tx(desc);
	}

#ifdef CONFIG_CARL9170FW_DEBUG_LED_HEARTBEAT
	xorl(AR9170_GPIO_REG_PORT_DATA, 2);
#endif /* CONFIG_CARL9170FW_DEBUG_LED_HEARTBEAT */
}

static void handle_upload(void)
{
	struct dma_desc *desc;

	for_each_desc_not_bits(desc, &fw.pta.up_queue, AR9170_OWN_BITS_HW) {
		/*
		 * BIG FAT NOTE:
		 *
		 * DO NOT compare the descriptor addresses.
		 */
		if (DESC_PAYLOAD(desc) == (void *) &dma_mem.reserved.rsp) {
			fw.usb.int_desc = desc;
			fw.usb.int_desc_available = 1;
		} else {
#ifdef CONFIG_CARL9170FW_LOOPBACK
			dma_reclaim(&fw.pta.down_queue, desc);
			down_trigger();
#else
			dma_reclaim(&fw.wlan.rx_queue, desc);
			_wlan_trigger(AR9170_DMA_TRIGGER_RXQ);
#endif /* CONFIG_CARL9170FW_LOOPBACK */
		}
	}

#ifdef CONFIG_CARL9170FW_DEBUG_LED_HEARTBEAT
	xorl(AR9170_GPIO_REG_PORT_DATA, 2);
#endif /* CONFIG_CARL9170FW_DEBUG_LED_HEARTBEAT */
}

/* handle interrupts from DMA chip */
void handle_host_interface(void)
{
	uint32_t pta_int;

	pta_int = get(AR9170_PTA_REG_INT_FLAG);

#define HANDLER(intr, flag, func)			\
	do {						\
		if ((intr & flag) != 0) {		\
			func();				\
		}					\
	} while (0)

	HANDLER(pta_int, AR9170_PTA_INT_FLAG_DN, handle_download);

	HANDLER(pta_int, AR9170_PTA_INT_FLAG_UP, handle_upload);

#undef HANDLER
}
