/*
 * carl9170 firmware - used by the ar9170 wireless device
 *
 * initialization and main() loop
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
#include "timer.h"
#include "hostif.h"
#include "printf.h"
#include "gpio.h"
#include "wl.h"

#define AR9170_WATCH_DOG_TIMER		   0x100

static void init(void)
{
	led_init();

#ifdef CONFIG_CARL9170FW_DEBUG_UART
	uart_init();
#endif /* CONFIG_CARL9170FW_DEBUG_UART */

	/* 25/50/100ms timer (depends on cpu clock) */
	timer_init(0, 50000);

	/* USB init */
	usb_init();

	memset(&dma_mem, 0, sizeof(dma_mem));

	/* fill DMA rings */
	dma_init_descriptors();

	/* clear all interrupt */
	set(AR9170_MAC_REG_INT_CTRL, 0xffff);

	orl(AR9170_MAC_REG_AFTER_PNP, 1);

	/* Init watch dog control flag */
#ifdef CONFIG_CARL9170FW_WATCHDOG
	fw.watchdog_enable = 1;

	set(AR9170_TIMER_REG_WATCH_DOG, AR9170_WATCH_DOG_TIMER);
#else
	fw.watchdog_enable = 0;
	set(AR9170_TIMER_REG_WATCH_DOG, 0xffff);
#endif /* CONFIG_CARL9170FW_WATCHDOG */

#ifdef CONFIG_CARL9170FW_GPIO_INTERRUPT
	fw.cached_gpio_state.gpio = get(AR9170_GPIO_REG_PORT_DATA) &
				    CARL9170_GPIO_MASK;
#endif /* CONFIG_CARL9170FW_GPIO_INTERRUPT */

	/* this will get the downqueue moving. */
	down_trigger();
}

static void handle_fw(void)
{
	if (fw.watchdog_enable == 1)
		set(AR9170_TIMER_REG_WATCH_DOG, AR9170_WATCH_DOG_TIMER);

	if (fw.reboot)
		reboot();
}

static void __attribute__((noreturn)) main_loop(void)
{
	clock_set(true, AHB_40MHZ_OSC);

	/* main loop */
	while (1) {
		handle_fw();

		/*
		 * Due to frame order persevation, the wlan subroutines
		 * must be executed before handle_host_interface.
		 */
		handle_wlan();

		handle_host_interface();

		handle_usb();

		handle_timer();

		fw.counter++;
	}
}

/*
 * The bootcode will work with the device driver to load the firmware
 * onto the device's Program SRAM. The Program SRAM has a size of 32 KB
 * and also contains the stack, which grows down from 0x208000.
 *
 * The Program SRAM starts at address 0x200000 on the device.
 * The firmware entry point (0x200004) is located in boot.S.
 * we put _start() there with the linker script carl9170.lds.
 */

void __attribute__((noreturn)) start(void)
{
	/* initialize firmware context and DMA memory */
	memset(&fw, 0, sizeof(fw));

	/* watchdog magic pattern check */
	if ((get(AR9170_PWR_REG_WATCH_DOG_MAGIC) & 0xffff0000) == 0x12340000) {
		/* watch dog warm start */
		incl(AR9170_PWR_REG_WATCH_DOG_MAGIC);
		usb_trigger_out();
	} else if ((get(AR9170_PWR_REG_WATCH_DOG_MAGIC) & 0xffff0000) == 0x98760000) {
		/* suspend/resume */
	}

	/* write the magic pattern for watch dog */
	andl(AR9170_PWR_REG_WATCH_DOG_MAGIC, 0xFFFF);
	orl(AR9170_PWR_REG_WATCH_DOG_MAGIC, 0x12340000);

	init();

#ifdef CONFIG_CARL9170FW_DEBUG

	BUG("TEST BUG");
	BUG_ON(0x2b || !0x2b);
	INFO("INFO MESSAGE");

	/* a set of unique characters to detect transfer data corruptions */
	DBG("AaBbCcDdEeFfGgHhIiJjKkLlMmNnOoPpQqRrSsTtUuVvWwXxYyZz"
	    " ~`!1@2#3$4%%5^6&7*8(9)0_-+={[}]|\\:;\"'<,>.?/");
#endif /* CONFIG_CARL9170FW_DEBUG */

	main_loop();
}
