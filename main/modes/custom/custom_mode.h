#pragma once
#include "navigation/mode.h"
#include "services/custom/custom_store.h"

typedef struct {
    custom_store_t *store;
    uint8_t slot;
    void *view;
} custom_mode_t;

enum {
    CUSTOM_MODE_ID_SLOT_1 = 2,
    CUSTOM_MODE_ID_SLOT_2 = 5,
    CUSTOM_MODE_ID_SLOT_3 = 6,
};

mode_t custom_mode_descriptor(custom_mode_t *custom, custom_store_t *store,
                              uint8_t slot);
const custom_slot_snapshot_t *custom_mode_snapshot(const custom_mode_t *custom);
