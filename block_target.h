/* SPDX-License-Identifier: MIT */
#ifndef BLOCK_LENS_BLOCK_TARGET_H
#define BLOCK_LENS_BLOCK_TARGET_H

#include <stdbool.h>
#include <modtopia/modtopia.h>

typedef struct BlockTarget {
    uint16_t terrain_id;
    uint16_t part_variation;
    uint16_t unresolved_part_marker;
    uint16_t x, y, z;
} BlockTarget;

/* Call on the game thread. Returns false when the view has no block target. */
bool block_target(const MtHost *host, uint64_t base, BlockTarget *target);

#endif
