/*
 * carl9170user - userspace testing utility for ar9170 devices
 *
 * common API declaration
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

#ifndef __CARL9170USER_H
#define __CARL9170USER_H

#include "SDL.h"
#include "SDL_thread.h"

#include "carlfw.h"

#include "debug.h"
#include "hw.h"
#include "fwcmd.h"
#include "frame.h"
#include "eeprom.h"
#include "ieee80211.h"
#include "wlan.h"

struct carlu {
	SDL_cond *resp_pend;
	SDL_mutex *resp_lock;
	uint8_t *resp_buf;
	size_t resp_len;

	int tx_pending;
	uint8_t cookie;

	void (*tx_cb)(struct carlu *, struct frame *);
	void (*tx_fb_cb)(struct carlu *, struct frame *);
	void (*rx_cb)(struct carlu *, void *, unsigned int);
	int (*cmd_cb)(struct carlu *, struct carl9170_rsp *,
		      void *, unsigned int);

	struct carlfw *fw;

	struct ar9170_eeprom eeprom;

	struct frame_queue tx_sent_queue[__AR9170_NUM_TXQ];

	SDL_mutex *mem_lock;
	unsigned int dma_chunks;
	unsigned int dma_chunk_size;
	unsigned int used_dma_chunks;

	unsigned int extra_headroom;
	bool tx_stream;
	bool rx_stream;

	/* statistics */
	unsigned int rxed;
	unsigned int txed;

	unsigned long tx_octets;
	unsigned long rx_octets;
};

struct carlu_rate {
	int8_t rix;
	int8_t cnt;
	uint8_t flags;
};

struct carlu_tx_info_tx {
	unsigned int key;
};

struct carlu_tx_info {
	uint32_t flags;

	struct carlu_rate rates[CARL9170_TX_MAX_RATES];

	union {
		struct carlu_tx_info_tx tx;
	};
};

static inline struct carlu_tx_info *get_tx_info(struct frame *frame)
{
	return (void *) frame->cb;
}

void *carlu_alloc_driver(size_t size);
void carlu_free_driver(struct carlu *ar);

int carlu_fw_check(struct carlu *ar);
void carlu_fw_info(struct carlu *ar);

void carlu_rx(struct carlu *ar, struct frame *frame);
int carlu_tx(struct carlu *ar, struct frame *frame);
void carlu_tx_feedback(struct carlu *ar,
			  struct carl9170_rsp *cmd);
void carlu_handle_command(struct carlu *ar, void *buf, size_t len);

struct frame *carlu_alloc_frame(struct carlu *ar, unsigned int size);
void carlu_free_frame(struct carlu *ar, struct frame *frame);

int carlu_cmd_echo(struct carlu *ar, const uint32_t message);
int carlu_cmd_reboot(struct carlu *ar);
int carlu_cmd_read_eeprom(struct carlu *ar);
int carlu_cmd_mem_dump(struct carlu *ar, const uint32_t start,
			const unsigned int len, void *_buf);
int carlu_cmd_write_mem(struct carlu *ar, const uint32_t addr,
			const uint32_t val);

#endif /* __CARL9170USER_H */
