/* SPDX-License-Identifier: MIT */
#include "block_target.h"

#include <math.h>

/* Pokopia 2.0.1. Camera and grid layouts are documented in Recompia's
   pkp_aim.cpp and pkp_view_projection.cpp at 8e79edb2f3649348. */
enum {
    GAME_MANAGER = 0x3561710,
    WORLD = 0x34efc98,
    PLAYER_MANAGER = 0x3561928,
    CAMERA_MANAGER = 0x3561948,
    CAMERA_ACCEPTS_INPUT = 0x887f0,
    RENDER_VIEW_ROOT = 0x3561758,
    PART_POOL = 0x3561680,
    POINT_CELL_POOL = 0x3561690,
    TERRAIN_MASK = 0x7ff,
    SEAWATER_TERRAIN_ID = 389,
    LAST_TERRAIN_ID = 0x485,
    PART_CAPACITY = 0xc8000,
};

typedef struct Grid {
    uint64_t world;
    uint64_t chunks;
    uint64_t cells;
} Grid;

typedef struct RayAxis {
    int cell;
    int step;
    double next;
    double interval;
} RayAxis;

static uint64_t read_pointer(const MtHost *host, uint64_t address) {
    uint64_t value;
    return address && host->mem_read(address, &value, sizeof value) == MT_OK ? value : 0;
}

static uint64_t inspectable_player(const MtHost *host, uint64_t base) {
    const uint64_t game = read_pointer(host, base + GAME_MANAGER);
    uint32_t permissions;
    if (!game || host->mem_read(game + 0x8c, &permissions, sizeof permissions) != MT_OK || !permissions)
        return 0;

    const uint64_t event = read_pointer(host, game + 0xd0);
    uint8_t busy;
    if (event && host->mem_read(event + 0x6ea, &busy, sizeof busy) == MT_OK && busy)
        return 0;

    const uint64_t players = read_pointer(host, base + PLAYER_MANAGER);
    if (!players)
        return 0;
    return read_pointer(host, players + 0x30);
}

static uint64_t active_follow_camera(const MtHost *host, uint64_t base) {
    const uint64_t manager = read_pointer(host, base + CAMERA_MANAGER);
    int8_t index;
    if (!manager || host->mem_read(manager + 0xcc8, &index, sizeof index) != MT_OK || index < 0 || index >= 14)
        return 0;
    const uint64_t camera = manager + 0x10;
    return read_pointer(host, manager + 0xc58 + 8 * (unsigned)index) == camera ? camera : 0;
}

static bool camera_accepts_input(const MtHost *host, uint64_t base, uint64_t camera) {
    /* The follow update at main+0x87ce8 uses this query to gate aim input.
       It checks camera locks, UI focus, and the local player's control state. */
    MtCall query = {.x = {camera}};
    return !host->call_guest || host->call_guest(base + CAMERA_ACCEPTS_INPUT, &query) != MT_OK ||
           (query.x[0] & 1) != 0;
}

/* The command query at main+0x2e2460 uses a 20-bit point handle. The helper
   at main+0x1afff4 combines a separate low-word grid with the cell's high nibble. */
static uint16_t part_at(const MtHost *host, uint64_t base, const Grid *grid, uint16_t chunk,
                        unsigned local, uint8_t high_byte) {
    const uint64_t points = read_pointer(host, base + POINT_CELL_POOL);
    uint16_t low_word;
    if (!points || host->mem_read(points + 2 * ((uint64_t)chunk * 0x20000 + local),
                                 &low_word, sizeof low_word) != MT_OK)
        return 0;

    const uint32_t handle = (uint32_t)low_word | ((uint32_t)(high_byte & 15) << 16);
    if (!handle || handle >= PART_CAPACITY)
        return 0;

    const uint64_t manager = read_pointer(host, grid->world + 0x6c050b0);
    uint32_t point_count;
    uint32_t part_index;
    if (!manager || host->mem_read(manager + 0x640020, &point_count, sizeof point_count) != MT_OK ||
        handle >= point_count || host->mem_read(manager + 0x18 + 4 * handle, &part_index, sizeof part_index) != MT_OK ||
        part_index >= PART_CAPACITY)
        return 0;

    const uint64_t parts = read_pointer(host, base + PART_POOL);
    uint16_t variation;
    if (!parts || host->mem_read(parts + 16 * part_index + 4, &variation, sizeof variation) != MT_OK)
        return 0;
    return variation & 0xfff;
}

/* A placed part occupies the cell even if its runtime point cannot yet be resolved. */
static bool occupied_cell(const MtHost *host, uint64_t base, const Grid *grid,
                          int x, int y, int z, BlockTarget *target) {
    if ((unsigned)x >= 2048 || (unsigned)y >= 128 || (unsigned)z >= 2048)
        return false;

    uint16_t chunk;
    const unsigned map_index = ((unsigned)z >> 5) * 64 + ((unsigned)x >> 5);
    if (host->mem_read(grid->chunks + 2 * map_index, &chunk, sizeof chunk) != MT_OK || chunk >= 256)
        return false;

    uint8_t packed[3];
    const unsigned local = ((unsigned)y << 10) | (((unsigned)z & 31) << 5) | ((unsigned)x & 31);
    if (host->mem_read(grid->cells + (uint64_t)chunk * 0x60000 + 3 * local, packed, sizeof packed) != MT_OK)
        return false;

    const uint16_t id = (uint16_t)((packed[0] | (packed[1] << 8)) & TERRAIN_MASK);
    /* Seawater is transparent to inspection, so a camera ray can reach the seabed. */
    if (!id || id == SEAWATER_TERRAIN_ID) return false;
    target->x = (uint16_t)x;
    target->y = (uint16_t)y;
    target->z = (uint16_t)z;
    /* The command query treats 1..0x485 as terrain. Placed parts also use
       values below the common 0x7ff marker, for example 1246 and 1602. */
    if (id > LAST_TERRAIN_ID) {
        target->part_variation = part_at(host, base, grid, chunk, local, packed[2]);
        if (!target->part_variation) target->unresolved_part_marker = id;
    } else {
        target->terrain_id = id;
    }
    return true;
}

static RayAxis ray_axis(double origin, double direction) {
    const int cell = (int)floor(origin);
    if (fabs(direction) < 1e-9)
        return (RayAxis){.cell = cell, .next = INFINITY, .interval = INFINITY};

    const int step = direction > 0 ? 1 : -1;
    const double boundary = step > 0 ? cell + 1 : cell;
    return (RayAxis){.cell = cell, .step = step, .next = (boundary - origin) / direction,
                     .interval = 1.0 / fabs(direction)};
}

static bool camera_hit(const MtHost *host, uint64_t base, const Grid *grid, BlockTarget *target) {
    const uint64_t root = read_pointer(host, base + RENDER_VIEW_ROOT);
    float view[8];  /* Final eye and look-at vectors, including the shoulder offset. */
    if (!root || host->mem_read(root + 0x49c0, view, sizeof view) != MT_OK)
        return false;

    double delta[3];
    double length_squared = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (!isfinite(view[axis]) || !isfinite(view[4 + axis]) ||
            fabsf(view[axis]) > 200000.0f || fabsf(view[4 + axis]) > 200000.0f)
            return false;
        delta[axis] = ((double)view[4 + axis] - view[axis]) / 100.0;
        length_squared += delta[axis] * delta[axis];
    }
    const double length = sqrt(length_squared);
    if (length < 1e-5)
        return false;

    /* Extend eight blocks beyond the look-at point, capped from the camera eye. */
    const double reach = fmin(length + 8, 24);
    RayAxis axes[] = {
        ray_axis(((double)view[0] + 102400) / 100, delta[0] / length),
        ray_axis((double)view[1] / 100, delta[1] / length),
        ray_axis(((double)view[2] + 102400) / 100, delta[2] / length),
    };

    for (;;) {
        const double next = fmin(axes[0].next, fmin(axes[1].next, axes[2].next));
        if (next > reach)
            return false;
        /* At an edge or corner, step each crossed plane before testing the new cell. */
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (axes[axis].next <= next + 1e-9) {
                axes[axis].cell += axes[axis].step;
                axes[axis].next += axes[axis].interval;
            }
        }
        if (occupied_cell(host, base, grid, axes[0].cell, axes[1].cell, axes[2].cell, target))
            return true;
    }
}

bool block_target(const MtHost *host, uint64_t base, BlockTarget *target) {
    *target = (BlockTarget){0};
    const uint64_t player = inspectable_player(host, base);
    if (!player)
        return false;
    const uint64_t camera = active_follow_camera(host, base);
    if (!camera)
        return false;

    Grid grid = {.world = read_pointer(host, base + WORLD)};
    if (!grid.world)
        return false;
    grid.chunks = read_pointer(host, grid.world + 0x6000030);
    grid.cells = read_pointer(host, grid.world + 0x6000018);
    if (!grid.chunks || !grid.cells)
        return false;

    uint8_t mouse[2];  /* Game mouse-mode enabled and native pointer hit valid. */
    if (host->mem_read(player + 0xaa81, mouse, sizeof mouse) != MT_OK)
        return false;
    if (mouse[0]) {
        int16_t hit[3];
        if (!mouse[1] || host->mem_read(player + 0xaa30, hit, sizeof hit) != MT_OK)
            return false;
        return occupied_cell(host, base, &grid, hit[0], hit[1], hit[2], target);
    }

    return camera_accepts_input(host, base, camera) && camera_hit(host, base, &grid, target);
}
