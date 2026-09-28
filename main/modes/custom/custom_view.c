#include "custom_view.h"
#include "custom_mode.h"

static void render(void *context) {
    custom_mode_t *custom = context;
    if (custom && custom->view) ui_page_show(custom->view);
}

void custom_view_init(mode_t *mode, lv_obj_t *parent) {
    custom_mode_t *custom = mode ? mode->context : NULL;
    if (!custom) return;
    custom->view = ui_page_create(parent);
    mode->render = render;
}
