#include "block_status.h"

#include <stdio.h>

enum {
    LANGUAGE = 0x36ca680,
    NONE = -1,
};

enum CropStage { NOT_A_CROP, DRY, SPROUT, GROWING, RIPE };

/* Item IDs shared by the crop variants in the installed 2.0.1a item table. */
static enum CropStage crop_stage(unsigned item) {
    switch (item) {
    case 402: case 403: case 405: case 797: case 798: case 799: case 800:
        return DRY;
    case 813: case 814: case 815: case 816:
        return SPROUT;
    case 807: case 809: case 811: case 817:
        return GROWING;
    case 808: case 810: case 812: case 818: case 2035:
        return RIPE;
    default:
        return NOT_A_CROP;
    }
}

/* These terrain families have ordinary Rock Smash interactions. Harder
   substrates and unclassified objects get no move recommendation. */
static bool ordinary_smash_target(unsigned terrain) {
    switch (terrain) {
    case 2: case 3: case 5: case 8: case 11: case 21: case 22: case 24:
    case 43: case 44: case 79: case 81: case 86: case 94: case 96: case 97:
    case 146: case 158: case 160: case 161: case 172: case 369: case 401:
        return true;
    default:
        return false;
    }
}

static bool tall_grass_target(unsigned variation) {
    return variation == 1 || variation == 681 || variation == 682;
}

typedef struct StatusText {
    const char *smash;
    const char *cut;
    const char *water;
    const char *sprout;
    const char *growing;
    const char *ripe;
} StatusText;

static const StatusText languages[] = {
    {"いわくだき", "いあいぎり", "乾燥 | みずでっぽう", "芽", "成長中", "収穫可能"},
    {"Rock Smash", "Cut", "Dry | Water Gun", "Sprout", "Growing", "Ready to harvest"},
    {"Éclate-Roc", "Coupe", "Sec | Pistolet à O", "Pousse", "En croissance", "Prêt à récolter"},
    {"Spaccaroccia", "Taglio", "Secco | Pistolacqua", "Germoglio", "In crescita", "Da raccogliere"},
    {"Zertrümmerer", "Zerschneider", "Trocken | Aquaknarre", "Spross", "Wächst", "Erntereif"},
    {"Golpe Roca", "Corte", "Seco | Pistola Agua", "Brote", "En crecimiento", "Listo para cosechar"},
    {"碎岩", "居合斬", "乾枯 | 水槍", "發芽", "成長中", "可收穫"},
    {"바위깨기", "풀베기", "마름 | 물대포", "싹", "성장 중", "수확 가능"},
    {"碎岩", "居合劈", "干枯 | 水枪", "发芽", "生长中", "可收获"},
};

int block_status(const MtHost *host, uint64_t base, const BlockTarget *target,
                 unsigned item_id, char *out, size_t capacity) {
    uint64_t language_address;
    uint32_t language;
    if (host->mem_read(base + LANGUAGE, &language_address, sizeof language_address) != MT_OK ||
        !language_address ||
        host->mem_read(language_address, &language, sizeof language) != MT_OK ||
        language >= sizeof languages / sizeof languages[0])
        language = 1;

    const StatusText *text = &languages[language];
    out[0] = '\0';
    switch (target->part_variation ? crop_stage(item_id) : NOT_A_CROP) {
    case DRY:
        snprintf(out, capacity, "%s", text->water);
        return NONE;
    case SPROUT:
        snprintf(out, capacity, "%s  1/3", text->sprout);
        return 33;
    case GROWING:
        snprintf(out, capacity, "%s  2/3", text->growing);
        return 67;
    case RIPE:
        snprintf(out, capacity, "%s  |  %s", text->ripe, text->cut);
        return 100;
    default:
        break;
    }

    if (ordinary_smash_target(target->terrain_id))
        snprintf(out, capacity, "%s", text->smash);
    else if (tall_grass_target(target->part_variation))
        snprintf(out, capacity, "%s", text->cut);
    return NONE;
}
