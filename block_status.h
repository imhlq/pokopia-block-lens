#ifndef BLOCK_LENS_BLOCK_STATUS_H
#define BLOCK_LENS_BLOCK_STATUS_H

#include <stddef.h>
#include "block_target.h"

/* Returns the visible status and a bar from 0 to 100, or -1 without a bar. */
int block_status(const MtHost *host, uint64_t base, const BlockTarget *target,
                 unsigned item_id, char *out, size_t capacity);

#endif
