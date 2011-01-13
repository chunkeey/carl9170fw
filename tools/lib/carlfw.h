#ifndef __CARLFW_H
#define __CARLFW_H

#include <linux/types.h>
#include "compiler.h"
#include "fwdesc.h"
#include "list.h"

struct carlfw;

void carlfw_release(struct carlfw *fw);
struct carlfw *carlfw_load(const char *basename);
int carlfw_store(struct carlfw *fw);
void *carlfw_find_desc(struct carlfw *fw,
	const uint8_t descid[4], const unsigned int len,
	const uint8_t compatible_revision);

int carlfw_desc_add_tail(struct carlfw *fw,
			 const struct carl9170fw_desc_head *desc);

int carlfw_desc_add(struct carlfw *fw,
		    const struct carl9170fw_desc_head *desc,
		    struct carl9170fw_desc_head *prev,
		    struct carl9170fw_desc_head *next);

void *carlfw_desc_mod_len(struct carlfw *fw,
			  struct carl9170fw_desc_head *desc,
			  size_t len);

int carlfw_desc_add_before(struct carlfw *fw,
			   const struct carl9170fw_desc_head *desc,
			   struct carl9170fw_desc_head *pos);

void carlfw_desc_unlink(struct carlfw *fw,
			struct carl9170fw_desc_head *desc);

void carlfw_desc_del(struct carlfw *fw,
		     struct carl9170fw_desc_head *entry);

void *carlfw_desc_next(struct carlfw *fw,
		       struct carl9170fw_desc_head *pos);

void *carlfw_mod_tailroom(struct carlfw *fw, ssize_t len);
void *carlfw_mod_headroom(struct carlfw *fw, ssize_t len);

void *carlfw_get_fw(struct carlfw *fw, size_t *len);

unsigned int carlfw_get_descs_num(struct carlfw *fw);
unsigned int carlfw_get_descs_size(struct carlfw *fw);
#endif /* __CARLFW_H */
