/*
 * carl9170 firmware - used by the ar9170 wireless device
 *
 * Interface to the WLAN part of the chip
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
#include "shared/phy.h"
#include "hostif.h"
#include "timer.h"
#include "wl.h"
#include "printf.h"
#include "rf.h"
#include "linux/ieee80211.h"

static void wlan_txunstuck(unsigned int queue)
{
	set_wlan_txq_dma_addr(queue, ((uint32_t) fw.wlan.tx_queue[queue].head) | 1);
	wlan_trigger(BIT(queue));
}

#ifdef CONFIG_CARL9170FW_DMA_QUEUE_BUMP
static void wlan_txupdate(unsigned int queue)
{
	set_wlan_txq_dma_addr(queue, ((uint32_t) fw.wlan.tx_queue[queue].head));
	wlan_trigger(BIT(queue));
}

static void wlan_dma_bump(unsigned int qidx)
{
	unsigned int offset = qidx;
	uint32_t status, trigger;

	status = get(AR9170_MAC_REG_DMA_STATUS) >> 12;
	trigger = get(AR9170_MAC_REG_DMA_TRIGGER) >> 12;

	while (offset != 0) {
		status >>= 4;
		trigger >>= 4;
		offset--;
	}

	status &= 0xf;
	trigger &= 0xf;

	if ((trigger == 0xa) && (status == 0x8)) {
		DBG("UNSTUCK");
		wlan_txunstuck(qidx);
	} else {
		DBG("UPDATE");
		wlan_txupdate(qidx);
	}
}
#endif /* CONFIG_CARL9170FW_DMA_QUEUE_BUMP */

#ifdef CONFIG_CARL9170FW_DEBUG
static void wlan_dump_queue(unsigned int qidx)
{

	struct dma_desc *desc;
	struct carl9170_tx_superframe *super;
	int entries = 0;

	__for_each_desc(desc, &fw.wlan.tx_queue[qidx]) {
		super = get_super(desc);
		DBG("%d: %p s:%x c:%x tl:%x ds:%x n:%p l:%p ", entries, desc,
		    desc->status, desc->ctrl, desc->totalLen,
		    desc->dataSize, desc->nextAddr, desc->lastAddr);

		DBG("c:%x tr:%d ri:%d l:%x m:%x p:%x fc:%x",
		    super->s.cookie, super->s.cnt, super->s.rix,
		    super->f.hdr.length, super->f.hdr.mac.set,
		    (unsigned int) le32_to_cpu(super->f.hdr.phy.set),
		    super->f.data.i3e.frame_control);

		entries++;
	}

	desc = get_wlan_txq_addr(qidx);

	DBG("Queue: %d: te:%d td:%d h:%p c:%p t:%p",
	    qidx, entries, queue_len(&fw.wlan.tx_queue[qidx]),
	    fw.wlan.tx_queue[qidx].head,
	    desc, fw.wlan.tx_queue[qidx].terminator);

	DBG("HW: t:%x s:%x ac:%x c:%x",
	    (unsigned int) get(AR9170_MAC_REG_DMA_TRIGGER),
	    (unsigned int) get(AR9170_MAC_REG_DMA_STATUS),
	    (unsigned int) get(AR9170_MAC_REG_AMPDU_COUNT),
	    (unsigned int) get(AR9170_MAC_REG_DMA_TXQX_ADDR_CURR));
}
#endif /* CONFIG_CARL9170FW_DEBUG */

static void wlan_send_buffered_tx_status(void)
{
	unsigned int len;

	while (fw.wlan.tx_status_pending) {
		len = min((unsigned int)fw.wlan.tx_status_pending,
			  CARL9170_RSP_TX_STATUS_NUM);
		len = min(len, CARL9170_TX_STATUS_NUM -	fw.wlan.tx_status_head_idx);

		/*
		 * rather than memcpy each individual request into a large buffer,
		 * we _splice_ them all together.
		 *
		 * The only downside is however that we have to be careful around
		 * the edges of the tx_status_cache.
		 *
		 * Note:
		 * Each tx_status is about 2 bytes. However every command package
		 * must have a size which is a multiple of 4.
		 */

		send_cmd_to_host((len * sizeof(struct carl9170_tx_status) + 3) & ~3,
				 CARL9170_RSP_TXCOMP, len, (void *)
				 &fw.wlan.tx_status_cache[fw.wlan.tx_status_head_idx]);

		fw.wlan.tx_status_pending -= len;
		fw.wlan.tx_status_head_idx += len;
		fw.wlan.tx_status_head_idx %= CARL9170_TX_STATUS_NUM;
	}
}

static struct carl9170_tx_status *wlan_get_tx_status_buffer(void)
{
	struct carl9170_tx_status *tmp;

	tmp = &fw.wlan.tx_status_cache[fw.wlan.tx_status_tail_idx++];
	fw.wlan.tx_status_tail_idx %= CARL9170_TX_STATUS_NUM;

	if (fw.wlan.tx_status_pending == CARL9170_TX_STATUS_NUM)
		wlan_send_buffered_tx_status();

	fw.wlan.tx_status_pending++;

	return tmp;
}

/* generate _aggregated_ tx_status for the host */
static void wlan_tx_complete(struct carl9170_tx_superframe *super,
			     bool txs)
{
	struct carl9170_tx_status *status;

	status = wlan_get_tx_status_buffer();

	/*
	 * The *unique* cookie and AC_ID is used by the driver for
	 * frame lookup.
	 */
	status->cookie = super->s.cookie;
	status->queue = super->s.queue;

	/*
	 * This field holds the number of tries of the rate in
	 * the rate index field (rix).
	 */
	status->rix = super->s.rix;
	status->tries = super->s.cnt;
	status->success = (txs) ? 1 : 0;
}

static bool wlan_tx_consume_retry(struct carl9170_tx_superframe *super)
{
	/* check if this was the last possible retry with this rate */
	if (unlikely(super->s.cnt >= super->s.ri[super->s.rix].tries)) {
		/* end of the road - indicate tx failure */
		if (unlikely(super->s.rix == CARL9170_TX_MAX_RETRY_RATES))
			return false;

		/* check if there are alternative rates available */
		if (!super->s.rr[super->s.rix].set)
			return false;

		/* try next retry rate */
		super->f.hdr.phy.set = super->s.rr[super->s.rix].set;

		/* finally - mark the old rate as USED */
		super->s.rix++;

		/* update MAC flags */
		super->f.hdr.mac.erp_prot = super->s.ri[super->s.rix].erp_prot;
		super->f.hdr.mac.ampdu = super->s.ri[super->s.rix].ampdu;

		/* reinitialize try counter */
		super->s.cnt = 1;
	} else {
		/* just increase retry counter */
		super->s.cnt++;
	}

	return true;
}

/* for all tries */
static void __wlan_tx(struct dma_desc *desc)
{
	struct carl9170_tx_superframe *super = get_super(desc);
#ifdef CONFIG_CARL9170FW_NORMAL_TX_RX
	unsigned int queue = super->s.queue;
#endif /* CONFIG_CARL9170FW_LOOPBACK */

	if (unlikely(super->s.fill_in_tsf)) {
		struct ieee80211_mgmt *mgmt = (void *) &super->f.data.i3e;
		uint32_t *tsf = (uint32_t *) &mgmt->u.probe_resp.timestamp;

		/*
		 * Truth be told: this is a hack.
		 *
		 * The *real* TSF is definitely going to be higher/older.
		 * But this hardware emulation code is head and shoulders
		 * above anything a driver can possibly do.
		 *
		 * (even, if it's got an accurate atomic clock source).
		 */

		read_tsf(tsf);
	}

#if (defined CONFIG_CARL9170FW_LOOPBACK) || (defined CONFIG_CARL9170FW_DISCARD)
	wlan_tx_complete(super, true);
	unhide_super(desc);
# ifdef CONFIG_CARL9170FW_LOOPBACK
	dma_put(&fw.pta.up_queue, desc);
	up_trigger();
# elif CONFIG_CARL9170FW_DISCARD
	dma_reclaim(&fw.pta.down_queue, desc);
	down_trigger();
# endif
#else /* CONFIG_CARL9170FW_LOOPBACK */

# if ((defined CONFIG_CARL9170FW_DEBUG) && (defined CONFIG_CARL9170FW_PSM))
	BUG_ON(fw.phy.psm.state != CARL9170_PSM_WAKE);
# endif /* CONFIG_CARL9170FW_DEBUG && CONFIG_CARL9170FW_PSM */

	/* insert desc into the right queue */
	dma_put(&fw.wlan.tx_queue[queue], desc);
	wlan_trigger(BIT(queue));
#endif /* CONFIG_CARL9170FW_LOOPBACK */
}

/* prepares frame for the first transmission */
static void _wlan_tx(struct dma_desc *desc)
{
	struct carl9170_tx_superframe *super = get_super(desc);

	if (unlikely(super->s.ampdu_commit_density)) {
		set(AR9170_MAC_REG_AMPDU_DENSITY,
		    MOD_VAL(AR9170_MAC_AMPDU_DENSITY,
			    get(AR9170_MAC_REG_AMPDU_DENSITY),
			    super->s.ampdu_density));
	}

	if (unlikely(super->s.ampdu_commit_factor)) {
		set(AR9170_MAC_REG_AMPDU_FACTOR,
		    MOD_VAL(AR9170_MAC_AMPDU_FACTOR,
			    get(AR9170_MAC_REG_AMPDU_FACTOR),
			    8 << super->s.ampdu_factor));
	}

	__wlan_tx(desc);
}

/* propagate transmission status back to the driver */
static bool wlan_tx_status(struct dma_queue *queue,
			   struct dma_desc *desc)
{
	struct ar9170_tx_frame *frame = DESC_PAYLOAD(desc);
	struct carl9170_tx_superframe *super = get_super(desc);
	struct ieee80211_hdr *hdr = &super->f.data.i3e;
	unsigned int qidx = super->s.queue;
	bool txfail, success;

	success = true;

	/* update hangcheck */
	fw.wlan.last_tx_desc_num[qidx] = 0;

	if (!!(desc->ctrl & AR9170_CTRL_FAIL)) {
		txfail = !!(desc->ctrl & AR9170_CTRL_TXFAIL);

		/* reset retry indicator flags */
		desc->ctrl &= ~(AR9170_CTRL_TXFAIL | AR9170_CTRL_BAFAIL);

		if (wlan_tx_consume_retry(super)) {
			/*
			 * retry for simple and aggregated 802.11 frames.
			 *
			 * Note: We must not mess up the original frame
			 * order.
			 */

			if (!frame->hdr.mac.ampdu) {
				/*
				 * 802.11 - 7.1.3.1.5.
				 * set "Retry Field" for consecutive attempts
				 *
				 * Note: For AMPDU see:
				 * 802.11n 9.9.1.6 "Retransmit Procedures"
				 */

				hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_RETRY);
			}

			if (txfail) {
				/* Normal TX Failure */

				/* demise descriptor ownership back to the hardware */
				dma_rearm(desc);

				/*
				 * And this will get the queue going again.
				 * To understand why: you have to get the HW
				 * specs... But sadly I never saw them.
				 */
				wlan_txunstuck(qidx);

				/* abort cycle - this is necessary due to HW design */
				return false;
			} else {
				/* (HT-) BlockACK failure */

				/*
				 * Unlink the failed attempt and put it into
				 * the retry queue. The caller routine must
				 * be aware of this so the frames don't get lost.
				 */

				dma_unlink_head(queue);
#ifdef CONFIG_CARL9170FW_DELAYED_TX
				dma_put(&fw.wlan.tx_retry, desc);
#else
				__wlan_tx(desc);
#endif /* CONFIG_CARL9170FW_DELAYED_TX */
				return true;
			}
		} else {
			/* out of frame attempts - discard frame */
			success = false;
		}
	}

	dma_unlink_head(queue);
	if (txfail) {
		/*
		 * Issue the queue bump,
		 * We need to do this in case this was the frame's last
		 * possible retry attempt and it unfortunately: it failed.
		 */

		wlan_txunstuck(qidx);
	}

	unhide_super(desc);

#ifdef CONFIG_CARL9170FW_HANDLE_BACK_REQ
	if (unlikely(super == (void *) &dma_mem.reserved.ba)) {
		fw.wlan.ba_desc = desc;
		fw.wlan.ba_desc_available = 1;
		return true;
	}
#endif /* CONFIG_CARL9170FW_HANDLE_BACK_REQ */

	wlan_tx_complete(super, success);

#ifdef CONFIG_CARL9170FW_CAB_QUEUE
	if (unlikely(super->s.cab))
		fw.wlan.cab_queue_len[super->s.vif_id]--;
#endif /* CONFIG_CARL9170FW_CAB_QUEUE */

	/* recycle freed descriptors */
	dma_reclaim(&fw.pta.down_queue, desc);
	down_trigger();
	return true;
}

static void handle_tx_completion(void)
{
	struct dma_desc *desc;
	unsigned int i;

	for (i = 0; i < __AR9170_NUM_TX_QUEUES; i++) {
		__while_desc_bits(desc, &fw.wlan.tx_queue[i], AR9170_OWN_BITS_SW) {
			if (!wlan_tx_status(&fw.wlan.tx_queue[i], desc)) {
				/* termination requested. */
				break;
			}
		}

#ifdef CONFIG_CARL9170FW_DELAYED_TX
		for_each_desc(desc, &fw.wlan.tx_retry)
			__wlan_tx(desc);

		for_each_desc(desc, &fw.wlan.tx_delay[i])
			_wlan_tx(desc);
#endif /* CONFIG_CARL9170FW_DELAYED_TX */
	}
}

void __hot wlan_tx(struct dma_desc *desc)
{
	struct carl9170_tx_superframe *super = DESC_PAYLOAD(desc);

	/* initialize rate control struct */
	super->s.rix = 0;
	super->s.cnt = 1;
	hide_super(desc);

#ifdef CONFIG_CARL9170FW_CAB_QUEUE
	if (unlikely(super->s.cab)) {
		fw.wlan.cab_queue_len[super->s.vif_id]++;
		dma_put(&fw.wlan.cab_queue[super->s.vif_id], desc);
		return;
	}
#endif /* CONFIG_CARL9170FW_CAB_QUEUE */

#ifdef CONFIG_CARL9170FW_DELAYED_TX
	if (!queue_empty(&fw.wlan.tx_queue[super->s.queue])) {
		dma_put(&fw.wlan.tx_delay[super->s.queue], desc);
		return;
	}
#endif /* CONFIG_CARL9170FW_DELAYED_TX */

	_wlan_tx(desc);
}

#ifdef CONFIG_CARL9170FW_HANDLE_BACK_REQ
static void wlan_send_buffered_ba(void)
{
	struct carl9170_tx_ba_superframe *baf = &dma_mem.reserved.ba.ba;
	struct ieee80211_ba *ba = (struct ieee80211_ba *) &baf->f.ba;
	struct carl9170_bar_ctx *ctx;

	if (likely(fw.wlan.ba_head_idx == fw.wlan.ba_tail_idx))
		return;

	/* there's no point to continue when the ba_desc is not available. */
	if (!fw.wlan.ba_desc_available)
		return;

	ctx = &fw.wlan.ba_cache[fw.wlan.ba_head_idx % CONFIG_CARL9170FW_BACK_REQS_NUM];
	fw.wlan.ba_head_idx++;

	/* Format BlockAck */
	fw.wlan.ba_desc->status = 0;
	fw.wlan.ba_desc->ctrl = AR9170_CTRL_FS_BIT | AR9170_CTRL_LS_BIT;
	fw.wlan.ba_desc_available = 0;
	fw.wlan.ba_desc->nextAddr = fw.wlan.ba_desc->lastAddr =
		fw.wlan.ba_desc;

	baf->s.len = fw.wlan.ba_desc->totalLen = fw.wlan.ba_desc->dataSize =
		sizeof(struct carl9170_tx_superdesc) +
		sizeof(struct ar9170_tx_hwdesc) +
		sizeof(struct ieee80211_ba);

	baf->s.ri[0].tries = 3;
	baf->s.queue = 0;
	baf->f.hdr.length = sizeof(struct ieee80211_ba) + FCS_LEN;

	/* HW Duration / Backoff */
	baf->f.hdr.mac.backoff = 1;
	baf->f.hdr.mac.hw_duration = 1;

	/* take the TX rate from the RX'd BAR */
	baf->f.hdr.phy.set = ctx->phy;
	baf->f.hdr.phy.tx_power = 29; /* 14.5 dBm */

	/* format outgoing BA */
	ba->frame_control = cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_BACK);
	ba->duration = cpu_to_le16(0);
	memcpy(ba->ta, ctx->ta, 6);
	memcpy(ba->ra, ctx->ra, 6);

	/*
	 * Unfortunately, we cannot look into the hardware's scoreboard.
	 * Therefore we have to proceed as described in 802.11n 9.10.7.5
	 * and send a null BlockAck.
	 */
	memset(ba->bitmap, 0x0, sizeof(ba->bitmap));

	/*
	 * NB:
	 * not entirely sure if this is 100% correct?!
	 */
	ba->control = ctx->control | cpu_to_le16(1);
	ba->start_seq_num = ctx->start_seq_num;

	wlan_tx(fw.wlan.ba_desc);
}

static struct carl9170_bar_ctx *wlan_get_bar_cache_buffer(void)
{
	struct carl9170_bar_ctx *tmp;

	/* expire oldest entry, if we ran out of ba_ctx' */
	if (fw.wlan.ba_head_idx + CONFIG_CARL9170FW_BACK_REQS_NUM < fw.wlan.ba_tail_idx)
		fw.wlan.ba_head_idx++;

	tmp = &fw.wlan.ba_cache[fw.wlan.ba_tail_idx % CONFIG_CARL9170FW_BACK_REQS_NUM];
	fw.wlan.ba_tail_idx++;

	return tmp;
}

static void handle_bar(struct dma_desc *desc, struct ieee80211_hdr *hdr,
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

	/* Brilliant! The BAR provides all necessary MACs! */
	memcpy(ctx->ra, bar->ta, 6);
	memcpy(ctx->ta, bar->ra, 6);

	/*
	 * NB:
	 * not entirely sure if this is 100% correct to force the
	 * imm ack bit or not...
	 */
	ctx->control = bar->control | cpu_to_le16(1);
	ctx->start_seq_num = bar->start_seq_num;
	ctx->phy = ar9170_rx_to_phy(desc);
	if (unlikely(!ctx->phy)) {
		/* provide a backup, in case ar9170_rx_to_phy fails */
		ctx->phy = cpu_to_le32(0x2cc301);
	}
}
#endif /* CONFIG_CARL9170FW_HANDLE_BACK_REQ */

static void wlan_check_rx_overrun(void)
{
	uint32_t overruns, total;

	fw.wlan.rx_total += total = get(AR9170_MAC_REG_RX_TOTAL);
	fw.wlan.rx_overruns += overruns = get(AR9170_MAC_REG_RX_OVERRUN);
	if (unlikely(overruns)) {
		if (overruns == total) {
			DBG("RX Overrun");
			fw.wlan.mac_reset++;
		}

		wlan_trigger(AR9170_DMA_TRIGGER_RXQ);
	}
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
#ifdef CONFIG_CARL9170FW_HANDLE_BACK_REQ
			handle_bar(desc, hdr, data_len, mac_err);
#endif /* CONFIG_CARL9170FW_HANDLE_BACK_REQ */
			/* fallthrough */
			rx_filter |= CARL9170_RX_FILTER_CTL_BACKR;
			break;
		case IEEE80211_STYPE_PSPOLL:
			rx_filter |= CARL9170_RX_FILTER_CTL_PSPOLL;
			break;
		default:
			rx_filter |= CARL9170_RX_FILTER_CTL_OTHER;
			break;
		}
	} else {
		/* ieee80211_is_mgmt */
		rx_filter |= CARL9170_RX_FILTER_MGMT;
	}

#undef AR9170_RX_ERROR_BAD

	return rx_filter;
}

static void handle_rx(void)
{
	struct dma_desc *desc;

	for_each_desc_not_bits(desc, &fw.wlan.rx_queue, AR9170_OWN_BITS_HW) {
		if (!(wlan_rx_filter(desc) & fw.wlan.rx_filter)) {
			dma_put(&fw.pta.up_queue, desc);
			up_trigger();
		} else {
			dma_reclaim(&fw.wlan.rx_queue, desc);
			_wlan_trigger(AR9170_DMA_TRIGGER_RXQ);
		}
	}
}

#ifdef CONFIG_CARL9170FW_CAB_QUEUE
void wlan_cab_flush_queue(const unsigned int vif)
{
	struct dma_queue *cab_queue = &fw.wlan.cab_queue[vif];
	struct dma_desc *desc;

	/* move queued frames into the main tx queues */
	for_each_desc(desc, cab_queue) {
		struct carl9170_tx_superframe *super = get_super(desc);
		if (!queue_empty(cab_queue)) {
			/*
			 * Set MOREDATA flag for all,
			 * but the last queued frame.
			 * see: 802.11-2007 11.2.1.5 f)
			 *
			 * This is actually the reason to why
			 * we need to prevent the reentry.
			 */

			super->f.data.i3e.frame_control |=
				cpu_to_le16(IEEE80211_FCTL_MOREDATA);
		} else {
			super->f.data.i3e.frame_control &=
				cpu_to_le16(~IEEE80211_FCTL_MOREDATA);
		}

		/* ready to roll! */
		_wlan_tx(desc);
	}
}

static uint8_t *beacon_find_ie(uint8_t ie, void *addr,
			       const unsigned int len)
{
	struct ieee80211_mgmt *mgmt = addr;
	uint8_t *pos, *end;

	pos = mgmt->u.beacon.variable;
	end = (uint8_t *) ((unsigned long)mgmt + (len - FCS_LEN));
	while (pos < end) {
		if (pos + 2 + pos[1] > end)
			return NULL;

		if (pos[0] == ie)
			return pos;

		pos += pos[1] + 2;
	}

	return NULL;
}

void wlan_cab_modify_dtim_beacon(const unsigned int vif,
	const unsigned int addr, const unsigned int len)
{
	uint8_t *_ie;
	struct ieee80211_tim_ie *ie;

	_ie = beacon_find_ie(WLAN_EID_TIM, (void *)addr, len);
	if (likely(_ie)) {
		ie = (struct ieee80211_tim_ie *) &_ie[2];

		if (!queue_empty(&fw.wlan.cab_queue[vif]) && (ie->dtim_count == 0)) {
			/* schedule DTIM transfer */
			fw.wlan.cab_flush_trigger[vif] = CARL9170_CAB_TRIGGER_ARMED;
		} else if ((fw.wlan.cab_queue_len[vif] == 0) && (fw.wlan.cab_flush_trigger[vif])) {
			/* undo all chances to the beacon structure */
			ie->bitmap_ctrl &= ~0x1;
			fw.wlan.cab_flush_trigger[vif] = CARL9170_CAB_TRIGGER_EMPTY;
		}

		/* Triggered by CARL9170_CAB_TRIGGER_ARMED || CARL9170_CAB_TRIGGER_DEFER */
		if (fw.wlan.cab_flush_trigger[vif]) {
			/* Set the almighty Multicast Traffic Indication Bit. */
			ie->bitmap_ctrl |= 0x1;
		}
	}
}
#endif /* CONFIG_CARL9170FW_CAB_QUEUE */

static void handle_beacon_config(void)
{
	uint32_t bcn_count;

	bcn_count = get(AR9170_MAC_REG_BCN_COUNT);
	send_cmd_to_host(4, CARL9170_RSP_BEACON_CONFIG, 0x00,
			 (uint8_t *) &bcn_count);

	set(AR9170_MAC_REG_BCN_CTRL, AR9170_BCN_CTRL_READY);
}

static void handle_pretbtt(void)
{
#ifdef CONFIG_CARL9170FW_CAB_QUEUE
	fw.wlan.cab_flush_time = get_clock_counter();
#endif /* CONFIG_CARL9170FW_CAB_QUEUE */

#ifdef CONFIG_CARL9170FW_PSM
	rf_psm();

	send_cmd_to_host(4, CARL9170_RSP_PRETBTT, 0x00,
			 (uint8_t *) &fw.phy.psm.state);
#else
	send_cmd_to_host(0, CARL9170_RSP_PRETBTT, 0x00, NULL);
#endif /* CONFIG_CARL9170FW_PSM */
}

static void handle_atim(void)
{
	send_cmd_to_host(0, CARL9170_RSP_ATIM, 0x00, NULL);
}

#ifdef CONFIG_CARL9170FW_DEBUG
static void handle_qos(void)
{
	/*
	 * What is the QoS Bit used for?
	 * Is it only an indicator for TXOP & Burst, or
	 * should we do something here?
	 */
}

static void handle_radar(void)
{
	send_cmd_to_host(0, CARL9170_RSP_RADAR, 0x00, NULL);
}
#endif /* CONFIG_CARL9170FW_DEBUG */

static void wlan_janitor(void)
{
#ifdef CONFIG_CARL9170FW_CAB_QUEUE
	unsigned int i;

	for (i = 0; i < CARL9170_INTF_NUM; i++) {
		if (unlikely(fw.wlan.cab_flush_trigger[i] == CARL9170_CAB_TRIGGER_ARMED)) {
			/*
			 * This is hardcoded into carl9170usb driver.
			 *
			 * The driver must set the PRETBTT event to beacon_interval -
			 * CARL9170_PRETBTT_KUS (usually 6) Kus.
			 *
			 * But still, we can only do so much about 802.11-2007 9.3.2.1 &
			 * 11.2.1.6. Let's hope the current solution is adequate enough.
			 */

			if (is_after_msecs(fw.wlan.cab_flush_time, (CARL9170_TBTT_DELTA))) {
				wlan_cab_flush_queue(i);

				/*
				 * This prevents the code from sending new BC/MC frames
				 * which were queued after the previous buffered traffic
				 * has been sent out... They will have to wait until the
				 * next DTIM beacon comes along.
				 */
				fw.wlan.cab_flush_trigger[i] = CARL9170_CAB_TRIGGER_DEFER;
			}
		}

	}
#endif /* CONFIG_CARL9170FW_CAB_QUEUE */

#ifdef CONFIG_CARL9170FW_DELAYED_TX
	if (fw.wlan.tx_trigger) {
		_wlan_trigger(fw.wlan.tx_trigger);
		fw.wlan.tx_trigger = 0;
	}
#endif /* CONFIG_CARL9170FW_DELAYED_TX */

	wlan_send_buffered_tx_status();

#ifdef CONFIG_CARL9170FW_HANDLE_BACK_REQ
	wlan_send_buffered_ba();
#endif /* CONFIG_CARL9170FW_HANDLE_BACK_REQ */
}

void handle_wlan(void)
{
	uint32_t intr;

	intr = get(AR9170_MAC_REG_INT_CTRL);
	/* ACK Interrupt */
	set(AR9170_MAC_REG_INT_CTRL, intr);

#define HANDLER(intr, flag, func)			\
	do {						\
		if ((intr & flag) != 0) {		\
			func();				\
		}					\
	} while (0)

	intr |= fw.wlan.soft_int;
	fw.wlan.soft_int = 0;

	HANDLER(intr, AR9170_MAC_INT_PRETBTT, handle_pretbtt);

	HANDLER(intr, AR9170_MAC_INT_ATIM, handle_atim);

	HANDLER(intr, AR9170_MAC_INT_RXC, handle_rx);

	HANDLER(intr, (AR9170_MAC_INT_TXC | AR9170_MAC_INT_RETRY_FAIL),
		handle_tx_completion);

#ifdef CONFIG_CARL9170FW_DEBUG
	HANDLER(intr, AR9170_MAC_INT_QOS, handle_qos);

	HANDLER(intr, AR9170_MAC_INT_RADAR, handle_radar);
#endif /* CONFIG_CARL9170FW_DEBUG */

	HANDLER(intr, AR9170_MAC_INT_CFG_BCN, handle_beacon_config);

	if (unlikely(intr))
		DBG("Unhandled Interrupt %x\n", (unsigned int) intr);

	wlan_janitor();

#undef HANDLER
}

static void wlan_check_hang(void)
{
	struct dma_desc *desc;
	unsigned int i;

	for (i = 0; i < __AR9170_NUM_TX_QUEUES; i++) {
		if (queue_empty(&fw.wlan.tx_queue[i])) {
			/* Nothing to do here... move along */
			continue;
		}

		/* fetch the current DMA queue position */
		desc = get_wlan_txq_addr(i);

		/* Stuck frame detection */
		if (unlikely(desc == fw.wlan.last_tx_desc[i])) {
			fw.wlan.last_tx_desc_num[i]++;

			if (unlikely(fw.wlan.last_tx_desc_num[i] > 6)) {
				/*
				 * schedule MAC reset (aka OFF/ON => dead)
				 *
				 * This will almost certainly kill
				 * the device for good, but it's the
				 * recommended thing to do...
				 */

				fw.wlan.mac_reset++;
			}

#ifdef CONFIG_CARL9170FW_DEBUG
			if (unlikely(fw.wlan.last_tx_desc_num[i] > 5)) {
				/*
				 * Sigh, the queue is almost certainly
				 * dead. Dump the queue content to the
				 * user, maybe we find out why it got
				 * so stuck.
				 */

				wlan_dump_queue(i);
			}
#endif /* CONFIG_CARL9170FW_DEBUG */

#ifdef CONFIG_CARL9170FW_DMA_QUEUE_BUMP
			if (unlikely(fw.wlan.last_tx_desc_num[i] > 3)) {
				/*
				 * Hrrm, bump the queue a bit.
				 * maybe this will get it going again.
				 */

				wlan_dma_bump(i);
			}
#endif /* CONFIG_CARL9170FW_DMA_QUEUE_BUMP */
		} else {
			/* Nothing stuck */
			fw.wlan.last_tx_desc[i] = desc;
			fw.wlan.last_tx_desc_num[i] = 0;
		}
	}
}

#ifdef CONFIG_CARL9170FW_FW_MAC_RESET
/*
 * NB: Resetting the MAC is a two-edged sword.
 * On most occasions, it does what it is supposed to do.
 * But there is a chance that this will make it
 * even worse and the radio dies silently.
 */
static void wlan_mac_reset(void)
{
	uint32_t val;
	uint32_t agg_wait_counter;
	uint32_t agg_density;
	uint32_t bcn_start_addr;
	uint32_t rctl, rcth;
	uint32_t cam_mode;
	uint32_t ack_power;
	uint32_t rts_cts_tpc;
	uint32_t rts_cts_rate;
	unsigned int i;

#ifdef CONFIG_CARL9170FW_RADIO_FUNCTIONS
	uint32_t rx_BB;
#endif /* CONFIG_CARL9170FW_RADIO_FUNCTIONS */

	INFO("MAC RESET");

	/* Save aggregation parameters */
	agg_wait_counter = get(AR9170_MAC_REG_AMPDU_FACTOR);
	agg_density = get(AR9170_MAC_REG_AMPDU_DENSITY);

	bcn_start_addr = get(AR9170_MAC_REG_BCN_ADDR);

	cam_mode = get(AR9170_MAC_REG_CAM_MODE);
	rctl = get(AR9170_MAC_REG_CAM_ROLL_CALL_TBL_L);
	rcth = get(AR9170_MAC_REG_CAM_ROLL_CALL_TBL_H);

	ack_power = get(AR9170_MAC_REG_ACK_TPC);
	rts_cts_tpc = get(AR9170_MAC_REG_RTS_CTS_TPC);
	rts_cts_rate = get(AR9170_MAC_REG_RTS_CTS_RATE);

#ifdef CONFIG_CARL9170FW_RADIO_FUNCTIONS
	/* 0x1c8960 write only */
	rx_BB = get(AR9170_PHY_REG_SWITCH_CHAIN_0);
#endif /* CONFIG_CARL9170FW_RADIO_FUNCTIONS */

	/* TX/RX must be stopped by now */
	val = get(AR9170_MAC_REG_POWER_STATE_CTRL);

	val |= AR9170_MAC_POWER_STATE_CTRL_RESET;

	/*
	 * Manipulate CCA threshold to stop transmission
	 *
	 * set(AR9170_PHY_REG_CCA_THRESHOLD, 0x300);
	 */

	/*
	 * check Rx state in 0(idle) 9(disable)
	 *
	 * chState = (get(AR9170_MAC_REG_MISC_684) >> 16) & 0xf;
	 * while( (chState != 0) && (chState != 9)) {
	 *	chState = (get(AR9170_MAC_REG_MISC_684) >> 16) & 0xf;
	 * }
	 */

	set(AR9170_MAC_REG_POWER_STATE_CTRL, val);

	delay(2);

	/* Restore aggregation parameters */
	set(AR9170_MAC_REG_AMPDU_FACTOR, agg_wait_counter);
	set(AR9170_MAC_REG_AMPDU_DENSITY, agg_density);

	set(AR9170_MAC_REG_BCN_ADDR, bcn_start_addr);
	set(AR9170_MAC_REG_CAM_MODE, cam_mode);
	set(AR9170_MAC_REG_CAM_ROLL_CALL_TBL_L, rctl);
	set(AR9170_MAC_REG_CAM_ROLL_CALL_TBL_H, rcth);

	set(AR9170_MAC_REG_RTS_CTS_TPC, rts_cts_tpc);
	set(AR9170_MAC_REG_ACK_TPC, ack_power);
	set(AR9170_MAC_REG_RTS_CTS_RATE, rts_cts_rate);

#ifdef CONFIG_CARL9170FW_RADIO_FUNCTIONS
	set(AR9170_PHY_REG_SWITCH_CHAIN_2, rx_BB);
#endif /* CONFIG_CARL9170FW_RADIO_FUNCTIONS */

	/*
	 * Manipulate CCA threshold to resume transmission
	 *
	 * set(AR9170_PHY_REG_CCA_THRESHOLD, 0x0);
	 */

	/* Reinitialize all WLAN TX DMA queues. */
	for (i = 0; i < __AR9170_NUM_TX_QUEUES; i++) {
		struct dma_desc *iter;

		__for_each_desc_bits(iter, &fw.wlan.tx_queue[i], AR9170_OWN_BITS_SW);

		set_wlan_txq_dma_addr(i, (uint32_t) iter);
		if (!is_terminator(&fw.wlan.tx_queue[i], iter))
			wlan_trigger(BIT(i));

		DBG("Q:%d l:%d h:%p t:%p cu:%p it:%p ct:%x st:%x\n", i, queue_len(&fw.wlan.tx_queue[i]),
		     fw.wlan.tx_queue[i].head, fw.wlan.tx_queue[i].terminator,
		     get_wlan_txq_addr(i), iter, iter->ctrl, iter->status);
	}

	fw.wlan.soft_int |= AR9170_MAC_INT_RXC | AR9170_MAC_INT_TXC |
			    AR9170_MAC_INT_RETRY_FAIL;

	set(AR9170_MAC_REG_DMA_RXQ_ADDR, (uint32_t) fw.wlan.rx_queue.head);
	wlan_trigger(AR9170_DMA_TRIGGER_RXQ);
}
#else
static void wlan_mac_reset(void)
{
	/* The driver takes care of reinitializing the device */
	BUG("MAC RESET");
}
#endif /* CONFIG_CARL9170FW_FW_MAC_RESET */

void __cold wlan_timer(void)
{
	unsigned int cached_mac_reset;

	cached_mac_reset = fw.wlan.mac_reset;

	/* TX Queue Hang check */
	wlan_check_hang();

	/* RX Overrun check */
	wlan_check_rx_overrun();

	if (unlikely(fw.wlan.mac_reset >= CARL9170_MAC_RESET_RESET)) {
		wlan_mac_reset();
		fw.wlan.mac_reset = CARL9170_MAC_RESET_OFF;
	} else {
		if (fw.wlan.mac_reset && cached_mac_reset == fw.wlan.mac_reset)
			fw.wlan.mac_reset--;
	}
}
