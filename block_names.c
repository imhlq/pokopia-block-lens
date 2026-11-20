/* SPDX-License-Identifier: MIT */
#include "block_names.h"

#include "block_names_data.inc"

/* Pokopia 2.0.1, main 76a489f3bd3642fa5e18bd83d32c98bacc5f7d0e. The
   game's item-name function reads the active language through table slot 956. */
enum {
    TABLE_MANAGER = 0x35618f0,
    TABLE_COUNT = 0x2b70,
    NAME_TABLE_SLOT = 0x3bc,
    NAME_OF = 0xa34e1c,
    MAX_NAME_BYTES = 200,
};

static bool name_table_ready(const MtHost *host, uint64_t base) {
    uint64_t manager = 0;
    uint32_t count = 0;
    uint64_t table = 0;
    return host->mem_read(base + TABLE_MANAGER, &manager, sizeof manager) == MT_OK && manager &&
           host->mem_read(manager + TABLE_COUNT, &count, sizeof count) == MT_OK &&
           count > NAME_TABLE_SLOT &&
           host->mem_read(manager + 0x10 + NAME_TABLE_SLOT * 8, &table, sizeof table) == MT_OK && table;
}

BlockItem block_item(uint16_t terrain_id, uint16_t part_variation) {
    if (part_variation && part_variation < sizeof part_names / sizeof part_names[0])
        return part_names[part_variation];
    if (terrain_id < sizeof terrain_names / sizeof terrain_names[0])
        return terrain_names[terrain_id];
    return (BlockItem){0};
}

bool block_name(const MtHost *host, uint64_t base, uint16_t name_id, char *out, size_t capacity) {
    if (!capacity) return false;
    out[0] = '\0';

    if (!name_id || !base || !name_table_ready(host, base)) return false;

    MtCall args = {0};
    args.x[0] = name_id;
    if (host->call_guest(base + NAME_OF, &args) != MT_OK || !args.x[0]) return false;

    /* The name bank includes game markup, such as capitalization and ruby
       commands. Keep the visible UTF-8 text for the HUD's plain-text label. */
    bool in_tag = false;
    size_t length = 0;
    for (size_t i = 0; i < MAX_NAME_BYTES; ++i) {
        char ch;
        if (host->mem_read(args.x[0] + i, &ch, sizeof ch) != MT_OK) break;
        if (!ch) {
            if (length && !in_tag) {
                out[length] = '\0';
                return true;
            }
            break;
        }
        if (ch == '<') in_tag = true;
        if (in_tag) {
            if (ch == '>') in_tag = false;
            continue;
        }
        if (length + 1 >= capacity) break;
        out[length++] = ch;
    }
    out[0] = '\0';
    return false;
}
