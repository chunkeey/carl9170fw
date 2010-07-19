/*
 * carl9170 firmware - used by the ar9170 wireless device
 *
 * Clock, Timer & Timing functions
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
#include "printf.h"
#include "timer.h"
#include "wl.h"
#include "gpio.h"

void timer_init(const unsigned int timer, const unsigned int interval)
{
	/* Set timer to periodic mode */
	orl(AR9170_TIMER_REG_CONTROL, BIT(timer));

	/* Set time interval */
	set(AR9170_TIMER_REG_TIMER0 + (timer << 2), interval - 1);

	/* Clear timer interrupt flag */
	orl(AR9170_TIMER_REG_INTERRUPT, BIT(timer));
}

static void clock_calibrate(void)
{
	uint32_t t0, loop = 13;

	t0 = get_clock_counter();

	/*
	 * TODO:
	 * Write this code in assembler, so the reading is accurate
	 * and can be used to correct the timer intervals.
	 */
	while (((get_clock_counter() - t0) & (BIT(18)-1)) < 1000)
		loop += 9;	/* really rough uOP estimation */

	fw.bogoclock = loop;
}

void clock_set(const bool on, const enum cpu_clock_t _clock)
{
	/*
	 * Word of Warning!
	 * This setting does more than just mess with the CPU Clock.
	 * So watch out, if you need _stable_ timer interrupts.
	 */

	set(AR9170_PWR_REG_CLOCK_SEL, (uint32_t) ((on ? 0x70 : 0x600) | _clock));
	clock_calibrate();
}

static void timer0_isr(void)
{
	wlan_timer();

#ifdef CONFIG_CARL9170FW_GPIO_INTERRUPT
	gpio_timer();
#endif /* CONFIG_CARL9170FW_GPIO_INTERRUPT */

#ifdef CONFIG_CARL9170FW_USB_WATCHDOG
	usb_watchdog_timer();
#endif /* CONFIG_CARL9170FW_USB_WATCHDOG */

#ifdef CONFIG_CARL9170FW_DEBUG_LED_HEARTBEAT
	set(AR9170_GPIO_REG_PORT_DATA, get(AR9170_GPIO_REG_PORT_DATA) ^ 1);
#endif /* CONFIG_CARL9170FW_DEBUG_LED_HEARTBEAT */
}

void handle_timer(void)
{
	uint32_t intr;

	intr = get(AR9170_TIMER_REG_INTERRUPT);

	/* ACK timer interrupt */
	set(AR9170_TIMER_REG_INTERRUPT, intr);

#define HANDLER(intr, flag, func)			\
	do {						\
		if ((intr & flag) != 0) {		\
			intr &= ~flag;			\
			func();				\
		}					\
	} while (0)

	HANDLER(intr, BIT(0), timer0_isr);

	if (intr)
		DBG("Unhandled Timer Event %x", (unsigned int) intr);

#undef HANDLER
}
