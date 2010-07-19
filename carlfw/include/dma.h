/*
 * carl9170 firmware - used by the ar9170 wireless device
 *
 * This module contains DMA descriptor related definitions.
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

#ifndef __CARL9170FW_DMA_H
#define __CARL9170FW_DMA_H

#include "config.h"
#include "types.h"
#include "compiler.h"
#include "hw.h"
#include "ieee80211.h"
#include "wlan.h"

struct dma_desc {
	volatile uint16_t status;	/* Descriptor status */
	volatile uint16_t ctrl;		/* Descriptor control */
	volatile uint16_t dataSize;	/* Data size */
	volatile uint16_t totalLen;	/* Total length */
	struct dma_desc *lastAddr;	/* Last address of this chain */
	union {
		uint8_t *_dataAddr;	/* Data buffer address */
		void *dataAddr;
	} __packed;
	struct dma_desc *nextAddr;	/* Next TD address */
} __packed;

/* (Up, Dn, 5x Tx, Rx), USB Int, (5x delayed Tx + retry), CAB, BA */
#define AR9170_TERMINATOR_NUMBER_B	8

#define AR9170_TERMINATOR_NUMBER_INT	1

#ifdef CONFIG_CARL9170FW_DELAYED_TX
#define AR9170_TERMINATOR_NUMBER_DELAY	6
#else
#define AR9170_TERMINATOR_NUMBER_DELAY	0
#endif /* CONFIG_CARL9170FW_DELAYED_TX */

#ifdef CONFIG_CARL9170FW_CAB_QUEUE
#define AR9170_TERMINATOR_NUMBER_CAB	1
#else
#define AR9170_TERMINATOR_NUMBER_CAB	0
#endif /* CONFIG_CARL9170FW_CAB_QUEUE */

#ifdef CONFIG_CARL9170FW_HANDLE_BACK_REQ
#define AR9170_TERMINATOR_NUMBER_BA	1
#else
#define AR9170_TERMINATOR_NUMBER_BA	0
#endif /* CONFIG_CARL9170FW_HANDLE_BACK_REQ */
#define AR9170_TERMINATOR_NUMBER (AR9170_TERMINATOR_NUMBER_B + \
				  AR9170_TERMINATOR_NUMBER_INT + \
				  AR9170_TERMINATOR_NUMBER_DELAY + \
				  AR9170_TERMINATOR_NUMBER_CAB + \
				  AR9170_TERMINATOR_NUMBER_BA)

#define AR9170_BLOCK_SIZE           (256 + 64)

#define AR9170_DESCRIPTOR_SIZE      (sizeof(struct dma_desc))

struct ar9170_tx_ba_frame {
	struct ar9170_tx_hwdesc hdr;
	struct ieee80211_ba ba;
} __packed;

struct carl9170_tx_ba_superframe {
	struct carl9170_tx_superdesc s;
	struct ar9170_tx_ba_frame f;
} __packed;

#define CARL9170_BA_BUFFER_LEN	(__roundup(sizeof(struct carl9170_tx_ba_superframe), 16))
#define CARL9170_RSP_BUFFER_LEN	AR9170_BLOCK_SIZE

struct carl9170_sram_reserved {
#ifdef CONFIG_CARL9170FW_HANDLE_BACK_REQ
	union {
		uint32_t buf[CARL9170_BA_BUFFER_LEN / sizeof(uint32_t)];
		struct carl9170_tx_ba_superframe ba;
	} ba;
#endif /* CONFIG_CARL9170FW_HANDLE_BACK_REQ */
	union {
		uint32_t buf[CARL9170_MAX_CMD_LEN / sizeof(uint32_t)];
		struct carl9170_cmd cmd;
	} cmd;

	union {
		uint32_t buf[CARL9170_RSP_BUFFER_LEN / sizeof(uint32_t)];
		struct carl9170_rsp rsp;
	} rsp;

	union {
		uint32_t buf[CARL9170_INTF_NUM][AR9170_MAC_BCN_LENGTH_MAX / sizeof(uint32_t)];
	} bcn;
};

/*
 * Memory layout in RAM:
 *
 * 0x100000			+--
 *				| terminator descriptors (dma_desc)
 *				|  - Up (to USB host)
 *				|  - Down (from USB host)
 *				|  - TX (5x, to wifi)
 *				|  - RX (from wifi)
 *				|  - CAB Queue
 *				|  - FW cmd & req descriptor
 *				|  - BlockAck descriptor
 *				|  - Delayed TX (5x)
 *				| total: AR9170_TERMINATOR_NUMBER
 *				+--
 *				| block descriptors (dma_desc)
 *				| (AR9170_BLOCK_NUMBER)
 * AR9170_BLOCK_BUFFER_BASE	+-- align to multiple of 64
 *				| block buffers (AR9170_BLOCK_SIZE each)
 *				| (AR9170_BLOCK_NUMBER)
 * approx. 0x117c00		+--
 *				| BA buffer (128 bytes)
 *				+--
 *				| CMD buffer (128 bytes)
 *				+--
 *				| RSP buffer (320 bytes)
 *				+--
 *				| BEACON buffer (256 bytes)
 *				+--
 *				| unaccounted space / padding
 *				+--
 * 0x18000
 */

#define AR9170_SRAM_SIZE		0x18000
#define CARL9170_SRAM_RESERVED		(sizeof(struct carl9170_sram_reserved))

#define AR9170_FRAME_MEMORY_SIZE	(AR9170_SRAM_SIZE - CARL9170_SRAM_RESERVED)

#define BLOCK_ALIGNMENT		64

#define NONBLOCK_DESCRIPTORS_SIZE	\
	(AR9170_DESCRIPTOR_SIZE * (AR9170_TERMINATOR_NUMBER))

#define NONBLOCK_DESCRIPTORS_SIZE_ALIGNED	\
	(ALIGN(NONBLOCK_DESCRIPTORS_SIZE, BLOCK_ALIGNMENT))

#define AR9170_BLOCK_NUMBER	((AR9170_FRAME_MEMORY_SIZE - NONBLOCK_DESCRIPTORS_SIZE_ALIGNED) / \
				 (AR9170_BLOCK_SIZE + AR9170_DESCRIPTOR_SIZE))

struct ar9170_data_block {
	uint8_t	data[AR9170_BLOCK_SIZE];
};

struct ar9170_dma_memory {
	struct dma_desc			terminator[AR9170_TERMINATOR_NUMBER];
	struct dma_desc			block[AR9170_BLOCK_NUMBER];
	struct ar9170_data_block	data[AR9170_BLOCK_NUMBER] __attribute__((aligned(BLOCK_ALIGNMENT)));
	struct carl9170_sram_reserved	reserved __attribute__((aligned(BLOCK_ALIGNMENT)));
};

extern struct ar9170_dma_memory dma_mem;

#define AR9170_DOWN_BLOCK_RATIO     2
#define AR9170_RX_BLOCK_RATIO       1
/* Tx 16*2 = 32 packets => 32*(5*320) */
#define AR9170_TX_BLOCK_NUMBER     (AR9170_BLOCK_NUMBER * AR9170_DOWN_BLOCK_RATIO / \
				   (AR9170_RX_BLOCK_RATIO + AR9170_DOWN_BLOCK_RATIO))
#define AR9170_RX_BLOCK_NUMBER     (AR9170_BLOCK_NUMBER - AR9170_TX_BLOCK_NUMBER)

/* Error code */
#define AR9170_ERR_FS_BIT           1
#define AR9170_ERR_LS_BIT           2
#define AR9170_ERR_OWN_BITS         3
#define AR9170_ERR_DATA_SIZE        4
#define AR9170_ERR_TOTAL_LEN        5
#define AR9170_ERR_DATA             6
#define AR9170_ERR_SEQ              7
#define AR9170_ERR_LEN              8

/* Status bits definitions */
/* Own bits definitions */
#define AR9170_OWN_BITS_MASK        0x3
#define AR9170_OWN_BITS_SW          0x0
#define AR9170_OWN_BITS_HW          0x1
#define AR9170_OWN_BITS_SE          0x2

/* Control bits definitions */
#define AR9170_CTRL_TXFAIL	1
#define AR9170_CTRL_BAFAIL	2
#define AR9170_CTRL_FAIL_MASK	(AR9170_CTRL_TXFAIL | AR9170_CTRL_BAFAIL)

/* First segament bit */
#define AR9170_CTRL_LS_BIT               0x100
/* Last segament bit */
#define AR9170_CTRL_FS_BIT               0x200

struct dma_queue {
	struct dma_desc *head;
	struct dma_desc *terminator;
};

#define DESC_PAYLOAD(a)			((void *)a->dataAddr)
#define DESC_PAYLOAD_OFF(a, offset)	((void *)((unsigned long)(a->_dataAddr) + offset))

struct dma_desc *dma_unlink_head(struct dma_queue *queue);
void dma_init_descriptors(void);
void dma_reclaim(struct dma_queue *q, struct dma_desc *desc);
void dma_put(struct dma_queue *q, struct dma_desc *desc);
void dma_queue_reclaim(struct dma_queue *dst, struct dma_queue *src);
void queue_dump(void);
void wlan_txq_hangfix(const unsigned int queue);

static inline __inline bool queue_empty(struct dma_queue *q)
{
	return q->head == q->terminator;
}

/*
 * Get a completed packet with # descriptors. Return the first
 * descriptor and pointer the head directly by lastAddr->nextAddr
 */
static inline __inline struct dma_desc *dma_dequeue_bits(struct dma_queue *q,
						uint16_t bits)
{
	struct dma_desc *desc = NULL;

	if ((q->head->status & AR9170_OWN_BITS_MASK) == bits)
		desc = dma_unlink_head(q);

	return desc;
}

static inline __inline struct dma_desc *dma_dequeue_not_bits(struct dma_queue *q,
						    uint16_t bits)
{
	struct dma_desc *desc = NULL;

	/* AR9170_OWN_BITS_HW will be filtered out here too. */
	if ((q->head->status & AR9170_OWN_BITS_MASK) != bits)
		desc = dma_unlink_head(q);

	return desc;
}

#define for_each_desc_bits(desc, queue, bits)				\
	while ((desc = dma_dequeue_bits(queue, bits)))

#define for_each_desc_not_bits(desc, queue, bits)			\
	while ((desc = dma_dequeue_not_bits(queue, bits)))

#define for_each_desc(desc, queue)					\
	while ((desc = dma_unlink_head(queue)))

#define __for_each_desc_bits(desc, queue, bits)				\
	for (desc = (queue)->head;					\
	     (desc != (queue)->terminator &&				\
	     (desc->status & AR9170_OWN_BITS_MASK) == bits);		\
	     desc = desc->lastAddr->nextAddr)

#define __while_desc_bits(desc, queue, bits)				\
	for (desc = (queue)->head;					\
	     (!queue_empty(queue) &&					\
	     (desc->status & AR9170_OWN_BITS_MASK) == bits);		\
	     desc = (queue)->head)

#define __for_each_desc(desc, queue)					\
	for (desc = (queue)->head;					\
	     desc != (queue)->terminator;				\
	     desc = (desc)->lastAddr->nextAddr)

#define __for_each_desc_safe(desc, tmp, queue)				\
	for (desc = (queue)->head, tmp = desc->lastAddr->nextAddr;	\
	     desc != (queue)->terminator;				\
	     desc = tmp, tmp = tmp->lastAddr->nextAddr)

#define __while_subdesc(desc, queue)					\
	for (desc = (queue)->head;					\
	     desc != (queue)->terminator;				\
	     desc = (desc)->nextAddr)

static inline __inline unsigned int queue_len(struct dma_queue *q)
{
	struct dma_desc *desc;
	unsigned int i = 0;

	__while_subdesc(desc, q)
		i++;

	return i;
}

/*
 * rearm a completed packet, so it will be processed agian.
 */
static inline __inline void dma_rearm(struct dma_desc *desc)
{
	/* Set OWN bit to HW */
	desc->status = ((desc->status & (~AR9170_OWN_BITS_MASK)) |
			AR9170_OWN_BITS_HW);
}

static inline void __check_desc(void)
{
	struct ar9170_dma_memory mem;
	BUILD_BUG_ON(sizeof(struct ar9170_data_block) != AR9170_BLOCK_SIZE);
	BUILD_BUG_ON(sizeof(struct dma_desc) != 20);

	BUILD_BUG_ON(sizeof(mem) > AR9170_SRAM_SIZE);

#ifdef CONFIG_CARL9170FW_HANDLE_BACK_REQ
	BUILD_BUG_ON(offsetof(struct carl9170_sram_reserved, ba.buf) & (BLOCK_ALIGNMENT - 1));
#endif /* CONFIG_CARL9170FW_HANDLE_BACK_REQ */
	BUILD_BUG_ON(offsetof(struct carl9170_sram_reserved, cmd.buf) & (BLOCK_ALIGNMENT - 1));
	BUILD_BUG_ON(offsetof(struct carl9170_sram_reserved, rsp.buf) & (BLOCK_ALIGNMENT - 1));
	BUILD_BUG_ON(offsetof(struct carl9170_sram_reserved, bcn.buf) & (BLOCK_ALIGNMENT - 1));
}

#endif /* __CARL9170FW_DMA_H */
