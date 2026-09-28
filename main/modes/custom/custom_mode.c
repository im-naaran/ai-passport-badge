#include "custom_mode.h"

static bool is_available(void *context) {
    return context && ((custom_mode_t *)context)->has_content;
}

mode_t custom_mode_descriptor(custom_mode_t *custom) {
    if (!custom) return (mode_t){0};
    *custom = (custom_mode_t){0};
    return (mode_t){.id = 2, .name = "个性化", .context = custom,
                    .is_available = is_available};
}

void custom_mode_set_content_available(custom_mode_t *custom, bool available) {
    if (custom) custom->has_content = available;
}
