/*
 * carl9170 firmware - used by the ar9170 wireless device
 *
 * Pattern pulse definitions
 *
 * Copyright 2012 Christian Lamparter <chunkeey@googlemail.com>
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

#ifndef __CARL9170FW_PATTERN_H
#define __CARL9170FW_PATTERN_H

#include "types.h"
#include "compiler.h"
#include "fwdesc.h"

enum PATTERN_TYPE {
	NO_PATTERN = 0,
	ONE_KHZ,
	TEN_KHZ,

	ONE_TWO_KHZ,

	FCC1,
	FCC4,

	ETSIFIXED,

	/* keep last */
	__CARL9170FW_NUM_PATTERNS
};

struct pattern_pulse_info {
	unsigned int pulse_width;
	unsigned int pulse_interval;
	uint32_t     pulse_pattern;
	uint32_t     pulse_mode;
};

struct pattern_info {
	unsigned int pulses;
	const struct pattern_pulse_info *pattern;
};

static const struct pattern_pulse_info pattern_NO_PATTERN[0] = {  };
static const struct pattern_pulse_info pattern_ONE_KHZ[] = {
	{
		.pulse_width = 1,
		.pulse_interval = 1000,
		.pulse_pattern = 0xaa55,
		.pulse_mode    = 0x17f01,
	},
};

static const struct pattern_pulse_info pattern_TEN_KHZ[] = {
	{
		.pulse_width = 1,
		.pulse_interval = 100,
		.pulse_pattern = 0xaa55,
		.pulse_mode    = 0x17f01,
	},
};

static const struct pattern_pulse_info pattern_ONE_TWO_KHZ[] = {
	{
		.pulse_width = 1,
		.pulse_interval = 1000,
		.pulse_pattern = 0xaa55,
		.pulse_mode    = 0x17f01,
	},

	{
		.pulse_width = 10,
		.pulse_interval = 500,
		.pulse_pattern = 0xaa55,
		.pulse_mode    = 0x17f01,
	},
};

/*
 * Data taken from:
 * <http://linuxwireless.org/en/developers/DFS>
 */

/* FCC Test Signal 1 - 1us pulse, 1428 us interval */
static const struct pattern_pulse_info pattern_FCC1[] = {
	{
		.pulse_width = 1,
		.pulse_interval = 1428,
		.pulse_pattern = 0xaa55,
		.pulse_mode    = 0x17f01,
	},
};

/* FCC Test Signal 4 - 11-20us pulse, 200-500 us interval */
static const struct pattern_pulse_info pattern_FCC4[] = {
	{
		.pulse_width = 11,
		.pulse_interval = 200,
		.pulse_pattern = 0xaa55,
		.pulse_mode    = 0x7f01,
	},
};

/* ETSI Test Signal 1 (Fixed) - 1us Pulse, 750 us interval */
static const struct pattern_pulse_info pattern_ETSIFIXED[] = {
	{
		.pulse_width = 1,
		.pulse_interval = 750,
		.pulse_pattern = 0xaa55,
		.pulse_mode    = 0x7f01,
	},
};


#define ADD_RADAR(name) [name] = { .pulses = ARRAY_SIZE(pattern_## name), .pattern = pattern_## name }

static const struct pattern_info patterns[__CARL9170FW_NUM_PATTERNS] = {
	ADD_RADAR(NO_PATTERN),
	ADD_RADAR(ONE_KHZ),
	ADD_RADAR(TEN_KHZ),
	ADD_RADAR(ONE_TWO_KHZ),
	ADD_RADAR(FCC1),
	ADD_RADAR(FCC4),
	ADD_RADAR(ETSIFIXED),
};

#define MAP_ENTRY(idx) [idx] = { .index = idx, .name = # idx , }
#define NAMED_MAP_ENTRY(idx, named) [idx] = {.index = idx, .name = named, }

static const struct carl9170fw_pattern_map_entry pattern_names[__CARL9170FW_NUM_PATTERNS] = {
	MAP_ENTRY(NO_PATTERN),
	MAP_ENTRY(ONE_KHZ),
	MAP_ENTRY(TEN_KHZ),
	MAP_ENTRY(ONE_TWO_KHZ),

	MAP_ENTRY(FCC1),
	MAP_ENTRY(FCC4),

	MAP_ENTRY(ETSIFIXED),
};

#endif /* __CARL9170FW_PATTERN_H */
