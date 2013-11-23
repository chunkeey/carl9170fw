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

#if defined(CARL9170FW_PATTERN_GENERATOR)

void radar_pattern_generator(void)
{
	if (fw.phy.state == CARL9170_PHY_ON) {
		if (likely(fw.wlan.soft_radar == NO_RADAR ||
		    fw.wlan.soft_radar >= __CARL9170FW_NUM_RADARS))
			return;

		const struct radar_info *radar = &radars[fw.wlan.soft_radar];
		if (radar->pulses >= fw.wlan.pattern_index) {
			fw.wlan.pattern_index = 0;
		}

		if (radar->pulses > fw.wlan.pattern_index) {
			const struct radar_info_pattern *pattern = &radar->pattern[fw.wlan.pattern_index];
			if (is_after_usecs(fw.wlan.radar_last, pattern->pulse_interval)) {
				fw.wlan.radar_last = get_clock_counter();
				set(0x1C3BC0, pattern->pulse_pattern);
				set(0x1C3BBC, pattern->pulse_mode);
				udelay(pattern->pulse_width);
				set(0x1C3BBC, ~pattern->pulse_mode);
				fw.wlan.pattern_index++;
			}
		}
	}
}

#endif /* CONFIG_CARL9170FW_RADAR */
