/*
 * carl9170 firmware - used by the ar9170 wireless device
 *
 * pattern generator
 *
 * Copyright 2013	Christian Lamparter <chunkeey@googlemail.com>
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
#include "pattern_generator.h"
#include "fwdsc.h"
#include "timer.h"

#if defined(CONFIG_CARL9170FW_PATTERN_GENERATOR)

void pattern_generator(void)
{
	if (fw.phy.state == CARL9170_PHY_ON) {
		if (likely(fw.wlan.soft_pattern == NO_PATTERN ||
		    fw.wlan.soft_pattern >= __CARL9170FW_NUM_PATTERNS))
			return;

		const struct pattern_info *pattern = &patterns[fw.wlan.soft_pattern];
		if (pattern->pulses >= fw.wlan.pattern_index) {
			fw.wlan.pattern_index = 0;
		}

		if (pattern->pulses > fw.wlan.pattern_index) {
			const struct pattern_pulse_info *ppi = &pattern->pattern[fw.wlan.pattern_index];
			if (is_after_usecs(fw.wlan.pattern_last, ppi->pulse_interval)) {
				fw.wlan.pattern_last = get_clock_counter();
				set(0x1C3BC0, ppi->pulse_pattern);
				set(0x1C3BBC, ppi->pulse_mode);
				udelay(ppi->pulse_width);
				set(0x1C3BBC, ~ppi->pulse_mode);
				fw.wlan.pattern_index++;
			}
		}
	}
}

#endif /* CONFIG_CONFIG_CARL9170FW_RADAR */
