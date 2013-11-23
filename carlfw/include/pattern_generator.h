/*
 * carl9170 firmware - used by the ar9170 wireless device
 *
 * Pattern Generator definitions
 *
 * Copyright 2012, 2013 Christian Lamparter <chunkeey@googlemail.com>
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

#ifndef __CARL9170FW_PATTERN_GENERATOR_H
#define __CARL9170FW_PATTERN_GENERATOR_H

#include "generated/autoconf.h"
#include "types.h"
#include "compiler.h"
#include "fwdesc.h"
#include "pattern.h"

#if defined(CONFIG_CARL9170FW_PATTERN_GENERATOR)
void pattern_generator(void);

#else
static inline void pattern_generator(void)
{
}

#endif /* CONFIG_CARL9170FW_PATTERN_GENERATOR */

#endif /* __CARL9170FW_PATTERN_GENERATOR_H */
