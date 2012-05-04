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

#include "../../carlfw/include/radar.h"
#include "carlfw.h"

#include "compiler.h"

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

static int add_radars(struct carlfw *fw) {
	const struct carl9170fw_otus_desc *otus_desc = NULL;
	struct carl9170fw_radar_desc *radar_desc = NULL;
	int radars_to_add;

	otus_desc = carlfw_find_desc(fw, (uint8_t *) OTUS_MAGIC,
				     sizeof(*otus_desc),
				     CARL9170FW_OTUS_DESC_CUR_VER);
	if (!otus_desc) {
		fprintf(stderr, "No OTUS descriptor found\n");
		return -1;
	}

	if (!carl9170fw_supports(otus_desc->feature_set, CARL9170FW_RADAR_PATTERN_GENERATOR)) {
		return 0;
	}

	radar_desc = carlfw_find_desc(fw, (uint8_t *) RADAR_MAGIC,
				      sizeof(*radar_desc),
				      CARL9170FW_RADAR_DESC_CUR_VER);

	if (!radar_desc) {
		fprintf(stderr, "Firmware has radar pattern feature set, but "
			"can't find a valid radar descriptor\n");
	}

	radars_to_add = radar_desc->num_radars -
		 ((radar_desc->head.length - sizeof(*radar_desc)) /
		 sizeof(struct carl9170fw_radar_map_entry));
	if (radars_to_add == 0) {
		/* been there, done that */
		return 0;
	}

	if (radars_to_add == __CARL9170FW_NUM_RADARS) {
		struct carl9170fw_radar_desc *tmp;
		unsigned int len, map_len;

		map_len = sizeof(struct carl9170fw_radar_map_entry) * radars_to_add;
		len = sizeof(*tmp) + map_len;
		tmp = malloc(len);
		if (!tmp)
			return -ENOMEM;

		radar_desc = carlfw_desc_mod_len(fw, &radar_desc->head, map_len);
		if (IS_ERR_OR_NULL(radar_desc))
			return (int) PTR_ERR(radar_desc);

		memcpy(&radar_desc->radars, radar_names, map_len);
		return 0;
	} else {
		fprintf(stderr, "don't know what you did, but congrats you broke it!");
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

	err = add_radars(fw);
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
