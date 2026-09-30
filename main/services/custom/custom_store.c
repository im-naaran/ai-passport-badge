#include "custom_store.h"

#include <string.h>

enum {
    PAYLOAD_CRC_OFFSET = 28,
    COMMIT_OFFSET = 36,
};

static custom_store_result_t refresh_slot(custom_store_t *store, uint8_t logical_slot) {
    custom_record_view_t views[CUSTOM_BANK_COUNT];
    bool valid[CUSTOM_BANK_COUNT] = {false, false};
    for (uint8_t bank = 0; bank < CUSTOM_BANK_COUNT; ++bank) {
        size_t offset;
        if (!custom_bank_offset(logical_slot, bank, &offset)) return CUSTOM_STORE_INVALID;
        valid[bank] = custom_record_validate(store->backend.mapped + offset,
                                             CUSTOM_BANK_SIZE, logical_slot, &views[bank]);
    }
    int selected = !valid[0] ? (valid[1] ? 1 : -1) :
        (!valid[1] || !custom_sequence_newer(views[1].meta.sequence,
                                             views[0].meta.sequence) ? 0 : 1);
    if (selected < 0) {
        store->active_banks[logical_slot] = -1;
        store->snapshots[logical_slot] = (custom_slot_snapshot_t){0};
        return CUSTOM_STORE_EMPTY;
    }
    const custom_record_view_t *record = &views[selected];
    store->active_banks[logical_slot] = (int8_t)selected;
    store->snapshots[logical_slot] = (custom_slot_snapshot_t){
        .occupied = record->meta.occupied,
        .image = record->image,
        .sequence = record->meta.sequence,
    };
    return record->meta.occupied ? CUSTOM_STORE_OK : CUSTOM_STORE_EMPTY;
}

custom_store_result_t custom_store_init(custom_store_t *store, custom_store_backend_t backend) {
    if (!store || !backend.mapped || backend.size != CUSTOM_PARTITION_SIZE ||
        !backend.erase || !backend.write || !backend.read)
        return CUSTOM_STORE_INVALID;
    *store = (custom_store_t){.backend = backend, .available = true};
    bool has_content = false;
    for (uint8_t slot = 0; slot < CUSTOM_SLOT_COUNT; ++slot) {
        store->active_banks[slot] = -1;
        custom_store_result_t result = refresh_slot(store, slot);
        if (result == CUSTOM_STORE_INVALID) return result;
        has_content |= result == CUSTOM_STORE_OK;
    }
    return has_content ? CUSTOM_STORE_OK : CUSTOM_STORE_EMPTY;
}

custom_store_result_t custom_store_init_empty(custom_store_t *store) {
    if (!store) return CUSTOM_STORE_INVALID;
    *store = (custom_store_t){0};
    for (uint8_t slot = 0; slot < CUSTOM_SLOT_COUNT; ++slot)
        store->active_banks[slot] = -1;
    return CUSTOM_STORE_EMPTY;
}

bool custom_store_available(const custom_store_t *store) {
    return store && store->available;
}

const custom_slot_snapshot_t *custom_store_snapshot(const custom_store_t *store,
                                                     uint8_t logical_slot) {
    return store && logical_slot < CUSTOM_SLOT_COUNT ? &store->snapshots[logical_slot] : NULL;
}

uint8_t custom_store_occupied_mask(const custom_store_t *store) {
    uint8_t mask = 0;
    if (!store) return mask;
    for (uint8_t slot = 0; slot < CUSTOM_SLOT_COUNT; ++slot)
        if (store->snapshots[slot].occupied) mask |= (uint8_t)(1u << slot);
    return mask;
}

bool custom_store_has_content(const custom_store_t *store) {
    return custom_store_occupied_mask(store) != 0;
}

bool custom_store_first_occupied(const custom_store_t *store, uint8_t *logical_slot) {
    if (!logical_slot) return false;
    uint8_t mask = custom_store_occupied_mask(store);
    for (uint8_t slot = 0; slot < CUSTOM_SLOT_COUNT; ++slot) {
        if (mask & (uint8_t)(1u << slot)) {
            *logical_slot = slot;
            return true;
        }
    }
    return false;
}

bool custom_store_adjacent_occupied(const custom_store_t *store, uint8_t from,
                                    int direction, uint8_t *logical_slot) {
    if (!logical_slot || from >= CUSTOM_SLOT_COUNT || (direction != -1 && direction != 1))
        return false;
    uint8_t mask = custom_store_occupied_mask(store);
    for (uint8_t step = 1; step < CUSTOM_SLOT_COUNT; ++step) {
        int candidate = (int)from + direction * step;
        while (candidate < 0) candidate += CUSTOM_SLOT_COUNT;
        candidate %= CUSTOM_SLOT_COUNT;
        if (mask & (uint8_t)(1u << candidate)) {
            *logical_slot = (uint8_t)candidate;
            return true;
        }
    }
    return false;
}

static custom_store_result_t begin_record(custom_store_t *store, uint8_t logical_slot,
                                          bool occupied) {
    if (!store || logical_slot >= CUSTOM_SLOT_COUNT) return CUSTOM_STORE_INVALID;
    if (store->busy) return CUSTOM_STORE_BUSY;
    if (!store->available) return CUSTOM_STORE_IO_ERROR;
    uint8_t bank = store->active_banks[logical_slot] == 0 ? 1 : 0;
    size_t offset;
    if (!custom_bank_offset(logical_slot, bank, &offset)) return CUSTOM_STORE_INVALID;
    custom_record_meta_t meta = {
        .sequence = store->active_banks[logical_slot] < 0 ? 1 :
            store->snapshots[logical_slot].sequence + 1,
        .logical_slot = logical_slot,
        .occupied = occupied,
        .image_format = occupied ? CUSTOM_IMAGE_FORMAT_RGB565_LE : CUSTOM_IMAGE_FORMAT_NONE,
        .width = occupied ? CUSTOM_IMAGE_WIDTH : 0,
        .height = occupied ? CUSTOM_IMAGE_HEIGHT : 0,
        .stride = occupied ? CUSTOM_IMAGE_STRIDE : 0,
        .image_length = occupied ? CUSTOM_IMAGE_BYTES : 0,
        .schema = CUSTOM_RECORD_SCHEMA_1,
    };
    custom_store_result_t result = store->backend.erase(store->backend.context,
                                                         offset, CUSTOM_BANK_SIZE);
    if (result != CUSTOM_STORE_OK) return result;
    uint8_t header[CUSTOM_RECORD_HEADER_SIZE];
    custom_record_prepare(header, &meta);
    result = store->backend.write(store->backend.context, offset, header, sizeof(header));
    if (result != CUSTOM_STORE_OK) return result;
    store->pending_slot = logical_slot;
    store->pending_bank = bank;
    store->pending_meta = meta;
    store->pending_image_written = 0;
    store->pending_crc = custom_crc32_start();
    store->busy = true;
    return CUSTOM_STORE_OK;
}

custom_store_result_t custom_store_begin_update(custom_store_t *store, uint8_t logical_slot) {
    return begin_record(store, logical_slot, true);
}

custom_store_result_t custom_store_write(custom_store_t *store,
                                         const uint8_t *data, size_t length) {
    if (!store || !store->busy || (!data && length)) return CUSTOM_STORE_INVALID;
    if (length > store->pending_meta.image_length - store->pending_image_written)
        return CUSTOM_STORE_INVALID;
    size_t bank_offset;
    if (!custom_bank_offset(store->pending_slot, store->pending_bank, &bank_offset))
        return CUSTOM_STORE_INVALID;
    size_t offset = bank_offset + CUSTOM_RECORD_HEADER_SIZE + store->pending_image_written;
    custom_store_result_t result = length ?
        store->backend.write(store->backend.context, offset, data, length) : CUSTOM_STORE_OK;
    if (result != CUSTOM_STORE_OK) return result;
    store->pending_crc = custom_crc32_extend(store->pending_crc, data, length);
    store->pending_image_written += length;
    return CUSTOM_STORE_OK;
}

custom_store_result_t custom_store_finish(custom_store_t *store) {
    if (!store || !store->busy ||
        store->pending_image_written != store->pending_meta.image_length)
        return CUSTOM_STORE_INVALID;
    size_t bank_offset;
    if (!custom_bank_offset(store->pending_slot, store->pending_bank, &bank_offset))
        return CUSTOM_STORE_INVALID;

    uint8_t expected_header[CUSTOM_RECORD_HEADER_SIZE];
    custom_record_prepare(expected_header, &store->pending_meta);
    custom_record_finalize(expected_header, custom_crc32_finish(store->pending_crc));
    custom_store_result_t result = store->backend.write(
        store->backend.context, bank_offset + PAYLOAD_CRC_OFFSET,
        expected_header + PAYLOAD_CRC_OFFSET, 8);

    uint8_t read_header[COMMIT_OFFSET];
    if (result == CUSTOM_STORE_OK)
        result = store->backend.read(store->backend.context, bank_offset,
                                     read_header, sizeof(read_header));
    if (result == CUSTOM_STORE_OK &&
        memcmp(read_header, expected_header, sizeof(read_header)) != 0)
        result = CUSTOM_STORE_IO_ERROR;

    uint8_t verify[CUSTOM_STREAM_CHUNK_BYTES];
    uint32_t crc_state = custom_crc32_start();
    for (size_t read = 0; result == CUSTOM_STORE_OK &&
         read < store->pending_meta.image_length;) {
        size_t remaining = store->pending_meta.image_length - read;
        size_t chunk = remaining < sizeof(verify) ? remaining : sizeof(verify);
        result = store->backend.read(store->backend.context,
            bank_offset + CUSTOM_RECORD_HEADER_SIZE + read, verify, chunk);
        if (result == CUSTOM_STORE_OK) crc_state = custom_crc32_extend(crc_state, verify, chunk);
        read += chunk;
    }
    if (result == CUSTOM_STORE_OK &&
        custom_crc32_finish(crc_state) != custom_crc32_finish(store->pending_crc))
        result = CUSTOM_STORE_IO_ERROR;

    uint8_t committed[4];
    custom_record_mark_committed(committed);
    if (result == CUSTOM_STORE_OK)
        result = store->backend.write(store->backend.context,
            bank_offset + COMMIT_OFFSET, committed, sizeof(committed));

    uint8_t completed_slot = store->pending_slot;
    bool expected_occupied = store->pending_meta.occupied;
    uint32_t expected_sequence = store->pending_meta.sequence;
    store->busy = false;
    store->pending_image_written = 0;
    if (result != CUSTOM_STORE_OK) return result;
    result = refresh_slot(store, completed_slot);
    const custom_slot_snapshot_t *snapshot = &store->snapshots[completed_slot];
    if ((result != CUSTOM_STORE_OK && expected_occupied) ||
        (result != CUSTOM_STORE_EMPTY && !expected_occupied) ||
        snapshot->occupied != expected_occupied || snapshot->sequence != expected_sequence)
        return CUSTOM_STORE_IO_ERROR;
    return CUSTOM_STORE_OK;
}

void custom_store_abort(custom_store_t *store) {
    if (!store) return;
    // The incomplete bank stays uncommitted; a later transaction erases it before reuse.
    store->busy = false;
    store->pending_image_written = 0;
}

custom_store_result_t custom_store_clear(custom_store_t *store, uint8_t logical_slot) {
    if (!store || logical_slot >= CUSTOM_SLOT_COUNT) return CUSTOM_STORE_INVALID;
    if (store->busy) return CUSTOM_STORE_BUSY;
    if (!store->available) return CUSTOM_STORE_IO_ERROR;
    // DELETE is idempotent: an already empty slot does not consume another Flash erase cycle.
    if (!store->snapshots[logical_slot].occupied) return CUSTOM_STORE_OK;
    custom_store_result_t result = begin_record(store, logical_slot, false);
    return result == CUSTOM_STORE_OK ? custom_store_finish(store) : result;
}
