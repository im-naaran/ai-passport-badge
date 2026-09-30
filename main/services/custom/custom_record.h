#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    CUSTOM_SLOT_COUNT = 3,
    CUSTOM_BANK_COUNT = 2,
    CUSTOM_RECORD_HEADER_SIZE = 40,
    CUSTOM_IMAGE_WIDTH = 240,
    CUSTOM_IMAGE_HEIGHT = 320,
    CUSTOM_IMAGE_STRIDE = 480,
    CUSTOM_IMAGE_BYTES = 153600,
    CUSTOM_BANK_SIZE = 0x28000,
    CUSTOM_PARTITION_SIZE = 0x100000,
};

typedef enum {
    CUSTOM_IMAGE_FORMAT_NONE = 0,
    CUSTOM_IMAGE_FORMAT_RGB565_LE = 1,
} custom_image_format_t;

typedef enum {
    CUSTOM_RECORD_SCHEMA_1 = 1,
} custom_record_schema_t;

typedef struct {
    uint32_t sequence;
    uint16_t logical_slot;
    bool occupied;
    uint16_t image_format;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint32_t image_length;
    uint16_t schema;
} custom_record_meta_t;

typedef struct {
    custom_record_meta_t meta;
    const uint8_t *image;
} custom_record_view_t;

bool custom_sequence_newer(uint32_t candidate, uint32_t current);
bool custom_bank_offset(uint8_t logical_slot, uint8_t physical_bank, size_t *offset);

uint32_t custom_crc32_start(void);
uint32_t custom_crc32_extend(uint32_t state, const uint8_t *data, size_t length);
uint32_t custom_crc32_finish(uint32_t state);
uint32_t custom_crc32(const uint8_t *data, size_t length);

void custom_record_prepare(uint8_t header[CUSTOM_RECORD_HEADER_SIZE],
                           const custom_record_meta_t *meta);
void custom_record_finalize(uint8_t header[CUSTOM_RECORD_HEADER_SIZE], uint32_t payload_crc);
void custom_record_mark_committed(uint8_t commit_bytes[4]);
bool custom_record_decode(const uint8_t header[CUSTOM_RECORD_HEADER_SIZE],
                          uint8_t expected_slot, custom_record_meta_t *meta,
                          uint32_t *payload_crc);
bool custom_record_validate(const uint8_t *bank, size_t bank_size, uint8_t expected_slot,
                            custom_record_view_t *view);
