#include "custom_record.h"

#include <string.h>

enum {
    CUSTOM_MAGIC = 0x4D545343u, /* CSTM in the serialized little-endian header. */
    CUSTOM_COMMIT_MARKER = 0xC0570A11u,
    CUSTOM_FLAG_OCCUPIED = 1u,
    OFFSET_SEQUENCE = 8,
    OFFSET_LOGICAL_SLOT = 12,
    OFFSET_FLAGS = 14,
    OFFSET_IMAGE_FORMAT = 16,
    OFFSET_WIDTH = 18,
    OFFSET_HEIGHT = 20,
    OFFSET_STRIDE = 22,
    OFFSET_IMAGE_LENGTH = 24,
    OFFSET_PAYLOAD_CRC = 28,
    OFFSET_HEADER_CRC = 32,
    OFFSET_COMMIT = 36,
};

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

bool custom_sequence_newer(uint32_t candidate, uint32_t current) {
    // Signed subtraction preserves ordering when a per-slot sequence wraps UINT32_MAX.
    return candidate != current && (int32_t)(candidate - current) > 0;
}

bool custom_bank_offset(uint8_t logical_slot, uint8_t physical_bank, size_t *offset) {
    if (!offset || logical_slot >= CUSTOM_SLOT_COUNT || physical_bank >= CUSTOM_BANK_COUNT)
        return false;
    size_t index = (size_t)logical_slot * CUSTOM_BANK_COUNT + physical_bank;
    size_t result = index * CUSTOM_BANK_SIZE;
    if (result > CUSTOM_PARTITION_SIZE || CUSTOM_BANK_SIZE > CUSTOM_PARTITION_SIZE - result)
        return false;
    *offset = result;
    return true;
}

uint32_t custom_crc32_start(void) { return UINT32_MAX; }

uint32_t custom_crc32_extend(uint32_t state, const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        state ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            state = (state >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(state & 1u));
    }
    return state;
}

uint32_t custom_crc32_finish(uint32_t state) { return state ^ UINT32_MAX; }

uint32_t custom_crc32(const uint8_t *data, size_t length) {
    return custom_crc32_finish(custom_crc32_extend(custom_crc32_start(), data, length));
}

static bool meta_valid(const custom_record_meta_t *meta, uint8_t expected_slot) {
    if (!meta || meta->schema != CUSTOM_RECORD_SCHEMA_1 ||
        meta->logical_slot >= CUSTOM_SLOT_COUNT || meta->logical_slot != expected_slot)
        return false;
    if (!meta->occupied)
        return meta->image_format == CUSTOM_IMAGE_FORMAT_NONE && meta->width == 0 &&
            meta->height == 0 && meta->stride == 0 && meta->image_length == 0;
    return meta->image_format == CUSTOM_IMAGE_FORMAT_RGB565_LE &&
        meta->width == CUSTOM_IMAGE_WIDTH && meta->height == CUSTOM_IMAGE_HEIGHT &&
        meta->stride == CUSTOM_IMAGE_STRIDE && meta->image_length == CUSTOM_IMAGE_BYTES &&
        CUSTOM_RECORD_HEADER_SIZE + (size_t)meta->image_length <= CUSTOM_BANK_SIZE;
}

void custom_record_prepare(uint8_t header[CUSTOM_RECORD_HEADER_SIZE],
                           const custom_record_meta_t *meta) {
    memset(header, 0xFF, CUSTOM_RECORD_HEADER_SIZE);
    write_u32(header, CUSTOM_MAGIC);
    write_u16(header + 4, CUSTOM_RECORD_SCHEMA_1);
    write_u16(header + 6, CUSTOM_RECORD_HEADER_SIZE);
    write_u32(header + OFFSET_SEQUENCE, meta->sequence);
    write_u16(header + OFFSET_LOGICAL_SLOT, meta->logical_slot);
    write_u16(header + OFFSET_FLAGS, meta->occupied ? CUSTOM_FLAG_OCCUPIED : 0);
    write_u16(header + OFFSET_IMAGE_FORMAT, meta->image_format);
    write_u16(header + OFFSET_WIDTH, meta->width);
    write_u16(header + OFFSET_HEIGHT, meta->height);
    write_u16(header + OFFSET_STRIDE, meta->stride);
    write_u32(header + OFFSET_IMAGE_LENGTH, meta->image_length);
}

void custom_record_finalize(uint8_t header[CUSTOM_RECORD_HEADER_SIZE], uint32_t payload_crc) {
    write_u32(header + OFFSET_PAYLOAD_CRC, payload_crc);
    // The header CRC includes stable metadata and payload CRC, excluding itself and commit.
    write_u32(header + OFFSET_HEADER_CRC, custom_crc32(header, OFFSET_HEADER_CRC));
}

void custom_record_mark_committed(uint8_t commit_bytes[4]) {
    write_u32(commit_bytes, CUSTOM_COMMIT_MARKER);
}

bool custom_record_decode(const uint8_t header[CUSTOM_RECORD_HEADER_SIZE],
                          uint8_t expected_slot, custom_record_meta_t *meta,
                          uint32_t *payload_crc) {
    if (!header || expected_slot >= CUSTOM_SLOT_COUNT || read_u32(header) != CUSTOM_MAGIC ||
        read_u16(header + 4) != CUSTOM_RECORD_SCHEMA_1 ||
        read_u16(header + 6) != CUSTOM_RECORD_HEADER_SIZE ||
        read_u32(header + OFFSET_COMMIT) != CUSTOM_COMMIT_MARKER ||
        read_u32(header + OFFSET_HEADER_CRC) != custom_crc32(header, OFFSET_HEADER_CRC))
        return false;
    uint16_t flags = read_u16(header + OFFSET_FLAGS);
    if (flags & (uint16_t)~CUSTOM_FLAG_OCCUPIED) return false;
    custom_record_meta_t decoded = {
        .sequence = read_u32(header + OFFSET_SEQUENCE),
        .logical_slot = read_u16(header + OFFSET_LOGICAL_SLOT),
        .occupied = (flags & CUSTOM_FLAG_OCCUPIED) != 0,
        .image_format = read_u16(header + OFFSET_IMAGE_FORMAT),
        .width = read_u16(header + OFFSET_WIDTH),
        .height = read_u16(header + OFFSET_HEIGHT),
        .stride = read_u16(header + OFFSET_STRIDE),
        .image_length = read_u32(header + OFFSET_IMAGE_LENGTH),
        .schema = CUSTOM_RECORD_SCHEMA_1,
    };
    if (!meta_valid(&decoded, expected_slot)) return false;
    if (meta) *meta = decoded;
    if (payload_crc) *payload_crc = read_u32(header + OFFSET_PAYLOAD_CRC);
    return true;
}

bool custom_record_validate(const uint8_t *bank, size_t bank_size, uint8_t expected_slot,
                            custom_record_view_t *view) {
    custom_record_meta_t meta;
    uint32_t expected_crc;
    if (!bank || bank_size < CUSTOM_BANK_SIZE ||
        !custom_record_decode(bank, expected_slot, &meta, &expected_crc))
        return false;
    const uint8_t *image = meta.occupied ? bank + CUSTOM_RECORD_HEADER_SIZE : NULL;
    if (custom_crc32(image, meta.image_length) != expected_crc) return false;
    if (view) *view = (custom_record_view_t){.meta = meta, .image = image};
    return true;
}
