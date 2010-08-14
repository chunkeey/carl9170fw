/*
 * carl9170 firmware - used by the ar9170 wireless device
 *
 * Firmware context definition
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

#ifndef __CARL9170FW_CARL9170_H
#define __CARL9170FW_CARL9170_H

#include "generated/autoconf.h"
#include "version.h"
#include "config.h"
#include "types.h"
#include "compiler.h"
#include "fwcmd.h"
#include "hw.h"
#include "dma.h"
#include "usb.h"
#include "cmd.h"

struct carl9170_bar_ctx {
	uint8_t ta[6];
	uint8_t ra[6];
	__le16 start_seq_num;
	__le16 control;
	__le32 phy;
};

#ifdef CONFIG_CARL9170FW_CAB_QUEUE
enum carl9170_cab_trigger {
	CARL9170_CAB_TRIGGER_EMPTY	= 0,
	CARL9170_CAB_TRIGGER_ARMED	= BIT(0),
	CARL9170_CAB_TRIGGER_DEFER	= BIT(1),
};
#endif /* CONFIG_CARL9170FW_CAB_QUEUE */

enum carl9170_ep0_action {
	CARL9170_EP0_NO_ACTION		= 0,
	CARL9170_EP0_STALL		= BIT(0),
	CARL9170_EP0_TRIGGER		= BIT(1),
};

enum carl9170_mac_reset_state {
	CARL9170_MAC_RESET_OFF		= 0,
	CARL9170_MAC_RESET_ARMED,
	CARL9170_MAC_RESET_RESET,
	CARL9170_MAC_RESET_FORCE,
};

/*
 * This platform - being an odd 32-bit architecture - prefers to
 * have 32-Bit variables.
 */

struct firmware_context_struct {
	/* timer / clocks */
	unsigned int bogoclock;			/* supposed to be CPU clock in KHz */
	unsigned int counter;			/* main() cycles */

	/* misc */
	unsigned int watchdog_enable;
	unsigned int reboot;

	struct {
		/* Host Interface DMA queues */
		struct dma_queue up_queue;	/* used to send frames to the host */
		struct dma_queue down_queue;	/* stores incoming frames from the host */
	} pta;

	struct {
		/* Hardware DMA queues */
		struct dma_queue tx_queue[__AR9170_NUM_TX_QUEUES];	/* wlan tx queue */
		struct dma_queue rx_queue;				/* wlan rx queue */

#ifdef CONFIG_CARL9170FW_DELAYED_TX
		struct dma_queue tx_delay[__AR9170_NUM_TX_QUEUES];
		struct dma_queue tx_retry;
		unsigned int tx_trigger;
#endif /* CONFIG_CARL9170FW_DELAYED_TX */

		/* Hardware DMA queue unstuck/fix detection */
		unsigned int last_tx_desc_num[__AR9170_NUM_TX_QUEUES];
		struct dma_desc *last_tx_desc[__AR9170_NUM_TX_QUEUES];
		unsigned int rx_total;
		unsigned int rx_overruns;
		unsigned int mac_reset;

#ifdef CONFIG_CARL9170FW_CAB_QUEUE
		/* CAB */
		struct dma_queue cab_queue;
		unsigned int cab_queue_len,
			     cab_flush_time;
		enum carl9170_cab_trigger cab_flush_trigger;
#endif /* CONFIG_CARL9170FW_CAB_QUEUE */

		/* tx status */
		unsigned int tx_status_pending,
			     tx_status_head_idx,
			     tx_status_tail_idx;
		struct carl9170_tx_status tx_status_cache[CARL9170_TX_STATUS_NUM];

#ifdef CONFIG_CARL9170FW_HANDLE_BACK_REQ
		/* BA(R) Request Handler */
		struct dma_desc *ba_desc;
		struct carl9170_bar_ctx ba_cache[CONFIG_CARL9170FW_BACK_REQS_NUM];
		unsigned int ba_desc_available,
			     ba_tail_idx,
			     ba_head_idx;
#endif /* CONFIG_CARL9170FW_HANDLE_BACK_REQ */
	} wlan;

	struct {
		unsigned int config,
			     interface_setting,
			     alternate_interface_setting;
		enum carl9170_ep0_action ep0_action;

		void *ep0_txrx_buffer;
		unsigned int ep0_txrx_len,
			     ep0_txrx_pos;

		struct ar9170_usb_config *cfg_desc;
		struct ar9170_usb_config *os_cfg_desc;

		/*
		 * special buffers for command & response handling
		 *
		 * the firmware uses a sort of ring-buffer to communicate
		 * to the host.
		 */
		unsigned int int_pending,
			     int_desc_available,
			     int_head_index,
			     int_tail_index;
		struct dma_desc *int_desc;
		struct carl9170_rsp int_buf[CARL9170_INT_RQ_CACHES];

#ifdef CONFIG_CARL9170FW_DEBUG_USB
		/* USB printf */
		unsigned int put_index;
		uint8_t put_buffer[CARL9170_MAX_CMD_PAYLOAD_LEN];
#endif /* CONFIG_CARL9170FW_DEBUG_USB */

#ifdef CONFIG_CARL9170FW_USB_WATCHDOG
		struct carl9170_watchdog_cmd watchdog;
#endif /* CONFIG CARL9170FW_USB_WATCHDOG */
	} usb;

	struct {
#ifdef CONFIG_CARL9170FW_RADIO_FUNCTIONS
		/* (cached) ar9170_rf_init */

		/* PHY/RF state */
		unsigned int frequency;
		unsigned int ht_settings;

#ifdef CONFIG_CARL9170FW_PSM
		struct carl9170_psm psm;
#endif /* CONFIG_CARL9170FW_PSM */
#endif /* CONFIG_CARL9170FW_RADIO_FUNCTIONS */
	} phy;

#ifdef CONFIG_CARL9170FW_GPIO_INTERRUPT
	struct carl9170_gpio cached_gpio_state;
#endif /*CONFIG_CARL9170FW_GPIO_INTERRUPT */
};

/*
 * global firmware context struct.
 *
 * NOTE: This struct will zeroed out in start()
 */
extern struct firmware_context_struct fw;
#endif /* __CARL9170FW_CARL9170_H */
