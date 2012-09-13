/*
 * carl9170 firmware - used by the ar9170 wireless device
 *
 * Host interface routines
 *
 * Copyright (c) 2000-2005 ZyDAS Technology Corporation
 * Copyright (c) 2007-2009 Atheros Communications, Inc.
 * Copyright	2009	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2009-2012	Christian Lamparter <chunkeey@googlemail.com>
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
#include "io.h"
#include "cam.h"
#include "rf.h"
#include "timer.h"
#include "wol.h"

static bool length_check(struct dma_desc *desc)
{
	volatile struct carl9170_tx_superframe *super = __get_super(desc);

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
	 * is _SW ( handle_download_exception )
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

			wlan_tx_complete(__get_super(desc), false);
			dma_reclaim(&fw.pta.down_queue, desc);
			down_trigger();
		} else {
			wlan_tx(desc);
		}
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
			dma_reclaim(&fw.wlan.rx_queue, desc);
			wlan_trigger(AR9170_DMA_TRIGGER_RXQ);
		}
	}

#ifdef CONFIG_CARL9170FW_DEBUG_LED_HEARTBEAT
	xorl(AR9170_GPIO_REG_PORT_DATA, 2);
#endif /* CONFIG_CARL9170FW_DEBUG_LED_HEARTBEAT */
}

static void handle_download_exception(void)
{
	struct dma_desc *desc, *target;

	/* actually, the queue should be stopped by now? */
	usb_stop_down_queue();

	target = (void *)((get(AR9170_PTA_REG_DN_CURR_ADDRH) << 16) |
		  get(AR9170_PTA_REG_DN_CURR_ADDRL));

	/*
	 * Put "forgotten" packets from the head of the queue, back
	 * to the current position
	 */
	__while_desc_bits(desc, &fw.pta.down_queue, AR9170_OWN_BITS_HW) {
		if (desc == target)
			break;

		dma_reclaim(&fw.pta.down_queue,
		    dma_unlink_head(&fw.pta.down_queue));
	}

	__for_each_desc_continue(desc, &fw.pta.down_queue) {
		if ((desc->status & AR9170_OWN_BITS) == AR9170_OWN_BITS_SW)
			dma_fix_downqueue(desc);
	}


	usb_start_down_queue();

	down_trigger();
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

	/* This is just guesswork and MAGIC */
	pta_int = get(AR9170_PTA_REG_DMA_STATUS);
	HANDLER(pta_int, 0x1, handle_download_exception);

#undef HANDLER
}

void handle_cmd(struct carl9170_rsp *resp)
{
	struct carl9170_cmd *cmd = &dma_mem.reserved.cmd.cmd;
	unsigned int i;

	/* copies cmd, len and extra fields */
	resp->hdr.len = cmd->hdr.len;
	resp->hdr.cmd = cmd->hdr.cmd;
	resp->hdr.ext = cmd->hdr.ext;
	resp->hdr.seq |= cmd->hdr.seq;

	switch (cmd->hdr.cmd & ~CARL9170_CMD_ASYNC_FLAG) {
	case CARL9170_CMD_RREG:
		for (i = 0; i < (cmd->hdr.len / 4); i++)
			resp->rreg_res.vals[i] = get(cmd->rreg.regs[i]);
		break;

	case CARL9170_CMD_WREG:
		resp->hdr.len = 0;
		for (i = 0; i < (cmd->hdr.len / 8); i++)
			set(cmd->wreg.regs[i].addr, cmd->wreg.regs[i].val);
		break;

	case CARL9170_CMD_ECHO:
		memcpy(resp->echo.vals, cmd->echo.vals, cmd->hdr.len);
		break;

	case CARL9170_CMD_SWRST:
#ifdef CONFIG_CARL9170FW_FW_MAC_RESET
		/*
		 * Command has no payload, so the response
		 * has no payload either.
		 * resp->hdr.len = 0;
		 */
		fw.wlan.mac_reset = CARL9170_MAC_RESET_FORCE;
#endif /* CONFIG_CARL9170FW_FW_MAC_RESET */
		break;

	case CARL9170_CMD_REBOOT:
		/*
		 * resp->len = 0;
		 */
		fw.reboot = 1;
		break;

	case CARL9170_CMD_READ_TSF:
		resp->hdr.len = 8;
		read_tsf((uint32_t *)resp->tsf.tsf);
		break;

	case CARL9170_CMD_RX_FILTER:
		resp->hdr.len = 0;
		fw.wlan.rx_filter = cmd->rx_filter.rx_filter;
		break;

	case CARL9170_CMD_WOL:
		wol_cmd(&cmd->wol);
		break;

	case CARL9170_CMD_TALLY:
		resp->hdr.len = sizeof(struct carl9170_tally_rsp);
		memcpy(&resp->tally, &fw.tally, sizeof(struct carl9170_tally_rsp));
		resp->tally.tick = fw.ticks_per_usec;
		memset(&fw.tally, 0, sizeof(struct carl9170_tally_rsp));
		break;

	case CARL9170_CMD_BCN_CTRL:
		resp->hdr.len = 0;

		if (cmd->bcn_ctrl.mode & CARL9170_BCN_CTRL_CAB_TRIGGER) {
			wlan_modify_beacon(cmd->bcn_ctrl.vif_id,
				cmd->bcn_ctrl.bcn_addr, cmd->bcn_ctrl.bcn_len);
			set(AR9170_MAC_REG_BCN_ADDR, cmd->bcn_ctrl.bcn_addr);
			set(AR9170_MAC_REG_BCN_LENGTH, cmd->bcn_ctrl.bcn_len);
			set(AR9170_MAC_REG_BCN_CTRL, AR9170_BCN_CTRL_READY);
		} else {
			wlan_cab_flush_queue(cmd->bcn_ctrl.vif_id);
			fw.wlan.cab_flush_trigger[cmd->bcn_ctrl.vif_id] = CARL9170_CAB_TRIGGER_EMPTY;
		}
		break;

#ifdef CONFIG_CARL9170FW_SECURITY_ENGINE
	case CARL9170_CMD_EKEY:
		resp->hdr.len = 0;
		set_key(&cmd->setkey);
		break;

	case CARL9170_CMD_DKEY:
		resp->hdr.len = 0;
		disable_key(&cmd->disablekey);
		break;
#endif /* CONFIG_CARL9170FW_SECURITY_ENGINE */

#ifdef CONFIG_CARL9170FW_RADIO_FUNCTIONS
	case CARL9170_CMD_FREQUENCY:
	case CARL9170_CMD_RF_INIT:
		rf_cmd(cmd, resp);
		break;

	case CARL9170_CMD_FREQ_START:
		/*
		 * resp->hdr.len = 0;
		 */
		rf_notify_set_channel();
		break;

	case CARL9170_CMD_PSM:
		resp->hdr.len = 0;
		fw.phy.psm.state = le32_to_cpu(cmd->psm.state);
		rf_psm();
		break;
#endif /* CONFIG_CARL9170FW_RADIO_FUNCTIONS */

	default:
		BUG("Unknown command %x\n", cmd->hdr.cmd);
		break;
	}
}
