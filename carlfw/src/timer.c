/*
 * carl9170 firmware - used by the ar9170 wireless device
 *
 * timer code
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
#include "timer.h"
#include "gpio.h"
#include "printf.h"
#include "wl.h"

void timer_init(const unsigned int timer, const unsigned int interval)
{
	/* Set timer to periodic mode */
	orl(AR9170_TIMER_REG_CONTROL, BIT(timer));

	/* Set time interval */
	set(AR9170_TIMER_REG_TIMER0 + (timer << 2), interval - 1);

	/* Clear timer interrupt flag */
	orl(AR9170_TIMER_REG_INTERRUPT, BIT(timer));
}

void clock_set(enum cpu_clock_t clock_, bool on)
{
	/*
	 * Word of Warning!
	 * This setting does more than just mess with the CPU Clock.
	 * So watch out, if you need _stable_ timer interrupts.
	 */
#ifdef CONFIG_CARL9170FW_RADIO_FUNCTIONS
	if (fw.phy.frequency < 3000000)
		set(AR9170_PWR_REG_PLL_ADDAC, 0x5163);
	else
		set(AR9170_PWR_REG_PLL_ADDAC, 0x5143);
#else
	set(AR9170_PWR_REG_PLL_ADDAC, 0x5163);
#endif /* CONFIG_CARL9170FW_RADIO_FUNCTIONS */

	fw.ticks_per_usec = GET_VAL(AR9170_PWR_PLL_ADDAC_DIV,
		get(AR9170_PWR_REG_PLL_ADDAC));

	set(AR9170_PWR_REG_CLOCK_SEL, (uint32_t) ((on ? 0x70 : 0x600) | clock_));

	switch (clock_) {
	case AHB_20_22MHZ:
		fw.ticks_per_usec >>= 1;
	case AHB_40MHZ_OSC:
	case AHB_40_44MHZ:
		fw.ticks_per_usec >>= 1;
	case AHB_80_88MHZ:
		break;
	}
}

static void timer0_isr(void)
{
	wlan_timer();

#ifdef CONFIG_CARL9170FW_GPIO_INTERRUPT
	gpio_timer();
#endif /* CONFIG_CARL9170FW_GPIO_INTERRUPT */

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
