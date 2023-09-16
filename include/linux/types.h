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

#ifndef __LINUX_TYPES_H
#define __LINUX_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/cdefs.h>

#if BYTE_ORDER == BIG_ENDIAN
#error	"big endian is not supported by target"
#endif

typedef uint16_t	__le16;
typedef uint32_t	__le32;
typedef uint64_t	__le64;

typedef uint8_t		u8;
typedef uint8_t		__u8;
typedef uint16_t	u16;
typedef uint16_t	__u16;
typedef uint32_t	u32;
typedef uint32_t	__u32;
typedef uint64_t	u64;
typedef uint64_t	__u64;
typedef int8_t		s8;
typedef int8_t		__s8;
typedef int16_t		s16;
typedef int16_t		__s16;
typedef int32_t		s32;
typedef int32_t		__s32;
typedef int64_t		s64;
typedef int64_t		__s64;

#define cpu_to_le16(x) ((__le16)(uint16_t)(x))
#define le16_to_cpu(x) ((uint16_t)(__le16)(x))
#define cpu_to_le32(x) ((__le32)(uint32_t)(x))
#define le32_to_cpu(x) ((uint32_t)(__le32)(x))
#define cpu_to_le64(x) ((__le64)(uint64_t)(x))
#define le64_to_cpu(x) ((uint64_t)(__le64)(x))

typedef uint16_t	__be16;
typedef uint32_t	__be32;
typedef uint64_t	__be64;

#define cpu_to_be64 __cpu_to_be64
#define be64_to_cpu __be64_to_cpu
#define cpu_to_be32 __cpu_to_be32
#define be32_to_cpu __be32_to_cpu
#define cpu_to_be16 __cpu_to_be16
#define be16_to_cpu __be16_to_cpu
#define cpu_to_be64p __cpu_to_be64p
#define be64_to_cpup __be64_to_cpup
#define cpu_to_be32p __cpu_to_be32p
#define be32_to_cpup __be32_to_cpup
#define cpu_to_be16p __cpu_to_be16p
#define cpu_to_be64s __cpu_to_be64s
#define be64_to_cpus __be64_to_cpus
#define cpu_to_be32s __cpu_to_be32s
#define be32_to_cpus __be32_to_cpus
#define cpu_to_be16s __cpu_to_be16s
#define be16_to_cpus __be16_to_cpus
#define be16_to_cpup __be16_to_cpup

#endif /* __LINUX_TYPES_H */
