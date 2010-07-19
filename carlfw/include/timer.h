/*
 * carl9170 firmware - used by the ar9170 wireless device
 *
 * Clock, Timer & Timing
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

#ifndef __CARL9170FW_TIMER_H
#define __CARL9170FW_TIMER_H

#include "config.h"

enum cpu_clock_t {
	AHB_40MHZ_OSC   = 0,
	AHB_20_22MHZ    = 1,
	AHB_40_44MHZ    = 2,
	AHB_80_88MHZ    = 3
};

#define AR9170_TICKS_PER_MICROSECOND	80

void handle_timer(void);
void timer_init(const unsigned int timer, const unsigned int interval);
void clock_set(const bool on, const enum cpu_clock_t _clock);

static inline __inline uint32_t get_clock_counter(void)
{
	return (get(AR9170_TIMER_REG_CLOCK_HIGH) << 16) | get(AR9170_TIMER_REG_CLOCK_LOW);
}

static inline __inline bool is_after_msecs(uint32_t t0, uint32_t msecs)
{
	return (get_clock_counter() - t0) / (AR9170_TICKS_PER_MICROSECOND * 1000) > msecs;
}

static inline __inline void delay(uint32_t msec)
{
	uint32_t t1, t2, dt;

	t1 = get_clock_counter();
	while (1) {
		t2 = get_clock_counter();
		dt = (t2 - t1) / AR9170_TICKS_PER_MICROSECOND / 1000;
		if (dt >= msec)
			break;
	}
}

static inline __inline void udelay(uint32_t usec)
{
	uint32_t t1, t2, dt;

	t1 = get_clock_counter();
	while (1) {
		t2 = get_clock_counter();
		dt = (t2 - t1) / AR9170_TICKS_PER_MICROSECOND;
		if (dt >= usec)
			break;
	}
}

#endif /* __CARL9170FW_TIMER_H */
