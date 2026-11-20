#ifndef BLOCK_LENS_OVERLAY_H
#define BLOCK_LENS_OVERLAY_H

typedef enum OverlayPosition {
    OVERLAY_TOP_CENTER,
    OVERLAY_TOP_LEFT,
    OVERLAY_TOP_RIGHT,
} OverlayPosition;

int overlay_start(void);
void overlay_set(const char *label, const char *details, unsigned item_id, OverlayPosition position,
                 int progress);
void overlay_stop(void);

#endif
