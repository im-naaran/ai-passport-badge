#include <string.h>
#include <unity.h>

#include "services/custom/custom_record.h"
#include "services/custom/custom_store.h"

static uint8_t flash_data[CUSTOM_PARTITION_SIZE];
static uint8_t image_data[CUSTOM_IMAGE_BYTES];
static custom_store_t store;
static unsigned erase_calls;
static unsigned write_calls;
static unsigned read_calls;
static unsigned fail_erase_call;
static unsigned fail_write_call;
static unsigned fail_read_call;

static custom_store_result_t erase_flash(void *context, size_t offset, size_t length) {
    (void)context;
    ++erase_calls;
    if (fail_erase_call && erase_calls == fail_erase_call) return CUSTOM_STORE_IO_ERROR;
    if (offset > sizeof(flash_data) || length > sizeof(flash_data) - offset)
        return CUSTOM_STORE_INVALID;
    memset(flash_data + offset, 0xFF, length);
    return CUSTOM_STORE_OK;
}

static custom_store_result_t write_flash(void *context, size_t offset,
                                         const uint8_t *data, size_t length) {
    (void)context;
    ++write_calls;
    if (fail_write_call && write_calls == fail_write_call) return CUSTOM_STORE_IO_ERROR;
    if (offset > sizeof(flash_data) || length > sizeof(flash_data) - offset)
        return CUSTOM_STORE_INVALID;
    for (size_t i = 0; i < length; ++i) {
        if ((flash_data[offset + i] & data[i]) != data[i]) return CUSTOM_STORE_IO_ERROR;
        flash_data[offset + i] &= data[i];
    }
    return CUSTOM_STORE_OK;
}

static custom_store_result_t read_flash(void *context, size_t offset,
                                        uint8_t *data, size_t length) {
    (void)context;
    ++read_calls;
    if (fail_read_call && read_calls == fail_read_call) return CUSTOM_STORE_IO_ERROR;
    if (offset > sizeof(flash_data) || length > sizeof(flash_data) - offset)
        return CUSTOM_STORE_INVALID;
    memcpy(data, flash_data + offset, length);
    return CUSTOM_STORE_OK;
}

static custom_store_backend_t backend(void) {
    return (custom_store_backend_t){
        .mapped = flash_data,
        .size = sizeof(flash_data),
        .erase = erase_flash,
        .write = write_flash,
        .read = read_flash,
    };
}

static void save_image(uint8_t slot, uint8_t value) {
    memset(image_data, value, sizeof(image_data));
    TEST_ASSERT_EQUAL(CUSTOM_STORE_OK, custom_store_begin_update(&store, slot));
    size_t written = 0;
    while (written < sizeof(image_data)) {
        size_t chunk = sizeof(image_data) - written;
        if (chunk > 4093) chunk = 4093;
        TEST_ASSERT_EQUAL(CUSTOM_STORE_OK,
            custom_store_write(&store, image_data + written, chunk));
        written += chunk;
    }
    TEST_ASSERT_EQUAL(CUSTOM_STORE_OK, custom_store_finish(&store));
}

void setUp(void) {
    memset(flash_data, 0xFF, sizeof(flash_data));
    memset(image_data, 0, sizeof(image_data));
    erase_calls = write_calls = read_calls = 0;
    fail_erase_call = fail_write_call = fail_read_call = 0;
    TEST_ASSERT_EQUAL(CUSTOM_STORE_EMPTY, custom_store_init(&store, backend()));
}

void tearDown(void) {}

static custom_record_meta_t image_meta(uint8_t slot, uint32_t sequence) {
    return (custom_record_meta_t){
        .sequence = sequence,
        .logical_slot = slot,
        .occupied = true,
        .image_format = CUSTOM_IMAGE_FORMAT_RGB565_LE,
        .width = CUSTOM_IMAGE_WIDTH,
        .height = CUSTOM_IMAGE_HEIGHT,
        .stride = CUSTOM_IMAGE_STRIDE,
        .image_length = CUSTOM_IMAGE_BYTES,
        .schema = CUSTOM_RECORD_SCHEMA_1,
    };
}

static void commit_record(uint8_t *bank, custom_record_meta_t meta, uint8_t fill) {
    memset(bank, 0xFF, CUSTOM_BANK_SIZE);
    custom_record_prepare(bank, &meta);
    if (meta.image_length) memset(bank + CUSTOM_RECORD_HEADER_SIZE, fill, meta.image_length);
    custom_record_finalize(bank,
        custom_crc32(bank + CUSTOM_RECORD_HEADER_SIZE, meta.image_length));
    custom_record_mark_committed(bank + 36);
}

static void test_crc_header_vectors_and_bank_boundaries(void) {
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, custom_crc32((const uint8_t *)"123456789", 9));
    TEST_ASSERT_TRUE(custom_sequence_newer(1, UINT32_MAX));
    TEST_ASSERT_FALSE(custom_sequence_newer(UINT32_MAX, 1));
    TEST_ASSERT_FALSE(custom_sequence_newer(7, 7));
    size_t offset = 0;
    TEST_ASSERT_TRUE(custom_bank_offset(0, 0, &offset));
    TEST_ASSERT_EQUAL(0, offset);
    TEST_ASSERT_TRUE(custom_bank_offset(2, 1, &offset));
    TEST_ASSERT_EQUAL_HEX32(5u * CUSTOM_BANK_SIZE, offset);
    TEST_ASSERT_FALSE(custom_bank_offset(3, 0, &offset));
    TEST_ASSERT_FALSE(custom_bank_offset(0, 2, &offset));

    uint8_t bank[CUSTOM_BANK_SIZE];
    custom_record_meta_t meta = image_meta(1, 0x78563412);
    commit_record(bank, meta, 0x5A);
    TEST_ASSERT_EQUAL_HEX8('C', bank[0]);
    TEST_ASSERT_EQUAL_HEX8('S', bank[1]);
    TEST_ASSERT_EQUAL_HEX8('T', bank[2]);
    TEST_ASSERT_EQUAL_HEX8('M', bank[3]);
    TEST_ASSERT_EQUAL_HEX8(0x12, bank[8]);
    custom_record_view_t view;
    TEST_ASSERT_TRUE(custom_record_validate(bank, sizeof(bank), 1, &view));
    TEST_ASSERT_TRUE(view.meta.occupied);
    TEST_ASSERT_EQUAL_HEX8(0x5A, view.image[0]);
    TEST_ASSERT_FALSE(custom_record_validate(bank, sizeof(bank), 0, NULL));
    TEST_ASSERT_FALSE(custom_record_validate(bank, CUSTOM_BANK_SIZE - 1, 1, NULL));
    for (size_t byte = 0; byte < CUSTOM_RECORD_HEADER_SIZE; ++byte) {
        bank[byte] ^= 1;
        TEST_ASSERT_FALSE(custom_record_validate(bank, sizeof(bank), 1, NULL));
        bank[byte] ^= 1;
    }
    bank[CUSTOM_RECORD_HEADER_SIZE + 100] ^= 1;
    TEST_ASSERT_FALSE(custom_record_validate(bank, sizeof(bank), 1, NULL));
}

static void test_tombstone_and_invalid_field_combinations(void) {
    uint8_t bank[CUSTOM_BANK_SIZE];
    custom_record_meta_t tombstone = {
        .sequence = 9,
        .logical_slot = 2,
        .schema = CUSTOM_RECORD_SCHEMA_1,
    };
    commit_record(bank, tombstone, 0);
    custom_record_view_t view;
    TEST_ASSERT_TRUE(custom_record_validate(bank, sizeof(bank), 2, &view));
    TEST_ASSERT_FALSE(view.meta.occupied);
    TEST_ASSERT_NULL(view.image);

    custom_record_meta_t invalid = tombstone;
    invalid.width = CUSTOM_IMAGE_WIDTH;
    commit_record(bank, invalid, 0);
    TEST_ASSERT_FALSE(custom_record_validate(bank, sizeof(bank), 2, NULL));
    invalid = image_meta(2, 10);
    invalid.image_length--;
    commit_record(bank, invalid, 0x11);
    TEST_ASSERT_FALSE(custom_record_validate(bank, sizeof(bank), 2, NULL));
    invalid = image_meta(2, 11);
    commit_record(bank, invalid, 0x22);
    bank[14] |= 0x02;
    /* Re-finalize so reserved flags, rather than the CRC mismatch, reject the record. */
    custom_record_finalize(bank,
        custom_crc32(bank + CUSTOM_RECORD_HEADER_SIZE, CUSTOM_IMAGE_BYTES));
    custom_record_mark_committed(bank + 36);
    TEST_ASSERT_FALSE(custom_record_validate(bank, sizeof(bank), 2, NULL));
}

static void test_three_slots_save_independently_and_reload(void) {
    save_image(0, 0x11);
    save_image(1, 0x22);
    save_image(2, 0x33);
    TEST_ASSERT_EQUAL_HEX8(0x07, custom_store_occupied_mask(&store));
    for (uint8_t slot = 0; slot < CUSTOM_SLOT_COUNT; ++slot) {
        const custom_slot_snapshot_t *snapshot = custom_store_snapshot(&store, slot);
        TEST_ASSERT_TRUE(snapshot->occupied);
        TEST_ASSERT_EQUAL(1, snapshot->sequence);
        TEST_ASSERT_EQUAL_HEX8((slot + 1) * 0x11, snapshot->image[0]);
    }
    custom_store_t rebooted;
    TEST_ASSERT_EQUAL(CUSTOM_STORE_OK, custom_store_init(&rebooted, backend()));
    TEST_ASSERT_EQUAL_HEX8(0x07, custom_store_occupied_mask(&rebooted));
}

static void test_overwrite_rotates_bank_and_corrupt_latest_falls_back(void) {
    save_image(1, 0x41);
    save_image(1, 0x42);
    TEST_ASSERT_EQUAL(2, custom_store_snapshot(&store, 1)->sequence);
    TEST_ASSERT_EQUAL_HEX8(0x42, custom_store_snapshot(&store, 1)->image[0]);
    size_t latest;
    TEST_ASSERT_TRUE(custom_bank_offset(1, 1, &latest));
    flash_data[latest + CUSTOM_RECORD_HEADER_SIZE + 20] ^= 1;
    custom_store_t rebooted;
    TEST_ASSERT_EQUAL(CUSTOM_STORE_OK, custom_store_init(&rebooted, backend()));
    TEST_ASSERT_EQUAL(1, custom_store_snapshot(&rebooted, 1)->sequence);
    TEST_ASSERT_EQUAL_HEX8(0x41, custom_store_snapshot(&rebooted, 1)->image[0]);
}

static void test_store_selects_newer_bank_across_sequence_wrap(void) {
    size_t bank_a;
    size_t bank_b;
    TEST_ASSERT_TRUE(custom_bank_offset(0, 0, &bank_a));
    TEST_ASSERT_TRUE(custom_bank_offset(0, 1, &bank_b));
    commit_record(flash_data + bank_a, image_meta(0, UINT32_MAX), 0xA1);
    commit_record(flash_data + bank_b, image_meta(0, 0), 0xA2);
    custom_store_t rebooted;
    TEST_ASSERT_EQUAL(CUSTOM_STORE_OK, custom_store_init(&rebooted, backend()));
    TEST_ASSERT_EQUAL(0, custom_store_snapshot(&rebooted, 0)->sequence);
    TEST_ASSERT_EQUAL_HEX8(0xA2, custom_store_snapshot(&rebooted, 0)->image[0]);
}

static void test_busy_bounds_abort_and_io_failures_keep_snapshot(void) {
    save_image(0, 0x51);
    TEST_ASSERT_EQUAL(CUSTOM_STORE_OK, custom_store_begin_update(&store, 0));
    TEST_ASSERT_EQUAL(CUSTOM_STORE_BUSY, custom_store_begin_update(&store, 1));
    TEST_ASSERT_EQUAL(CUSTOM_STORE_BUSY, custom_store_clear(&store, 0));
    TEST_ASSERT_EQUAL(CUSTOM_STORE_INVALID,
                      custom_store_write(&store, image_data, CUSTOM_IMAGE_BYTES + 1));
    custom_store_abort(&store);
    TEST_ASSERT_EQUAL_HEX8(0x51, custom_store_snapshot(&store, 0)->image[0]);

    fail_erase_call = erase_calls + 1;
    TEST_ASSERT_EQUAL(CUSTOM_STORE_IO_ERROR, custom_store_begin_update(&store, 0));
    fail_erase_call = 0;
    fail_write_call = write_calls + 1;
    TEST_ASSERT_EQUAL(CUSTOM_STORE_IO_ERROR, custom_store_begin_update(&store, 0));
    TEST_ASSERT_EQUAL_HEX8(0x51, custom_store_snapshot(&store, 0)->image[0]);
}

static void test_readback_and_commit_failures_never_publish(void) {
    save_image(0, 0x61);
    memset(image_data, 0x62, sizeof(image_data));
    TEST_ASSERT_EQUAL(CUSTOM_STORE_OK, custom_store_begin_update(&store, 0));
    TEST_ASSERT_EQUAL(CUSTOM_STORE_OK,
                      custom_store_write(&store, image_data, sizeof(image_data)));
    fail_read_call = read_calls + 1;
    TEST_ASSERT_EQUAL(CUSTOM_STORE_IO_ERROR, custom_store_finish(&store));
    TEST_ASSERT_EQUAL_HEX8(0x61, custom_store_snapshot(&store, 0)->image[0]);

    fail_read_call = 0;
    TEST_ASSERT_EQUAL(CUSTOM_STORE_OK, custom_store_begin_update(&store, 0));
    TEST_ASSERT_EQUAL(CUSTOM_STORE_OK,
                      custom_store_write(&store, image_data, sizeof(image_data)));
    fail_write_call = write_calls + 2; /* CRC fields succeed; commit fails. */
    TEST_ASSERT_EQUAL(CUSTOM_STORE_IO_ERROR, custom_store_finish(&store));
    TEST_ASSERT_EQUAL_HEX8(0x61, custom_store_snapshot(&store, 0)->image[0]);
}

static void test_clear_is_transactional_idempotent_and_reusable(void) {
    save_image(2, 0x71);
    unsigned erases_before = erase_calls;
    TEST_ASSERT_EQUAL(CUSTOM_STORE_OK, custom_store_clear(&store, 2));
    TEST_ASSERT_FALSE(custom_store_snapshot(&store, 2)->occupied);
    TEST_ASSERT_EQUAL(2, custom_store_snapshot(&store, 2)->sequence);
    TEST_ASSERT_EQUAL(erases_before + 1, erase_calls);
    TEST_ASSERT_EQUAL(CUSTOM_STORE_OK, custom_store_clear(&store, 2));
    TEST_ASSERT_EQUAL(erases_before + 1, erase_calls);

    custom_store_t rebooted;
    TEST_ASSERT_EQUAL(CUSTOM_STORE_EMPTY, custom_store_init(&rebooted, backend()));
    TEST_ASSERT_EQUAL_HEX8(0, custom_store_occupied_mask(&rebooted));
    store = rebooted;
    save_image(2, 0x72);
    TEST_ASSERT_EQUAL(3, custom_store_snapshot(&store, 2)->sequence);
    TEST_ASSERT_EQUAL_HEX8(0x72, custom_store_snapshot(&store, 2)->image[0]);
}

static void test_failed_clear_keeps_old_image(void) {
    save_image(1, 0x81);
    fail_write_call = write_calls + 2; /* Tombstone header succeeds; CRC fields fail. */
    TEST_ASSERT_EQUAL(CUSTOM_STORE_IO_ERROR, custom_store_clear(&store, 1));
    TEST_ASSERT_TRUE(custom_store_snapshot(&store, 1)->occupied);
    TEST_ASSERT_EQUAL_HEX8(0x81, custom_store_snapshot(&store, 1)->image[0]);
    custom_store_t rebooted;
    TEST_ASSERT_EQUAL(CUSTOM_STORE_OK, custom_store_init(&rebooted, backend()));
    TEST_ASSERT_TRUE(custom_store_snapshot(&rebooted, 1)->occupied);
}

static void test_unavailable_store_is_empty_and_read_only(void) {
    custom_store_t unavailable;
    TEST_ASSERT_EQUAL(CUSTOM_STORE_EMPTY, custom_store_init_empty(&unavailable));
    TEST_ASSERT_FALSE(custom_store_available(&unavailable));
    TEST_ASSERT_EQUAL_HEX8(0, custom_store_occupied_mask(&unavailable));
    TEST_ASSERT_EQUAL(CUSTOM_STORE_IO_ERROR,
                      custom_store_begin_update(&unavailable, 0));
    TEST_ASSERT_EQUAL(CUSTOM_STORE_IO_ERROR, custom_store_clear(&unavailable, 0));
    TEST_ASSERT_EQUAL(CUSTOM_STORE_INVALID, custom_store_init(&unavailable,
        (custom_store_backend_t){.mapped = flash_data, .size = CUSTOM_PARTITION_SIZE}));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_crc_header_vectors_and_bank_boundaries);
    RUN_TEST(test_tombstone_and_invalid_field_combinations);
    RUN_TEST(test_three_slots_save_independently_and_reload);
    RUN_TEST(test_overwrite_rotates_bank_and_corrupt_latest_falls_back);
    RUN_TEST(test_store_selects_newer_bank_across_sequence_wrap);
    RUN_TEST(test_busy_bounds_abort_and_io_failures_keep_snapshot);
    RUN_TEST(test_readback_and_commit_failures_never_publish);
    RUN_TEST(test_clear_is_transactional_idempotent_and_reusable);
    RUN_TEST(test_failed_clear_keeps_old_image);
    RUN_TEST(test_unavailable_store_is_empty_and_read_only);
    return UNITY_END();
}
