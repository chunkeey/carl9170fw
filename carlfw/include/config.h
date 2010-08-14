/*
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

#include "generated/autoconf.h"
#include "version.h"
#include "types.h"
#include "compiler.h"
#include "fwcmd.h"

#ifndef __CARL9170FW_CONFIG_H
#define __CARL9170FW_CONFIG_H

#define __CARL9170FW__

#define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

#if GCC_VERSION < 40400
# error "This firmware will not work if it is compiled with gcc versions < 4.4"
# error "See: http://gcc.gnu.org/gcc-4.4/changes.html / Caveats No. 4"
#endif

#if ((defined CONFIG_CARL9170FW_PRINTF) &&		\
     (!defined CONFIG_CARL9170FW_DEBUG_USB) &&		\
    (!defined CONFIG_CARL9170FW_DEBUG_UART))
# warning "You have disabled all debug message transports."
# warning "However CONFIG_CARL9170FW_PRINTF is still set..."
# warning "Which is a waste of firmware space, if you ask me."
#endif

#define CARL9170_TX_STATUS_NUM		(CARL9170_RSP_TX_STATUS_NUM)
#define CARL9170_INT_RQ_CACHES		16
#define AR9170_INT_MAGIC_HEADER_SIZE	12
#define CARL9170_TBTT_DELTA		(CARL9170_PRETBTT_KUS + 1)

#define CARL9170_GPIO_MASK		(AR9170_GPIO_PORT_WPS_BUTTON_PRESSED)

#ifdef CONFIG_CARL9170FW_VIFS_NUM
#define CARL9170_INTF_NUM		(1 + CONFIG_CARL9170FW_VIFS_NUM)
#else
#define CARL9170_INTF_NUM		(1)
#endif /* CONFIG_CARL9170FW_VIFS_NUM */

#if ((defined CONFIG_CARL9170FW_DEBUG) ||	\
     (defined CONFIG_CARL9170FW_LOOPBACK))
#define CARL9170FW_UNUSABLE	y
#endif

static inline void __config_check(void)
{
	BUILD_BUG_ON(!CARL9170_TX_STATUS_NUM);
	BUILD_BUG_ON(CARL9170_INTF_NUM < 1);

#ifdef CONFIG_CARL9170FW_HANDLE_BACK_REQ
	BUILD_BUG_ON(!CONFIG_CARL9170FW_BACK_REQS_NUM);
#endif /* CONFIG_CARL9170FW_HANDLE_BACK_REQ */
}

#endif /* __CARL9170FW_CONFIG_H */
