/*
 * carl9170 firmware - used by the ar9170 wireless device
 *
 * WLAN transmit and tx status
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

static void wlan_txunstuck(unsigned int queue)
{
	set_wlan_txq_dma_addr(queue, ((uint32_t) fw.wlan.tx_queue[queue].head) | 1);
}

#ifdef CONFIG_CARL9170FW_DMA_QUEUE_BUMP
static void wlan_txupdate(unsigned int queue)
{
	set_wlan_txq_dma_addr(queue, ((uint32_t) fw.wlan.tx_queue[queue].head));
}

void wlan_dma_bump(unsigned int qidx)
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
#else
void wlan_dma_bump(unsigned int __unused qidx)
{
}
#endif /* CONFIG_CARL9170FW_DMA_QUEUE_BUMP */

void wlan_send_buffered_tx_status(void)
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
void wlan_tx_complete(struct carl9170_tx_superframe *super,
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
	super->s.cookie = 0;

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

static inline u16 get_tid(struct ieee80211_hdr *hdr)
{
	return (ieee80211_get_qos_ctl(hdr))[0] & IEEE80211_QOS_CTL_TID_MASK;
}

/* This function will only work on uint32_t-aligned pointers! */
static bool same_hdr(const void *_d0, const void *_d1)
{
	const uint32_t *d0 = _d0;
	const uint32_t *d1 = _d1;

	/* BUG_ON((unsigned long)d0 & 3 || (unsigned long)d1 & 3)) */
	return !((d0[0] ^ d1[0]) |			/* FC + DU */
		 (d0[1] ^ d1[1]) |			/* addr1 */
		 (d0[2] ^ d1[2]) | (d0[3] ^ d1[3]) |	/* addr2 + addr3 */
		 (d0[4] ^ d1[4]));			/* addr3 */
}

static inline bool same_aggr(struct ieee80211_hdr *a, struct ieee80211_hdr *b)
{
	return (get_tid(a) == get_tid(b)) || same_hdr(a, b);
}

static void wlan_tx_ampdu_reset(unsigned int qidx)
{
	fw.wlan.ampdu_prev[qidx] = NULL;
}

static void wlan_tx_ampdu_end(unsigned int qidx)
{
	struct carl9170_tx_superframe *ht_prev = fw.wlan.ampdu_prev[qidx];

	if (ht_prev)
		ht_prev->f.hdr.mac.ba_end = 1;

	wlan_tx_ampdu_reset(qidx);
}

static void wlan_tx_ampdu(struct carl9170_tx_superframe *super)
{
	unsigned int qidx = super->s.queue;
	struct carl9170_tx_superframe *ht_prev = fw.wlan.ampdu_prev[qidx];

	if (super->f.hdr.mac.ampdu) {
		if (ht_prev &&
		    !same_aggr(&super->f.data.i3e, &ht_prev->f.data.i3e))
			ht_prev->f.hdr.mac.ba_end = 1;
		else
			super->f.hdr.mac.ba_end = 0;

		fw.wlan.ampdu_prev[qidx] = super;
	} else {
		wlan_tx_ampdu_end(qidx);
	}
}

/* for all tries */
static void __wlan_tx(struct dma_desc *desc)
{
	struct carl9170_tx_superframe *super = get_super(desc);

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

	wlan_tx_ampdu(super);

#ifdef CONFIG_CARL9170FW_DEBUG
	BUG_ON(fw.phy.psm.state != CARL9170_PSM_WAKE);
#endif /* CONFIG_CARL9170FW_DEBUG */

	/* insert desc into the right queue */
	dma_put(&fw.wlan.tx_queue[super->s.queue], desc);
}

static void wlan_assign_seq(struct ieee80211_hdr *hdr, unsigned int vif)
{
	hdr->seq_ctrl &= cpu_to_le16(~IEEE80211_SCTL_SEQ);
	hdr->seq_ctrl |= cpu_to_le16(fw.wlan.sequence[vif]);

	if (ieee80211_is_first_frag(hdr->seq_ctrl))
		fw.wlan.sequence[vif] += 0x10;
}

/* prepares frame for the first transmission */
static void _wlan_tx(struct dma_desc *desc)
{
	struct carl9170_tx_superframe *super = get_super(desc);

	if (unlikely(super->s.assign_seq))
		wlan_assign_seq(&super->f.data.i3e, super->s.vif_id);

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
}

/* propagate transmission status back to the driver */
static bool wlan_tx_status(struct dma_queue *queue,
			   struct dma_desc *desc)
{
	struct carl9170_tx_superframe *super = get_super(desc);
	unsigned int qidx = super->s.queue;
	bool txfail = false, success;

	success = true;

	/* update hangcheck */
	fw.wlan.last_super_num[qidx] = 0;

	/*
	 * Note:
	 * There could be a corner case when the TXFAIL is set
	 * even though the frame was properly ACKed by the peer:
	 *   a BlockAckReq with the immediate policy will cause
	 *   the receiving peer to produce a BlockACK unfortunately
	 *   the MAC in this chip seems to be expecting a legacy
	 *   ACK and marks the BAR as failed!
	 */

	if (!!(desc->ctrl & AR9170_CTRL_FAIL)) {
		txfail = !!(desc->ctrl & AR9170_CTRL_TXFAIL);

		/* reset retry indicator flags */
		desc->ctrl &= ~(AR9170_CTRL_TXFAIL | AR9170_CTRL_BAFAIL);

		/*
		 * Note: wlan_tx_consume_retry will override the old
		 * phy [CCK,OFDM, HT, BW20/40, MCS...] and mac vectors
		 * [AMPDU,RTS/CTS,...] therefore be careful when they
		 * are used.
		 */
		if (wlan_tx_consume_retry(super)) {
			/*
			 * retry for simple and aggregated 802.11 frames.
			 *
			 * Note: We must not mess up the original frame
			 * order.
			 */

			if (!super->f.hdr.mac.ampdu) {
				/*
				 * 802.11 - 7.1.3.1.5.
				 * set "Retry Field" for consecutive attempts
				 *
				 * Note: For AMPDU see:
				 * 802.11n 9.9.1.6 "Retransmit Procedures"
				 */
				super->f.data.i3e.frame_control |=
					cpu_to_le16(IEEE80211_FCTL_RETRY);
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

#ifndef CONFIG_CARL9170FW_DEBUG
				dma_unlink_head(queue);
#else /* CONFIG_CARL9170FW_DEBUG */
				BUG_ON(dma_unlink_head(queue) != desc);
#endif /* CONFIG_CARL9170FW_DEBUG */
				dma_put(&fw.wlan.tx_retry, desc);
				return true;
			}
		} else {
			/* out of frame attempts - discard frame */
			success = false;
		}
	}

#ifndef CONFIG_CARL9170FW_DEBUG
	dma_unlink_head(queue);
#else /* CONFIG_CARL9170FW_DEBUG */
	BUG_ON(dma_unlink_head(queue) != desc);
#endif /* CONFIG_CARL9170FW_DEBUG */
	if (txfail) {
		/*
		 * Issue the queue bump,
		 * We need to do this in case this was the frame's last
		 * possible retry attempt and it unfortunately: it failed.
		 */

		wlan_txunstuck(qidx);
	}

	unhide_super(desc);

	if (unlikely(super == fw.wlan.fw_desc_data)) {
		fw.wlan.fw_desc = desc;
		fw.wlan.fw_desc_available = 1;

		if (fw.wlan.fw_desc_callback)
			fw.wlan.fw_desc_callback(super, success);

		return true;
	}

	if (unlikely(super->s.cab))
		fw.wlan.cab_queue_len[super->s.vif_id]--;

	wlan_tx_complete(super, success);

	if (ieee80211_is_back_req(super->f.data.i3e.frame_control)) {
		fw.wlan.queued_bar--;
	}

	/* recycle freed descriptors */
	dma_reclaim(&fw.pta.down_queue, desc);
	down_trigger();
	return true;
}

void handle_wlan_tx_completion(void)
{
	struct dma_desc *desc;
	int i;

	for (i = AR9170_TXQ_SPECIAL; i >= AR9170_TXQ0; i--) {
		__while_desc_bits(desc, &fw.wlan.tx_queue[i], AR9170_OWN_BITS_SW) {
			if (!wlan_tx_status(&fw.wlan.tx_queue[i], desc)) {
				/* termination requested. */
				break;
			}
		}

		wlan_tx_ampdu_reset(i);

		for_each_desc(desc, &fw.wlan.tx_retry)
			__wlan_tx(desc);

		wlan_tx_ampdu_end(i);
		if (!queue_empty(&fw.wlan.tx_queue[i]))
			wlan_trigger(BIT(i));
	}
}

void __hot wlan_tx(struct dma_desc *desc)
{
	struct carl9170_tx_superframe *super = DESC_PAYLOAD(desc);

	if (ieee80211_is_back_req(super->f.data.i3e.frame_control)) {
		fw.wlan.queued_bar++;
	}

	/* initialize rate control struct */
	super->s.rix = 0;
	super->s.cnt = 1;
	hide_super(desc);

	if (unlikely(super->s.cab)) {
		fw.wlan.cab_queue_len[super->s.vif_id]++;
		dma_put(&fw.wlan.cab_queue[super->s.vif_id], desc);
		return;
	}

	_wlan_tx(desc);
	__wlan_tx(desc);
	wlan_trigger(BIT(super->s.queue));
}

void wlan_tx_fw(struct carl9170_tx_superdesc *super, fw_desc_callback_t cb)
{
	if (!fw.wlan.fw_desc_available)
		return;

	fw.wlan.fw_desc_available = 0;

	/* Format BlockAck */
	fw.wlan.fw_desc->ctrl = AR9170_CTRL_FS_BIT | AR9170_CTRL_LS_BIT;
	fw.wlan.fw_desc->status = AR9170_OWN_BITS_SW;

	fw.wlan.fw_desc->totalLen = fw.wlan.fw_desc->dataSize = super->len;
	fw.wlan.fw_desc_data = fw.wlan.fw_desc->dataAddr = super;
	fw.wlan.fw_desc->nextAddr = fw.wlan.fw_desc->lastAddr =
		fw.wlan.fw_desc;
	fw.wlan.fw_desc_callback = cb;
	wlan_tx(fw.wlan.fw_desc);
}

void wlan_send_buffered_ba(void)
{
	struct carl9170_tx_ba_superframe *baf = &dma_mem.reserved.ba.ba;
	struct ieee80211_ba *ba = (struct ieee80211_ba *) &baf->f.ba;
	struct carl9170_bar_ctx *ctx;

	if (likely(!fw.wlan.queued_ba))
		return;

	/* there's no point to continue when the ba_desc is not available. */
	if (!fw.wlan.fw_desc_available)
		return;

	ctx = &fw.wlan.ba_cache[fw.wlan.ba_head_idx];
	fw.wlan.ba_head_idx++;
	fw.wlan.ba_head_idx %= CONFIG_CARL9170FW_BACK_REQS_NUM;
	fw.wlan.queued_ba--;

	baf->s.len = sizeof(struct carl9170_tx_superdesc) +
		     sizeof(struct ar9170_tx_hwdesc) +
		     sizeof(struct ieee80211_ba);
	baf->s.ri[0].tries = 1;
	baf->s.cookie = 0;
	baf->s.queue = AR9170_TXQ_VO;
	baf->f.hdr.length = sizeof(struct ieee80211_ba) + FCS_LEN;

	baf->f.hdr.mac.no_ack = 1;

	baf->f.hdr.phy.modulation = 1; /* OFDM */
	baf->f.hdr.phy.tx_power = 34; /* 17 dBm */
	baf->f.hdr.phy.chains = 1;
	baf->f.hdr.phy.mcs = AR9170_TXRX_PHY_RATE_OFDM_6M;

	/* format outgoing BA */
	ba->frame_control = cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_BACK);
	ba->duration = cpu_to_le16(0);

	/* the BAR contains all necessary MACs. All we need is to swap them */
	memcpy(ba->ra, ctx->ta, 6);
	memcpy(ba->ta, ctx->ra, 6);

	/*
	 * Unfortunately, we cannot look into the hardware's scoreboard.
	 * Therefore we have to proceed as described in 802.11n 9.10.7.5
	 * and send a null BlockAck.
	 */
	memset(ba->bitmap, 0x0, sizeof(ba->bitmap));

	/*
	 * Both, the original firmare and ath9k set the NO ACK flag in
	 * the BA Ack Policy subfield.
	 */
	ba->control = ctx->control | cpu_to_le16(1);
	ba->start_seq_num = ctx->start_seq_num;
	wlan_tx_fw(&baf->s, NULL);
}

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
		__wlan_tx(desc);
		wlan_trigger(BIT(super->s.queue));
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

void wlan_modify_beacon(const unsigned int vif,
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

	/*
	 * Ideally, the sequence number should be assigned by the TX arbiter
	 * hardware. But AFAIK that's not possible, so we have to go for the
	 * next best thing and write it into the beacon fifo during the open
	 * beacon update window.
	 */

	wlan_assign_seq((struct ieee80211_hdr *)addr, vif);
}

void wlan_send_buffered_cab(void)
{
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
}
