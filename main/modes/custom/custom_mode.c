#include "custom_mode.h"

static bool is_available(void *context) {
    custom_mode_t *custom = context;
    return custom && custom_store_has_content(custom->store);
}

static void enter(void *context) {
    custom_mode_t *custom = context;
    if (!custom || !custom->store) return;
    const custom_slot_snapshot_t *current = custom->current_slot_known ?
        custom_store_snapshot(custom->store, custom->current_slot) : NULL;
    if (current && current->occupied) return;
    custom->current_slot_known = custom_store_first_occupied(custom->store,
                                                              &custom->current_slot);
}

static mode_action_t handle_key(void *context, mode_key_t key, int64_t now_us) {
    (void)now_us;
    custom_mode_t *custom = context;
    if (!custom || !custom->store || key == MODE_KEY_OK) return MODE_STAY;
    if (!custom->current_slot_known) enter(custom);
    if (!custom->current_slot_known) return MODE_STAY;
    uint8_t candidate;
    int direction = key == MODE_KEY_UP ? -1 : 1;
    if (custom_store_adjacent_occupied(custom->store, custom->current_slot,
                                       direction, &candidate))
        custom->current_slot = candidate;
    return MODE_STAY;
}

mode_t custom_mode_descriptor(custom_mode_t *custom, custom_store_t *store) {
    if (!custom) return (mode_t){0};
    *custom = (custom_mode_t){.store = store};
    return (mode_t){.id = 2, .name = "个性化", .context = custom,
                    .enter = enter, .handle_key = handle_key,
                    .is_available = is_available};
}

const custom_slot_snapshot_t *custom_mode_snapshot(const custom_mode_t *custom) {
    return custom && custom->store && custom->current_slot_known ?
        custom_store_snapshot(custom->store, custom->current_slot) : NULL;
}
