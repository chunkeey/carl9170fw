/*
 * carl9170 firmware - used by the ar9170 wireless device
 *
 * Interface to the WLAN part of the chip
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
}

#ifdef CONFIG_CARL9170FW_DMA_QUEUE_BUMP
static void wlan_txupdate(unsigned int queue)
{
	set_wlan_txq_dma_addr(queue, ((uint32_t) fw.wlan.tx_queue[queue].head));
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
static inline bool compare_ether_address(const void *_d0, const void *_d1)
{
	const uint32_t *d0 = _d0;
	const uint32_t *d1 = _d1;

	/* BUG_ON((unsigned long)d0 & 3 || (unsigned long)d1 & 3)) */
	return !((d0[0] ^ d1[0]) | (unsigned short)(d0[1] ^ d1[1]));
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

static void wlan_tx_ampdu_end(unsigned int qidx)
{
	struct carl9170_tx_superframe *ht_prev = fw.wlan.ampdu_prev[qidx];

	fw.wlan.ampdu_prev[qidx] = NULL;
	if (ht_prev)
		ht_prev->f.hdr.mac.ba_end = 1;
}

static void wlan_tx_ampdu(struct carl9170_tx_superframe *super)
{
	unsigned int qidx = super->s.queue;
	struct carl9170_tx_superframe *ht_prev = fw.wlan.ampdu_prev[qidx];

	if (!super->f.hdr.mac.ampdu) {
		wlan_tx_ampdu_end(qidx);
	} else {
		fw.wlan.ampdu_prev[qidx] = super;

		if (ht_prev &&
		    !same_aggr(&super->f.data.i3e, &ht_prev->f.data.i3e))
			ht_prev->f.hdr.mac.ba_end = 1;
		else
			super->f.hdr.mac.ba_end = 0;
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

# ifdef CONFIG_CARL9170FW_DEBUG
	BUG_ON(fw.phy.psm.state != CARL9170_PSM_WAKE);
# endif /* CONFIG_CARL9170FW_DEBUG */

	/* insert desc into the right queue */
	dma_put(&fw.wlan.tx_queue[super->s.queue], desc);
#endif /* CONFIG_CARL9170FW_LOOPBACK */
}

static void wlan_assign_seq(struct ieee80211_hdr *hdr, unsigned int vif)
{
	hdr->seq_ctrl &= cpu_to_le16(~IEEE80211_SCTL_SEQ);
	hdr->seq_ctrl |= cpu_to_le16(fw.wlan.sequence[vif]);

	if (!(hdr->seq_ctrl & cpu_to_le16(IEEE80211_SCTL_FRAG)))
		fw.wlan.sequence[vif] += 0x10;
}

/* prepares frame for the first transmission */
static void _wlan_tx(struct dma_desc *desc)
{
	struct carl9170_tx_superframe *super = get_super(desc);

	if (unlikely(super->s.assign_seq)) {
		wlan_assign_seq(&super->f.data.i3e, super->s.vif_id);
	}

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
	struct carl9170_tx_superframe *super = get_super(desc);
	unsigned int qidx = super->s.queue;
	bool txfail = false, success;

	success = true;

	/* update hangcheck */
	fw.wlan.last_super_num[qidx] = 0;

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

				dma_unlink_head(queue);
				dma_put(&fw.wlan.tx_retry, desc);
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

	if (unlikely(super == fw.wlan.fw_desc_data)) {
		fw.wlan.fw_desc = desc;
		fw.wlan.fw_desc_available = 1;

		if (fw.wlan.fw_desc_callback)
			fw.wlan.fw_desc_callback(super, success);

		return true;
	}

#ifdef CONFIG_CARL9170FW_CAB_QUEUE
	if (unlikely(super->s.cab))
		fw.wlan.cab_queue_len[super->s.vif_id]--;
#endif /* CONFIG_CARL9170FW_CAB_QUEUE */

	wlan_tx_complete(super, success);

	/* recycle freed descriptors */
	dma_reclaim(&fw.pta.down_queue, desc);
	down_trigger();
	return true;
}

static void handle_tx_completion(void)
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

	_wlan_tx(desc);
	wlan_trigger(BIT(super->s.queue));
}

static void wlan_tx_fw(struct carl9170_tx_superdesc *super, fw_desc_callback_t cb)
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

static void wlan_send_buffered_ba(void)
{
	struct carl9170_tx_ba_superframe *baf = &dma_mem.reserved.ba.ba;
	struct ieee80211_ba *ba = (struct ieee80211_ba *) &baf->f.ba;
	struct carl9170_bar_ctx *ctx;

	if (likely(fw.wlan.ba_head_idx == fw.wlan.ba_tail_idx))
		return;

	/* there's no point to continue when the ba_desc is not available. */
	if (!fw.wlan.fw_desc_available)
		return;

	ctx = &fw.wlan.ba_cache[fw.wlan.ba_head_idx];
	fw.wlan.ba_head_idx++;
	fw.wlan.ba_head_idx %= CONFIG_CARL9170FW_BACK_REQS_NUM;

	baf->s.len = sizeof(struct carl9170_tx_superdesc) +
		     sizeof(struct ar9170_tx_hwdesc) +
		     sizeof(struct ieee80211_ba);
	baf->s.ri[0].tries = 1;
	baf->s.cookie = 0;
	baf->s.queue = AR9170_TXQ_VO;
	baf->f.hdr.length = sizeof(struct ieee80211_ba) + FCS_LEN;

	/* HW Duration / Backoff */
	baf->f.hdr.mac.backoff = 1;
	baf->f.hdr.mac.hw_duration = 1;

	/* take the TX rate from the RX'd BAR */
	baf->f.hdr.phy.set = ctx->phy;
	baf->f.hdr.phy.tx_power = 29; /* 14.5 dBm */

	/* format outgoing BA */
	ba->frame_control = cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_NULLFUNC);
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
	wlan_tx_fw(&baf->s, NULL);
}

static struct carl9170_bar_ctx *wlan_get_bar_cache_buffer(void)
{
	struct carl9170_bar_ctx *tmp;

	tmp = &fw.wlan.ba_cache[fw.wlan.ba_tail_idx];
	fw.wlan.ba_tail_idx++;
	fw.wlan.ba_tail_idx %= CONFIG_CARL9170FW_BACK_REQS_NUM;

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

#ifdef CONFIG_CARL9170FW_WOL
void wlan_prepare_wol(void)
{
	/* set MAC filter */
	memcpy((void *)AR9170_MAC_REG_MAC_ADDR_L, fw.wlan.wol.cmd.mac, 6);
	memcpy((void *)AR9170_MAC_REG_BSSID_L, fw.wlan.wol.cmd.bssid, 6);
	set(AR9170_MAC_REG_RX_CONTROL, AR9170_MAC_RX_CTRL_DEAGG);

	/* set filter policy to: discard everything */
	fw.wlan.rx_filter = CARL9170_RX_FILTER_EVERYTHING;

	/* reenable rx dma */
	wlan_trigger(AR9170_DMA_TRIGGER_RXQ);

	/* initialize the last_beacon timer */
	fw.wlan.wol.last_null = fw.wlan.wol.last_beacon = get_clock_counter();
}

#ifdef CONFIG_CARL9170FW_WOL_NL80211_TRIGGERS
static bool wlan_rx_wol_magic_packet(struct ieee80211_hdr *hdr, unsigned int len)
{
	const unsigned char *data, *end, *mac;
	unsigned int found = 0;

	/*
	 * LIMITATION:
	 * We can only scan the first AR9170_BLOCK_SIZE [=~320] bytes
	 * for MAGIC patterns!
	 */

	mac = (const unsigned char *) AR9170_MAC_REG_MAC_ADDR_L;

	data = (u8 *)((unsigned long)hdr + ieee80211_hdrlen(hdr->frame_control));
	end = (u8 *)((unsigned long)hdr + len);

	/*
	 * scan for standard WOL Magic frame
	 *
	 * "A physical WakeOnLAN (Magic Packet) will look like this:
	 * ---------------------------------------------------------------
	 * | Synchronization Stream |  Target MAC |  Password (optional) |
	 * |  	6 octets	    |   96 octets |   0, 4 or 6		 |
	 * ---------------------------------------------------------------
	 *
	 * The Synchronization Stream is defined as 6 bytes of FFh.
	 * The Target MAC block contains 16 duplications of the IEEEaddress
	 * of the target, with no breaks or interruptions.
	 *
	 * The Password field is optional, but if present, contains either
	 * 4 bytes or 6 bytes. The WakeOnLAN dissector was implemented to
	 * dissect the password, if present, according to the command-line
	 * format that ether-wake uses, therefore, if a 4-byte password is
	 * present, it will be dissected as an IPv4 address and if a 6-byte
	 * password is present, it will be dissected as an Ethernet address.
	 *
	 * <http://wiki.wireshark.org/WakeOnLAN>
	 */

	while (data < end) {
		if (found >= 6) {
			if (*data == mac[found % 6])
				found++;
			else
				found = 0;
		}

		/* previous check might reset found counter */
		if (found < 6) {
			if (*data == 0xff)
				found++;
			else
				found = 0;
		}

		if (found == (6 + 16 * 6)) {
			return true;
		}

		data++;
	}

	return false;
}

static void wlan_wol_connect_callback(void __unused *dummy, bool success)
{
	if (success)
		fw.wlan.wol.lost_null = 0;
	else
		fw.wlan.wol.lost_null++;
}

static void wlan_wol_connection_monitor(void)
{
	struct carl9170_tx_null_superframe *nullf = &dma_mem.reserved.cmd.null;
	struct ieee80211_hdr *null = (struct ieee80211_hdr *) &nullf->f.null;

	if (!fw.wlan.fw_desc_available)
		return;

	memset(nullf, 0, sizeof(nullf));

	nullf->s.len = sizeof(struct carl9170_tx_superdesc) +
		     sizeof(struct ar9170_tx_hwdesc) +
		     sizeof(struct ieee80211_hdr);
	nullf->s.ri[0].tries = 3;
	nullf->s.assign_seq = true;
	nullf->s.queue = AR9170_TXQ_VO;
	nullf->f.hdr.length = sizeof(struct ieee80211_hdr) + FCS_LEN;

	nullf->f.hdr.mac.backoff = 1;
	nullf->f.hdr.mac.hw_duration = 1;
	nullf->f.hdr.mac.erp_prot = AR9170_TX_MAC_PROT_RTS;

	nullf->f.hdr.phy.modulation = AR9170_TX_PHY_MOD_OFDM;
	nullf->f.hdr.phy.bandwidth = AR9170_TX_PHY_BW_20MHZ;
	nullf->f.hdr.phy.chains = AR9170_TX_PHY_TXCHAIN_2;
	nullf->f.hdr.phy.tx_power = 29; /* 14.5 dBm */
	nullf->f.hdr.phy.mcs = AR9170_TXRX_PHY_RATE_OFDM_6M;

	/* format outgoing nullfunc */
	null->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
		IEEE80211_STYPE_NULLFUNC | IEEE80211_FCTL_TODS);

        memcpy(null->addr1, fw.wlan.wol.cmd.bssid, 6);
        memcpy(null->addr2, fw.wlan.wol.cmd.mac, 6);
        memcpy(null->addr3, fw.wlan.wol.cmd.bssid, 6);

	wlan_tx_fw(&nullf->s, wlan_wol_connect_callback);
}

static bool wlan_rx_wol_disconnect(const unsigned int rx_filter,
				   struct ieee80211_hdr *hdr,
				   unsigned int __unused len)
{
	const unsigned char *bssid;
	bssid = (const unsigned char *) AR9170_MAC_REG_BSSID_L;

	/* should catch both broadcast and unicast MLMEs */
	if (!(rx_filter & CARL9170_RX_FILTER_OTHER_RA)) {
		if (ieee80211_is_deauth(hdr->frame_control) ||
		    ieee80211_is_disassoc(hdr->frame_control))
			return true;
	}

	if (ieee80211_is_beacon(hdr->frame_control) &&
	    compare_ether_address(hdr->addr3, bssid)) {
		fw.wlan.wol.last_beacon = get_clock_counter();
	}

	return false;
}

#endif /* CARL9170FW_WOL_NL80211_TRIGGERS */

#ifdef CONFIG_CARL9170FW_WOL_PROBE_REQUEST

/*
 * Note: CONFIG_CARL9170FW_WOL_PROBE_REQUEST_SSID is not a real
 * string. We have to be careful not to add a \0 at the end.
 */
static const struct {
	u8 ssid_ie;
	u8 ssid_len;
	u8 ssid[sizeof(CONFIG_CARL9170FW_WOL_PROBE_REQUEST_SSID) - 1];
} __packed probe_req = {
	.ssid_ie = WLAN_EID_SSID,
	.ssid_len = sizeof(CONFIG_CARL9170FW_WOL_PROBE_REQUEST_SSID) - 1,
	.ssid = CONFIG_CARL9170FW_WOL_PROBE_REQUEST_SSID,
};

static bool wlan_rx_wol_probe_ssid(struct ieee80211_hdr *hdr, unsigned int len)
{
	const unsigned char *data, *end, *scan = (void *) &probe_req;

	/*
	 * IEEE 802.11-2007 7.3.2.1 specifies that the SSID is no
	 * longer than 32 octets.
	 */
	BUILD_BUG_ON((sizeof(CONFIG_CARL9170FW_WOL_PROBE_REQUEST_SSID) - 1) > 32);

	if (ieee80211_is_probe_req(hdr->frame_control)) {
		unsigned int i;
		end = (u8 *)((unsigned long)hdr + len);

		/*
		 * The position of the SSID information element inside
		 * a probe request frame is more or less "fixed".
		 */
		data = (u8 *)((struct ieee80211_mgmt *)hdr)->u.probe_req.variable;
		for (i = 0; i < (unsigned int)(probe_req.ssid_len + 1); i++) {
			if (scan[i] != data[i])
				return false;
		}

		return true;
	}

	return false;
}
#endif /* CONFIG_CARL9170FW_WOL_PROBE_REQUEST */

static void wlan_rx_wol(unsigned int rx_filter __unused, struct ieee80211_hdr *hdr __unused, unsigned int len __unused)
{
#ifdef CONFIG_CARL9170FW_WOL_NL80211_TRIGGERS
	/* Disconnect is always enabled */
	if (fw.wlan.wol.cmd.flags & CARL9170_WOL_DISCONNECT &&
	    rx_filter & CARL9170_RX_FILTER_MGMT)
		fw.wlan.wol.wake_up |= wlan_rx_wol_disconnect(rx_filter, hdr, len);

	if (fw.wlan.wol.cmd.flags & CARL9170_WOL_MAGIC_PKT &&
	    rx_filter & CARL9170_RX_FILTER_DATA)
		fw.wlan.wol.wake_up |= wlan_rx_wol_magic_packet(hdr, len);
#endif /* CONFIG_CARL9170FW_WOL_NL80211_TRIGGERS */

#ifdef CONFIG_CARL9170FW_WOL_PROBE_REQUEST
	if (rx_filter & CARL9170_RX_FILTER_MGMT)
		fw.wlan.wol.wake_up |= wlan_rx_wol_probe_ssid(hdr, len);
#endif /* CONFIG_CARL9170FW_WOL_PROBE_REQUEST */
}

static void wlan_wol_janitor(void)
{
	if (unlikely(fw.suspend_mode == CARL9170_HOST_SUSPENDED)) {
		if (fw.wlan.wol.cmd.flags & CARL9170_WOL_DISCONNECT) {
			/*
			 * connection lost after 10sec without receiving
			 * a beacon
			  */
			if (is_after_msecs(fw.wlan.wol.last_beacon, 10000))
				fw.wlan.wol.wake_up |= true;

			if (fw.wlan.wol.cmd.null_interval &&
			    is_after_msecs(fw.wlan.wol.last_null, fw.wlan.wol.cmd.null_interval))
				wlan_wol_connection_monitor();

			if (fw.wlan.wol.lost_null >= 5)
				fw.wlan.wol.wake_up |= true;
		}

		if (fw.wlan.wol.wake_up) {
			fw.suspend_mode = CARL9170_AWAKE_HOST;
			set(AR9170_USB_REG_WAKE_UP, AR9170_USB_WAKE_UP_WAKE);
		}
	}
}
#endif /* CONFIG_CARL9170FW_WOL */

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

#ifdef CONFIG_CARL9170FW_WOL
	if (unlikely(fw.suspend_mode == CARL9170_HOST_SUSPENDED)) {
		wlan_rx_wol(rx_filter, hdr, min(data_len,
			       (unsigned int)AR9170_BLOCK_SIZE));
	}
#endif /* CONFIG_CARL9170FW_WOL */

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
			wlan_trigger(AR9170_DMA_TRIGGER_RXQ);
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

static void wlan_send_buffered_cab(void)
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
#endif /* CONFIG_CARL9170FW_CAB_QUEUE */

static void handle_beacon_config(void)
{
	uint32_t bcn_count;

	bcn_count = get(AR9170_MAC_REG_BCN_COUNT);
	send_cmd_to_host(4, CARL9170_RSP_BEACON_CONFIG, 0x00,
			 (uint8_t *) &bcn_count);
}

static void handle_pretbtt(void)
{
#ifdef CONFIG_CARL9170FW_CAB_QUEUE
	fw.wlan.cab_flush_time = get_clock_counter();
#endif /* CONFIG_CARL9170FW_CAB_QUEUE */

	rf_psm();

	send_cmd_to_host(4, CARL9170_RSP_PRETBTT, 0x00,
			 (uint8_t *) &fw.phy.psm.state);
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
	wlan_send_buffered_cab();
#endif /* CONFIG_CARL9170FW_CAB_QUEUE */

	wlan_send_buffered_tx_status();

	wlan_send_buffered_ba();

#ifdef CONFIG_CARL9170FW_WOL
	wlan_wol_janitor();
#endif /* CONFIG_CARL9170FW_WOL */
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

enum {
	CARL9170FW_TX_MAC_BUMP = 4,
	CARL9170FW_TX_MAC_DEBUG = 6,
	CARL9170FW_TX_MAC_RESET = 7,
};

static void wlan_check_hang(void)
{
	struct dma_desc *desc;
	int i;

	for (i = AR9170_TXQ_SPECIAL; i >= AR9170_TXQ0; i--) {
		if (queue_empty(&fw.wlan.tx_queue[i])) {
			/* Nothing to do here... move along */
			continue;
		}

		/* fetch the current DMA queue position */
		desc = (struct dma_desc *)get_wlan_txq_addr(i);

		/* Stuck frame detection */
		if (unlikely(DESC_PAYLOAD(desc) == fw.wlan.last_super[i])) {
			fw.wlan.last_super_num[i]++;

			if (unlikely(fw.wlan.last_super_num[i] >= CARL9170FW_TX_MAC_RESET)) {
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
			if (unlikely(fw.wlan.last_super_num[i] >= CARL9170FW_TX_MAC_DEBUG)) {
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
			if (unlikely(fw.wlan.last_super_num[i] >= CARL9170FW_TX_MAC_BUMP)) {
				/*
				 * Hrrm, bump the queue a bit.
				 * maybe this will get it going again.
				 */

				wlan_dma_bump(i);
				wlan_trigger(BIT(i));
			}
#endif /* CONFIG_CARL9170FW_DMA_QUEUE_BUMP */
		} else {
			/* Nothing stuck */
			fw.wlan.last_super[i] = DESC_PAYLOAD(desc);
			fw.wlan.last_super_num[i] = 0;
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
	int i;

#ifdef CONFIG_CARL9170FW_RADIO_FUNCTIONS
	uint32_t rx_BB;
#endif /* CONFIG_CARL9170FW_RADIO_FUNCTIONS */

#ifdef CONFIG_CARL9170FW_NOISY_MAC_RESET
	INFO("MAC RESET");
#endif /* CONFIG_CARL9170FW_NOISY_MAC_RESET */

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

	val = AR9170_DMA_TRIGGER_RXQ;
	/* Reinitialize all WLAN TX DMA queues. */
	for (i = AR9170_TXQ_SPECIAL; i >= AR9170_TXQ0; i--) {
		struct dma_desc *iter;

		__for_each_desc_bits(iter, &fw.wlan.tx_queue[i], AR9170_OWN_BITS_SW);

		/* kill the stuck frame */
		if (!is_terminator(&fw.wlan.tx_queue[i], iter) &&
		    fw.wlan.last_super_num[i] >= CARL9170FW_TX_MAC_RESET &&
		    fw.wlan.last_super[i] == DESC_PAYLOAD(iter)) {
			struct carl9170_tx_superframe *super = get_super(iter);

			iter->status = AR9170_OWN_BITS_SW;
			/*
			 * Mark the frame as failed.
			 * The BAFAIL flag allows the frame to sail through
			 * wlan_tx_status without much "unstuck" trouble.
			 */
			iter->ctrl &= ~(AR9170_CTRL_FAIL);
			iter->ctrl |= AR9170_CTRL_BAFAIL;

			super->s.cnt = CARL9170_TX_MAX_RATE_TRIES;
			super->s.rix = CARL9170_TX_MAX_RETRY_RATES;

			fw.wlan.last_super_num[i] = 0;
			fw.wlan.last_super[i] = NULL;
			iter = iter->lastAddr->nextAddr;
		}

		set_wlan_txq_dma_addr(i, (uint32_t) iter);
		if (!is_terminator(&fw.wlan.tx_queue[i], iter))
			val |= BIT(i);

		DBG("Q:%d l:%d h:%p t:%p cu:%p it:%p ct:%x st:%x\n", i, queue_len(&fw.wlan.tx_queue[i]),
		     fw.wlan.tx_queue[i].head, fw.wlan.tx_queue[i].terminator,
		     get_wlan_txq_addr(i), iter, iter->ctrl, iter->status);
	}

	fw.wlan.soft_int |= AR9170_MAC_INT_RXC | AR9170_MAC_INT_TXC |
			    AR9170_MAC_INT_RETRY_FAIL;

	set(AR9170_MAC_REG_DMA_RXQ_ADDR, (uint32_t) fw.wlan.rx_queue.head);
	wlan_trigger(val);
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
