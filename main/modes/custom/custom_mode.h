#pragma once
#include "navigation/mode.h"
#include "services/custom/custom_store.h"

typedef struct {
    custom_store_t *store;
    uint8_t current_slot;
    bool current_slot_known;
    void *view;
} custom_mode_t;

mode_t custom_mode_descriptor(custom_mode_t *custom, custom_store_t *store);
const custom_slot_snapshot_t *custom_mode_snapshot(const custom_mode_t *custom);
