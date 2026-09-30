#include "custom_view.h"
#include "custom_mode.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <string.h>

typedef struct {
    lv_obj_t *root;
    lv_obj_t *image;
    lv_image_dsc_t image_dsc;
    const uint8_t *rendered_image;
    uint32_t rendered_sequence;
    bool has_rendered;
} custom_view_t;

static void render(void *context) {
    custom_mode_t *custom = context;
    custom_view_t *view = custom ? custom->view : NULL;
    if (!view) return;
    ui_page_show(view->root);
    const custom_slot_snapshot_t *snapshot = custom_mode_snapshot(custom);
    if (!snapshot || !snapshot->occupied || !snapshot->image) return;
    if (view->has_rendered && view->rendered_image == snapshot->image &&
        view->rendered_sequence == snapshot->sequence) return;
    if (view->has_rendered) lv_image_cache_drop(&view->image_dsc);
    // The mapped Flash pixels outlive LVGL, so switching slots needs no full image copy.
    view->image_dsc = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
                   .w = CUSTOM_IMAGE_WIDTH, .h = CUSTOM_IMAGE_HEIGHT,
                   .stride = CUSTOM_IMAGE_STRIDE},
        .data_size = CUSTOM_IMAGE_BYTES,
        .data = snapshot->image,
    };
    lv_image_set_src(view->image, &view->image_dsc);
    view->rendered_image = snapshot->image;
    view->rendered_sequence = snapshot->sequence;
    view->has_rendered = true;
}

void custom_view_init(mode_t *mode, lv_obj_t *parent) {
    custom_mode_t *custom = mode ? mode->context : NULL;
    if (!custom) return;
    custom_view_t *view = lv_malloc_zeroed(sizeof(*view));
    if (!view) return;
    view->root = ui_page_create(parent);
    view->image = lv_image_create(view->root);
    lv_obj_set_pos(view->image, 0, 0);
    lv_obj_set_size(view->image, CUSTOM_IMAGE_WIDTH, CUSTOM_IMAGE_HEIGHT);
    lv_obj_clear_flag(view->image, LV_OBJ_FLAG_CLICKABLE);
    custom->view = view;
    mode->render = render;
}
