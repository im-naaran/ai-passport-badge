#include "custom_mode.h"

static bool is_available(void *context) {
    custom_mode_t *custom = context;
    const custom_slot_snapshot_t *snapshot = custom_mode_snapshot(custom);
    return snapshot && snapshot->occupied;
}

mode_t custom_mode_descriptor(custom_mode_t *custom, custom_store_t *store,
                              uint8_t slot) {
    static const uint32_t ids[CUSTOM_SLOT_COUNT] = {
        CUSTOM_MODE_ID_SLOT_1, CUSTOM_MODE_ID_SLOT_2, CUSTOM_MODE_ID_SLOT_3,
    };
    static const char *const names[CUSTOM_SLOT_COUNT] = {
        "个性化1", "个性化2", "个性化3",
    };
    if (!custom || slot >= CUSTOM_SLOT_COUNT) return (mode_t){0};
    *custom = (custom_mode_t){.store = store, .slot = slot};
    return (mode_t){.id = ids[slot], .name = names[slot], .context = custom,
                    .is_available = is_available};
}

const custom_slot_snapshot_t *custom_mode_snapshot(const custom_mode_t *custom) {
    return custom && custom->store ?
        custom_store_snapshot(custom->store, custom->slot) : NULL;
}
