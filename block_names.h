/* SPDX-License-Identifier: MIT */
#ifndef BLOCK_LENS_BLOCK_NAMES_H
#define BLOCK_LENS_BLOCK_NAMES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <modtopia/modtopia.h>

typedef struct BlockItem {
    uint16_t name_id;
    uint16_t item_id;
} BlockItem;

BlockItem block_item(uint16_t terrain_id, uint16_t part_variation);

/* Call from the game thread. The result uses the game's selected language. */
bool block_name(const MtHost *host, uint64_t base, uint16_t name_id, char *out, size_t capacity);

#endif
