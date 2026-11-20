#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include <modtopia/modtopia.h>

#include "block_names.h"
#include "block_status.h"
#include "block_target.h"
#include "overlay.h"

MT_DECLARE_MOD()

static const char supported_build[] = "76a489f3bd3642fa5e18bd83d32c98ba";

static const MtHost *host;
static uint64_t main_base;
static BlockTarget last_target;
static uint64_t next_name_refresh;
static char name[256];
static bool has_name;
static BlockItem item;
static char toggle_key[64];
static char details_key[64];
static atomic_uint appearance;
static bool enabled = true;
static bool toggle_was_down;
static bool visible;
static char shown_name[256];
static char shown_details[96];
static unsigned shown_icon;
static int shown_progress;
static OverlayPosition shown_position;
static bool overlay_started;
static bool overlay_failed;

static void update_appearance(MtMod *mod, const char *key, void *user) {
    (void)key;
    (void)user;
    unsigned position = (unsigned)host->config_number(mod, "position");
    unsigned icons = host->config_bool(mod, "show_icons") ? 1 : 0;
    unsigned debug = host->config_bool(mod, "debug_mode") ? 2 : 0;
    atomic_store(&appearance, (position << 2) | debug | icons);
}

static void hide_label(void) {
    next_name_refresh = 0;
    if (visible) overlay_set(NULL, NULL, 0, OVERLAY_TOP_CENTER, -1);
    visible = false;
}

static int on_frame(MtMod *mod, const MtEvent *event, void *user) {
    (void)user;
    const bool toggled = host->key_down(mod, toggle_key) != 0;
    if (toggled && !toggle_was_down) enabled = !enabled;
    toggle_was_down = toggled;
    BlockTarget target;
    if (!enabled || overlay_failed || !block_target(host, main_base, &target)) {
        hide_label();
        return MT_EVENT_CONTINUE;
    }

    unsigned id = target.part_variation ? target.part_variation : target.terrain_id;
    if (target.unresolved_part_marker) id = target.unresolved_part_marker;
    if (target.terrain_id != last_target.terrain_id ||
        target.part_variation != last_target.part_variation ||
        target.unresolved_part_marker != last_target.unresolved_part_marker ||
        event->frame >= next_name_refresh) {
        last_target = target;
        next_name_refresh = event->frame + 30;
        item = block_item(target.terrain_id, target.part_variation);
        has_name = block_name(host, main_base, item.name_id, name, sizeof name);
        if (!has_name) {
            const char *kind = target.part_variation ? "Object" : "Block";
            if (target.unresolved_part_marker) kind = "Object marker";
            snprintf(name, sizeof name, "%s #%u", kind, id);
        }
    }

    const unsigned settings = atomic_load(&appearance);
    if (!has_name && !(settings & 2)) {
        if (visible) overlay_set(NULL, NULL, 0, OVERLAY_TOP_CENTER, -1);
        visible = false;
        return MT_EVENT_CONTINUE;
    }

    if (!overlay_started) {
        overlay_started = overlay_start() != 0;
        if (!overlay_started) {
            overlay_failed = true;
            host->log(mod, MT_LOG_WARN, "Could not start the Block Lens overlay");
            return MT_EVENT_CONTINUE;
        }
    }
    const unsigned icon = has_name && (settings & 1) ? item.item_id : 0;
    const OverlayPosition position = (OverlayPosition)(settings >> 2);
    char details[sizeof shown_details] = {0};
    const int progress = has_name ?
        block_status(host, main_base, &target, item.item_id, details, sizeof details) : -1;
    if (host->key_down(mod, details_key)) {
        const size_t n = strlen(details);
        snprintf(details + n, sizeof details - n, "%s#%u  |  X %u  Y %u  Z %u",
                 n ? "  |  " : "", id,
                 (unsigned)target.x, (unsigned)target.y, (unsigned)target.z);
    }
    if (!visible || icon != shown_icon || position != shown_position ||
        progress != shown_progress ||
        strcmp(name, shown_name) != 0 || strcmp(details, shown_details) != 0) {
        visible = true;
        shown_icon = icon;
        shown_position = position;
        shown_progress = progress;
        memcpy(shown_name, name, strlen(name) + 1);
        memcpy(shown_details, details, strlen(details) + 1);
        overlay_set(shown_name, shown_details, icon, position, progress);
    }
    return MT_EVENT_CONTINUE;
}

MT_EXPORT int mod_init(const MtHost *mod_host, MtMod *mod) {
    if (strcmp(mod_host->game_version, "2.0.1") != 0 ||
        strncmp(mod_host->game_build_id, supported_build,
                sizeof supported_build - 1) != 0) {
        mod_host->log(mod, MT_LOG_WARN, "Block Lens requires Pokopia 2.0.1a (76a489f3)");
        return MT_ERR_VERSION;
    }

    host = mod_host;
    main_base = host->module_base("main");
    if (!main_base) return MT_ERR_STATE;
    snprintf(toggle_key, sizeof toggle_key, "%s", host->config_string(mod, "toggle_key"));
    snprintf(details_key, sizeof details_key, "%s", host->config_string(mod, "details_key"));
    update_appearance(mod, NULL, NULL);
    const int rc = host->on_config_changed(mod, update_appearance, NULL);
    if (rc != MT_OK) return rc;
    return host->subscribe(mod, "FrameStart", MT_PRIORITY_NORMAL, on_frame, NULL);
}
