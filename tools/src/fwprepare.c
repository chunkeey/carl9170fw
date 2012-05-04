/*
 * Copyright 2012 Christian Lamparter <chunkeey@googlemail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2 of the License.
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

#include <stdlib.h>
#include <stdio.h>
#include <error.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "carlfw.h"

#include "compiler.h"
#include "pattern.h"

static void checksum_help(void)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "\tfwprepare FW-FILE\n");

	fprintf(stderr, "\nDescription:\n");
	fprintf(stderr, "\tThis simple utility prepares the firmware "
			"for release.\n");

	fprintf(stderr, "\nParameteres:\n");
	fprintf(stderr, "\t 'FW-FILE'	= firmware name\n");
	fprintf(stderr, "\n");
}

static int add_patterns(struct carlfw *fw) {
	const struct carl9170fw_otus_desc *otus_desc = NULL;
	struct carl9170fw_pattern_desc *pattern_desc = NULL;
	int to_add;

	otus_desc = carlfw_find_desc(fw, (uint8_t *) OTUS_MAGIC,
				     sizeof(*otus_desc),
				     CARL9170FW_OTUS_DESC_CUR_VER);
	if (!otus_desc) {
		fprintf(stderr, "No OTUS descriptor found\n");
		return -1;
	}

	if (!carl9170fw_supports(otus_desc->feature_set, CARL9170FW_PATTERN_GENERATOR)) {
		return 0;
	}

	pattern_desc = carlfw_find_desc(fw, (uint8_t *) PATTERN_MAGIC,
				      sizeof(*pattern_desc),
				      CARL9170FW_PATTERN_DESC_CUR_VER);

	if (!pattern_desc) {
		fprintf(stderr, "Firmware has the pattern generator feature set, but "
			"can't find a valid pattern descriptor\n");
		return 0;
	}

	to_add = pattern_desc->num_patterns -
		 ((pattern_desc->head.length - sizeof(*pattern_desc)) /
		 sizeof(struct carl9170fw_pattern_map_entry));
	if (to_add == 0) {
		/* been there, done that */
		return 0;
	}

	if (to_add == __CARL9170FW_NUM_PATTERNS) {
		struct carl9170fw_pattern_desc *tmp;
		unsigned int len, map_len;

		map_len = sizeof(struct carl9170fw_pattern_map_entry) * to_add;
		len = sizeof(*tmp) + map_len;
		tmp = malloc(len);
		if (!tmp)
			return -ENOMEM;

		pattern_desc = carlfw_desc_mod_len(fw, &pattern_desc->head, map_len);
		if (IS_ERR_OR_NULL(pattern_desc))
			return (int) PTR_ERR(pattern_desc);

		memcpy(&pattern_desc->patterns, pattern_names, map_len);
		return 0;
	} else {
		fprintf(stderr, "No idea, what you just did. But congrats: you broke it!");
		return -EINVAL;
	}
}

static int add_checksums(struct carlfw __unused *fw)
{
	/*
	 * No magic here, The checksum descriptor is added/update
	 * automatically in a subroutine of carlfw_store().
	 */
	return 0;
}

int main(int argc, char *args[])
{
	struct carlfw *fw = NULL;
	int err = 0;

	if (argc != 2) {
		err = -EINVAL;
		goto out;
	}

	fw = carlfw_load(args[1]);
	if (IS_ERR_OR_NULL(fw)) {
		err = PTR_ERR(fw);
		fprintf(stderr, "Failed to open file \"%s\" (%d).\n",
			args[1], err);
		goto out;
	}

	err = add_patterns(fw);
	if (err)
		goto out;

	err = add_checksums(fw);
	if (err)
		goto out;

	err = carlfw_store(fw);
	if (err) {
		fprintf(stderr, "Failed to apply checksum (%d).\n", err);
		goto out;
	}

out:
	switch (err) {
	case 0:
		fprintf(stdout, "firmware was prepared successfully.\n");
		break;
	case -EINVAL:
		checksum_help();
		break;
	default:
		break;
	}

	carlfw_release(fw);
	return err ? EXIT_FAILURE : EXIT_SUCCESS;
}
