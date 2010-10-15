/*
 * carl9170user - userspace testing utility for ar9170 devices
 *
 * USB back-end API declaration
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

#ifndef __CARL9170USER_USB_H
#define __CARL9170USER_USB_H

#include "SDL.h"
#include "SDL_thread.h"
#include "libusb.h"
#include "frame.h"
#include "list.h"

#include "fwcmd.h"
#include <unistd.h>

#define AR9170_RX_BULK_BUFS		16
#define AR9170_RX_BULK_BUF_SIZE		8192
#define AR9170_RX_BULK_IRQ_SIZE		64

/* endpoints */
#define AR9170_EP_TX				(LIBUSB_ENDPOINT_OUT | AR9170_USB_EP_TX)
#define AR9170_EP_RX				(LIBUSB_ENDPOINT_IN  | AR9170_USB_EP_RX)
#define AR9170_EP_IRQ				(LIBUSB_ENDPOINT_IN  | AR9170_USB_EP_IRQ)
#define AR9170_EP_CMD				(LIBUSB_ENDPOINT_OUT | AR9170_USB_EP_CMD)

#define AR9170_TX_MAX_ACTIVE_URBS		8

#define CARL9170_FIRMWARE_FILE "/lib/firmware/carl9170-1"
void carlusb_reset_txep(struct carlu *ar);

struct carlusb {
	struct carlu common;
	libusb_device_handle *dev;
	libusb_context *ctx;

	SDL_Thread *event_thread;
	bool stop_event_polling;

	struct libusb_transfer *rx_ring[AR9170_RX_BULK_BUFS];

	struct libusb_transfer *rx_interrupt;
	unsigned char irq_buf[AR9170_RX_BULK_IRQ_SIZE];

	union {
		unsigned char buf[CARL9170_MAX_CMD_LEN];
		struct carl9170_cmd cmd;
		struct carl9170_rsp rsp;
	} cmd;

	struct list_head tx_queue;
	SDL_mutex *tx_queue_lock;
	unsigned int tx_queue_len;

	struct list_head dev_list;
	unsigned int idx;

	unsigned int miniboot_size;
	unsigned int rx_max;

	int event_pipe[2];
};

int usb_init(void);
void usb_exit(void);

struct carlu *carlusb_probe(void);
void carlusb_close(struct carlu *ar);

void carlusb_tx(struct carlu *ar, struct frame *frame);
int carlusb_fw_check(struct carlusb *ar);

int carlusb_cmd(struct carlu *_ar, uint8_t oid, uint8_t *cmd, size_t clen,
		uint8_t *rsp, size_t rlen);

int carlusb_print_known_devices(void);

#endif /* __CARL9170USER_USB_H */
