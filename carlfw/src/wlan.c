/*
 * carl9170 firmware - used by the ar9170 wireless device
 *
 * Interface to the WLAN part of the chip
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

static void wlan_check_rx_overrun(void)
{
	uint32_t overruns, total;

	fw.tally.rx_total += total = get(AR9170_MAC_REG_RX_TOTAL);
	fw.tally.rx_overrun += overruns = get(AR9170_MAC_REG_RX_OVERRUN);
	if (unlikely(overruns)) {
		if (overruns == total) {
			DBG("RX Overrun");
			fw.wlan.mac_reset++;
		}

		wlan_trigger(AR9170_DMA_TRIGGER_RXQ);
	}
}

static void handle_beacon_config(void)
{
	uint32_t bcn_count;

	bcn_count = get(AR9170_MAC_REG_BCN_COUNT);
	send_cmd_to_host(4, CARL9170_RSP_BEACON_CONFIG, 0x00,
			 (uint8_t *) &bcn_count);
}

static void handle_pretbtt(void)
{
	fw.wlan.cab_flush_time = get_clock_counter();

#ifdef CONFIG_CARL9170FW_RADIO_FUNCTIONS
	rf_psm();

	send_cmd_to_host(4, CARL9170_RSP_PRETBTT, 0x00,
			 (uint8_t *) &fw.phy.psm.state);
#endif /* CONFIG_CARL9170FW_RADIO_FUNCTIONS */
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
	wlan_send_buffered_cab();

	wlan_send_buffered_tx_status();

	wlan_send_buffered_ba();

	wol_janitor();
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

	HANDLER(intr, AR9170_MAC_INT_RXC, handle_wlan_rx);

	HANDLER(intr, (AR9170_MAC_INT_TXC | AR9170_MAC_INT_RETRY_FAIL),
		handle_wlan_tx_completion);

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
