#pragma once

#include "custom_record.h"

typedef enum {
    CUSTOM_STORE_OK,
    CUSTOM_STORE_EMPTY,
    CUSTOM_STORE_INVALID,
    CUSTOM_STORE_BUSY,
    CUSTOM_STORE_IO_ERROR,
} custom_store_result_t;

enum {
    /* HTTP calls finish before its receive frame returns; cap both nested buffers for task-stack safety. */
    CUSTOM_STREAM_CHUNK_BYTES = 512,
};

typedef struct {
    bool occupied;
    const uint8_t *image;
    uint32_t sequence;
} custom_slot_snapshot_t;

typedef struct {
    void *context;
    const uint8_t *mapped;
    size_t size;
    custom_store_result_t (*erase)(void *context, size_t offset, size_t length);
    custom_store_result_t (*write)(void *context, size_t offset,
                                   const uint8_t *data, size_t length);
    custom_store_result_t (*read)(void *context, size_t offset,
                                  uint8_t *data, size_t length);
} custom_store_backend_t;

typedef struct {
    custom_store_backend_t backend;
    custom_slot_snapshot_t snapshots[CUSTOM_SLOT_COUNT];
    int8_t active_banks[CUSTOM_SLOT_COUNT];
    bool available;
    bool busy;
    uint8_t pending_slot;
    uint8_t pending_bank;
    custom_record_meta_t pending_meta;
    size_t pending_image_written;
    uint32_t pending_crc;
} custom_store_t;

custom_store_result_t custom_store_init(custom_store_t *store, custom_store_backend_t backend);
custom_store_result_t custom_store_init_empty(custom_store_t *store);
bool custom_store_available(const custom_store_t *store);
const custom_slot_snapshot_t *custom_store_snapshot(const custom_store_t *store,
                                                     uint8_t logical_slot);
uint8_t custom_store_occupied_mask(const custom_store_t *store);
bool custom_store_has_content(const custom_store_t *store);
bool custom_store_first_occupied(const custom_store_t *store, uint8_t *logical_slot);
bool custom_store_adjacent_occupied(const custom_store_t *store, uint8_t from,
                                    int direction, uint8_t *logical_slot);

custom_store_result_t custom_store_begin_update(custom_store_t *store, uint8_t logical_slot);
custom_store_result_t custom_store_write(custom_store_t *store,
                                         const uint8_t *data, size_t length);
custom_store_result_t custom_store_finish(custom_store_t *store);
void custom_store_abort(custom_store_t *store);
custom_store_result_t custom_store_clear(custom_store_t *store, uint8_t logical_slot);
