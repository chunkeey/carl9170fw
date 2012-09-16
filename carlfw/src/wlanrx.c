/*
 * carl9170 firmware - used by the ar9170 wireless device
 *
 * WLAN receive routines
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
#include "shared/phy.h"
#include "hostif.h"
#include "timer.h"
#include "wl.h"
#include "printf.h"
#include "rf.h"
#include "linux/ieee80211.h"
#include "wol.h"

static struct carl9170_bar_ctx *wlan_get_bar_cache_buffer(void)
{
	struct carl9170_bar_ctx *tmp;

	tmp = &fw.wlan.ba_cache[fw.wlan.ba_tail_idx];
	fw.wlan.ba_tail_idx++;
	fw.wlan.ba_tail_idx %= CONFIG_CARL9170FW_BACK_REQS_NUM;
	if (fw.wlan.queued_ba < CONFIG_CARL9170FW_BACK_REQS_NUM)
		fw.wlan.queued_ba++;

	return tmp;
}

static void handle_bar(struct dma_desc *desc __unused, struct ieee80211_hdr *hdr,
		       unsigned int len, unsigned int mac_err)
{
	struct ieee80211_bar *bar;
	struct carl9170_bar_ctx *ctx;

	if (unlikely(mac_err)) {
		/*
		 * This check does a number of things:
		 * 1. checks if the frame is in good nick
		 * 2. checks if the RA (MAC) matches
		 */
		return ;
	}

	if (unlikely(len < (sizeof(struct ieee80211_bar) + FCS_LEN))) {
		/*
		 * Sneaky, corrupted BARs... but not with us!
		 */

		return ;
	}

	bar = (void *) hdr;

	if ((bar->control & cpu_to_le16(IEEE80211_BAR_CTRL_MULTI_TID)) ||
	    !(bar->control & cpu_to_le16(IEEE80211_BAR_CTRL_CBMTID_COMPRESSED_BA))) {
		/* not implemented yet */

		return ;
	}

	ctx = wlan_get_bar_cache_buffer();

	memcpy(ctx->ra, bar->ra, 6);
	memcpy(ctx->ta, bar->ta, 6);
	ctx->control = bar->control;
	ctx->start_seq_num = bar->start_seq_num;
}

static unsigned int wlan_rx_filter(struct dma_desc *desc)
{
	struct ieee80211_hdr *hdr;
	unsigned int data_len;
	unsigned int rx_filter;
	unsigned int mac_err;

	data_len = ar9170_get_rx_mpdu_len(desc);
	mac_err = ar9170_get_rx_macstatus_error(desc);

#define AR9170_RX_ERROR_BAD (AR9170_RX_ERROR_FCS | AR9170_RX_ERROR_PLCP)

	if (unlikely(data_len < (4 + 6 + FCS_LEN) ||
	    desc->totalLen > CONFIG_CARL9170FW_RX_FRAME_LEN) ||
	    mac_err & AR9170_RX_ERROR_BAD) {
		/*
		 * This frame is too damaged to do anything
		 * useful with it.
		 */

		return CARL9170_RX_FILTER_BAD;
	}

	rx_filter = 0;
	if (mac_err & AR9170_RX_ERROR_WRONG_RA)
		rx_filter |= CARL9170_RX_FILTER_OTHER_RA;

	if (mac_err & AR9170_RX_ERROR_DECRYPT)
		rx_filter |= CARL9170_RX_FILTER_DECRY_FAIL;

	hdr = ar9170_get_rx_i3e(desc);
	if (likely(ieee80211_is_data(hdr->frame_control))) {
		rx_filter |= CARL9170_RX_FILTER_DATA;
	} else if (ieee80211_is_ctl(hdr->frame_control)) {
		switch (le16_to_cpu(hdr->frame_control) & IEEE80211_FCTL_STYPE) {
		case IEEE80211_STYPE_BACK_REQ:
			handle_bar(desc, hdr, data_len, mac_err);
			rx_filter |= CARL9170_RX_FILTER_CTL_BACKR;
			break;
		case IEEE80211_STYPE_PSPOLL:
			rx_filter |= CARL9170_RX_FILTER_CTL_PSPOLL;
			break;
		case IEEE80211_STYPE_BACK:
			if (fw.wlan.queued_bar) {
				/*
				 * Don't filter block acks when the application
				 * has queued BARs. This is because the firmware
				 * can't do the accouting and the application
				 * has to sort out if the BA belongs to any BARs.
				 */
				break;
			}
			/* otherwise fall through */
		default:
			rx_filter |= CARL9170_RX_FILTER_CTL_OTHER;
			break;
		}
	} else {
		/* ieee80211_is_mgmt */
		rx_filter |= CARL9170_RX_FILTER_MGMT;
	}

	if (unlikely(fw.suspend_mode == CARL9170_HOST_SUSPENDED)) {
		wol_rx(rx_filter, hdr, min(data_len,
			(unsigned int)AR9170_BLOCK_SIZE));
	}

#undef AR9170_RX_ERROR_BAD

	return rx_filter;
}

void handle_wlan_rx(void)
{
	struct dma_desc *desc;

	for_each_desc_not_bits(desc, &fw.wlan.rx_queue, AR9170_OWN_BITS_HW) {
		if (!(wlan_rx_filter(desc) & fw.wlan.rx_filter)) {
			dma_put(&fw.pta.up_queue, desc);
			up_trigger();
		} else {
			dma_reclaim(&fw.wlan.rx_queue, desc);
			wlan_trigger(AR9170_DMA_TRIGGER_RXQ);
		}
	}
}
