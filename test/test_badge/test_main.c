#include <unity.h>
#include <string.h>
#include "services/badge/badge_record.h"
#include "services/badge/badge_store.h"

static uint8_t flash[BADGE_PARTITION_SIZE];
static uint8_t default_image[BADGE_IMAGE_BYTES];
static badge_store_t store;
static int fail_erase, fail_write_call, fail_read;
static unsigned write_calls;

static badge_store_result_t erase_flash(void *context, size_t offset, size_t length) {
    (void)context;
    if (fail_erase) return BADGE_STORE_IO_ERROR;
    memset(flash + offset, 0xFF, length);
    return BADGE_STORE_OK;
}
static badge_store_result_t write_flash(void *context, size_t offset, const uint8_t *data, size_t length) {
    (void)context;
    ++write_calls;
    if (fail_write_call && write_calls == (unsigned)fail_write_call) return BADGE_STORE_IO_ERROR;
    for (size_t i = 0; i < length; ++i) {
        if ((flash[offset + i] & data[i]) != data[i]) return BADGE_STORE_IO_ERROR;
        flash[offset + i] &= data[i];
    }
    return BADGE_STORE_OK;
}
static badge_store_result_t read_flash(void *context, size_t offset, uint8_t *data, size_t length) {
    (void)context;
    if (fail_read) return BADGE_STORE_IO_ERROR;
    memcpy(data, flash + offset, length);
    return BADGE_STORE_OK;
}
static badge_store_backend_t backend(void) {
    return (badge_store_backend_t){NULL, flash, sizeof(flash), erase_flash, write_flash, read_flash};
}
static badge_profile_snapshot_t defaults(void) {
    return (badge_profile_snapshot_t){.name = "AI Passport", .image = default_image,
        .width = BADGE_IMAGE_WIDTH, .height = BADGE_IMAGE_HEIGHT,
        .stride = BADGE_IMAGE_STRIDE, .image_format = BADGE_IMAGE_FORMAT_RGB565_LE,
        .sequence = 0, .is_default = true, .bio = "我的 AI 身份"};
}
static badge_upload_meta_t upload(const char *name, const char *bio) {
    return (badge_upload_meta_t){.name = name, .name_length = strlen(name),
        .image_format = BADGE_IMAGE_FORMAT_RGB565_LE, .width = BADGE_IMAGE_WIDTH,
        .height = BADGE_IMAGE_HEIGHT, .stride = BADGE_IMAGE_STRIDE,
        .image_length = BADGE_IMAGE_BYTES, .bio = bio, .bio_length = strlen(bio)};
}
static void save_profile(const char *name, const char *bio, uint8_t value) {
    badge_upload_meta_t meta = upload(name, bio);
    TEST_ASSERT_EQUAL(BADGE_STORE_OK, badge_store_begin_update(&store, &meta));
    uint8_t chunk[1000]; memset(chunk, value, sizeof(chunk));
    for (unsigned i = 0; i < BADGE_IMAGE_BYTES / sizeof(chunk); ++i)
        TEST_ASSERT_EQUAL(BADGE_STORE_OK, badge_store_write(&store, chunk, sizeof(chunk)));
    TEST_ASSERT_EQUAL(BADGE_STORE_OK, badge_store_finish(&store));
}

void setUp(void) {
    memset(flash, 0xFF, sizeof(flash));
    memset(default_image, 0x5A, sizeof(default_image));
    fail_erase = fail_write_call = fail_read = 0; write_calls = 0;
    TEST_ASSERT_EQUAL(BADGE_STORE_DEFAULTED, badge_store_init(&store, backend(), defaults()));
}
void tearDown(void) {}

static void test_crc_and_explicit_header_vector(void) {
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, badge_crc32((const uint8_t *)"123456789", 9));
    badge_record_meta_t meta = {.sequence = 0x78563412, .name_length = 2,
        .image_format = BADGE_IMAGE_FORMAT_RGB565_LE, .width = BADGE_IMAGE_WIDTH,
        .height = BADGE_IMAGE_HEIGHT, .stride = BADGE_IMAGE_STRIDE,
        .image_length = BADGE_IMAGE_BYTES, .schema = BADGE_RECORD_SCHEMA_2,
        .bio_length = 3};
    uint8_t header[BADGE_RECORD_HEADER_SIZE];
    badge_record_prepare(header, &meta);
    TEST_ASSERT_EQUAL_HEX8('B', header[0]); TEST_ASSERT_EQUAL_HEX8('A', header[1]);
    TEST_ASSERT_EQUAL_HEX8('D', header[2]); TEST_ASSERT_EQUAL_HEX8('G', header[3]);
    TEST_ASSERT_EQUAL_HEX8(0x12, header[8]); TEST_ASSERT_EQUAL_HEX8(0x34, header[9]);
    TEST_ASSERT_EQUAL_HEX8(0x56, header[10]); TEST_ASSERT_EQUAL_HEX8(0x78, header[11]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, header[28]); TEST_ASSERT_EQUAL_HEX8(0xFF, header[36]);
    TEST_ASSERT_EQUAL(BADGE_RECORD_SCHEMA_2, header[4]);
    TEST_ASSERT_EQUAL(3, header[22]);
    badge_record_finalize(header, 0x01020304);
    uint8_t commit[4]; badge_record_mark_committed(commit); memcpy(header + 36, commit, 4);
    badge_record_meta_t decoded; uint32_t payload_crc;
    TEST_ASSERT_TRUE(badge_record_decode(header, &decoded, &payload_crc));
    TEST_ASSERT_EQUAL_HEX32(meta.sequence, decoded.sequence);
    TEST_ASSERT_EQUAL_HEX32(0x01020304, payload_crc);
    header[20] ^= 1; TEST_ASSERT_FALSE(badge_record_decode(header, NULL, NULL));
    header[20] ^= 1; header[4] = 3;
    TEST_ASSERT_FALSE(badge_record_decode(header, NULL, NULL));
}

static void test_name_bio_and_sequence_boundaries(void) {
    TEST_ASSERT_TRUE(badge_name_valid((const uint8_t *)"张三", 6));
    TEST_ASSERT_FALSE(badge_name_valid((const uint8_t *)"", 0));
    TEST_ASSERT_FALSE(badge_name_valid((const uint8_t *)"a\nb", 3));
    const uint8_t overlong[] = {0xC0, 0xAF};
    TEST_ASSERT_FALSE(badge_name_valid(overlong, sizeof(overlong)));
    const uint8_t surrogate[] = {0xED, 0xA0, 0x80};
    TEST_ASSERT_FALSE(badge_name_valid(surrogate, sizeof(surrogate)));
    TEST_ASSERT_TRUE(badge_bio_valid(NULL, 0));
    TEST_ASSERT_TRUE(badge_bio_valid((const uint8_t *)"保持好奇", 12));
    uint8_t bio96[BADGE_BIO_MAX_BYTES]; memset(bio96, 'a', sizeof(bio96));
    TEST_ASSERT_TRUE(badge_bio_valid(bio96, sizeof(bio96)));
    uint8_t bio97[BADGE_BIO_MAX_BYTES + 1]; memset(bio97, 'a', sizeof(bio97));
    TEST_ASSERT_FALSE(badge_bio_valid(bio97, sizeof(bio97)));
    TEST_ASSERT_FALSE(badge_bio_valid((const uint8_t *)"a\nb", 3));
    TEST_ASSERT_FALSE(badge_bio_valid(overlong, sizeof(overlong)));
    TEST_ASSERT_TRUE(badge_sequence_newer(1, UINT32_MAX));
    TEST_ASSERT_FALSE(badge_sequence_newer(UINT32_MAX, 1));
    TEST_ASSERT_FALSE(badge_sequence_newer(7, 7));
}

static void test_default_save_reload_and_slot_rotation(void) {
    const badge_profile_snapshot_t *snapshot = badge_store_snapshot(&store);
    TEST_ASSERT_TRUE(snapshot->is_default); TEST_ASSERT_EQUAL_STRING("AI Passport", snapshot->name);
    TEST_ASSERT_EQUAL_STRING("我的 AI 身份", snapshot->bio);
    save_profile("张三", "保持好奇", 0x11);
    snapshot = badge_store_snapshot(&store);
    TEST_ASSERT_FALSE(snapshot->is_default); TEST_ASSERT_EQUAL_STRING("张三", snapshot->name);
    TEST_ASSERT_EQUAL_STRING("保持好奇", snapshot->bio);
    TEST_ASSERT_EQUAL(1, snapshot->sequence); TEST_ASSERT_EQUAL_HEX8(0x11, snapshot->image[0]);
    save_profile("李四", "", 0x22);
    TEST_ASSERT_EQUAL(2, badge_store_snapshot(&store)->sequence);
    TEST_ASSERT_EQUAL_STRING("李四", badge_store_snapshot(&store)->name);
    badge_store_t rebooted;
    TEST_ASSERT_EQUAL(BADGE_STORE_OK, badge_store_init(&rebooted, backend(), defaults()));
    TEST_ASSERT_EQUAL_STRING("李四", badge_store_snapshot(&rebooted)->name);
    TEST_ASSERT_EQUAL_STRING("", badge_store_snapshot(&rebooted)->bio);
    flash[BADGE_SLOT_SIZE + BADGE_RECORD_HEADER_SIZE + 4] ^= 1;
    TEST_ASSERT_EQUAL(BADGE_STORE_OK, badge_store_init(&rebooted, backend(), defaults()));
    TEST_ASSERT_EQUAL_STRING("张三", badge_store_snapshot(&rebooted)->name);
    TEST_ASSERT_EQUAL_STRING("保持好奇", badge_store_snapshot(&rebooted)->bio);
}

static void write_u16(uint8_t *target, uint16_t value) {
    target[0] = (uint8_t)value; target[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *target, uint32_t value) {
    target[0] = (uint8_t)value; target[1] = (uint8_t)(value >> 8);
    target[2] = (uint8_t)(value >> 16); target[3] = (uint8_t)(value >> 24);
}

static void test_schema1_record_loads_with_empty_bio(void) {
    memset(flash, 0xFF, sizeof(flash));
    const char *name = "旧工牌";
    size_t name_length = strlen(name);
    uint8_t *header = flash;
    memcpy(header, "BADG", 4);
    write_u16(header + 4, BADGE_RECORD_SCHEMA_1);
    write_u16(header + 6, BADGE_RECORD_HEADER_SIZE);
    write_u32(header + 8, 7);
    write_u16(header + 12, (uint16_t)name_length);
    write_u16(header + 14, BADGE_IMAGE_FORMAT_RGB565_LE);
    write_u16(header + 16, BADGE_IMAGE_WIDTH);
    write_u16(header + 18, BADGE_IMAGE_HEIGHT);
    write_u16(header + 20, BADGE_IMAGE_STRIDE);
    write_u16(header + 22, 0);
    write_u32(header + 24, BADGE_IMAGE_BYTES);
    memcpy(flash + BADGE_RECORD_HEADER_SIZE, name, name_length);
    memset(flash + BADGE_RECORD_HEADER_SIZE + name_length, 0x2A, BADGE_IMAGE_BYTES);
    write_u32(header + 28, badge_crc32(flash + BADGE_RECORD_HEADER_SIZE,
                                      name_length + BADGE_IMAGE_BYTES));
    write_u32(header + 32, badge_crc32(header, 32));
    write_u32(header + 36, 0xC04D17EDu);

    badge_store_t legacy;
    TEST_ASSERT_EQUAL(BADGE_STORE_OK, badge_store_init(&legacy, backend(), defaults()));
    TEST_ASSERT_EQUAL_STRING("旧工牌", badge_store_snapshot(&legacy)->name);
    TEST_ASSERT_EQUAL_STRING("", badge_store_snapshot(&legacy)->bio);
    TEST_ASSERT_EQUAL_HEX8(0x2A, badge_store_snapshot(&legacy)->image[0]);
}

static void test_default_only_store_rejects_updates(void) {
    badge_store_t default_only;
    TEST_ASSERT_EQUAL(BADGE_STORE_DEFAULTED, badge_store_init_defaults(&default_only, defaults()));
    badge_upload_meta_t meta = upload("不能保存", "测试");
    TEST_ASSERT_EQUAL(BADGE_STORE_IO_ERROR, badge_store_begin_update(&default_only, &meta));
}

static void test_busy_bounds_abort_and_failures_keep_old_snapshot(void) {
    save_profile("旧资料", "旧语句", 0x33);
    badge_upload_meta_t meta = upload("新资料", "新语句");
    TEST_ASSERT_EQUAL(BADGE_STORE_OK, badge_store_begin_update(&store, &meta));
    TEST_ASSERT_EQUAL(BADGE_STORE_BUSY, badge_store_begin_update(&store, &meta));
    uint8_t extra[2] = {0};
    TEST_ASSERT_EQUAL(BADGE_STORE_INVALID, badge_store_write(&store, extra, BADGE_IMAGE_BYTES + 1));
    badge_store_abort(&store);
    TEST_ASSERT_EQUAL_STRING("旧资料", badge_store_snapshot(&store)->name);

    fail_erase = 1;
    TEST_ASSERT_EQUAL(BADGE_STORE_IO_ERROR, badge_store_begin_update(&store, &meta));
    fail_erase = 0; write_calls = 0; fail_write_call = 2;
    TEST_ASSERT_EQUAL(BADGE_STORE_IO_ERROR, badge_store_begin_update(&store, &meta));
    TEST_ASSERT_EQUAL_STRING("旧资料", badge_store_snapshot(&store)->name);
}

static void test_readback_or_commit_failure_never_publishes(void) {
    save_profile("旧资料", "旧语句", 0x44);
    badge_upload_meta_t meta = upload("新资料", "新语句");
    TEST_ASSERT_EQUAL(BADGE_STORE_OK, badge_store_begin_update(&store, &meta));
    uint8_t image[BADGE_IMAGE_BYTES]; memset(image, 0x55, sizeof(image));
    TEST_ASSERT_EQUAL(BADGE_STORE_OK, badge_store_write(&store, image, sizeof(image)));
    fail_read = 1;
    TEST_ASSERT_EQUAL(BADGE_STORE_IO_ERROR, badge_store_finish(&store));
    TEST_ASSERT_EQUAL_STRING("旧资料", badge_store_snapshot(&store)->name);

    fail_read = 0; write_calls = 0;
    TEST_ASSERT_EQUAL(BADGE_STORE_OK, badge_store_begin_update(&store, &meta));
    TEST_ASSERT_EQUAL(BADGE_STORE_OK, badge_store_write(&store, image, sizeof(image)));
    fail_write_call = (int)write_calls + 2; /* CRC fields succeed; final commit fails. */
    TEST_ASSERT_EQUAL(BADGE_STORE_IO_ERROR, badge_store_finish(&store));
    TEST_ASSERT_EQUAL_STRING("旧资料", badge_store_snapshot(&store)->name);
}

static void test_header_image_and_crc_write_failures_stay_uncommitted(void) {
    save_profile("旧资料", "旧语句", 0x66);
    badge_upload_meta_t meta = upload("新资料", "新语句");
    uint8_t image[BADGE_IMAGE_BYTES]; memset(image, 0x77, sizeof(image));

    write_calls = 0; fail_write_call = 1;
    TEST_ASSERT_EQUAL(BADGE_STORE_IO_ERROR, badge_store_begin_update(&store, &meta));
    TEST_ASSERT_EQUAL_STRING("旧资料", badge_store_snapshot(&store)->name);

    write_calls = 0; fail_write_call = 0;
    TEST_ASSERT_EQUAL(BADGE_STORE_OK, badge_store_begin_update(&store, &meta));
    fail_write_call = (int)write_calls + 1;
    TEST_ASSERT_EQUAL(BADGE_STORE_IO_ERROR, badge_store_write(&store, image, sizeof(image)));
    badge_store_abort(&store);
    TEST_ASSERT_EQUAL_STRING("旧资料", badge_store_snapshot(&store)->name);

    write_calls = 0; fail_write_call = 0;
    TEST_ASSERT_EQUAL(BADGE_STORE_OK, badge_store_begin_update(&store, &meta));
    TEST_ASSERT_EQUAL(BADGE_STORE_OK, badge_store_write(&store, image, sizeof(image)));
    fail_write_call = (int)write_calls + 1;
    TEST_ASSERT_EQUAL(BADGE_STORE_IO_ERROR, badge_store_finish(&store));
    TEST_ASSERT_EQUAL_STRING("旧资料", badge_store_snapshot(&store)->name);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_crc_and_explicit_header_vector);
    RUN_TEST(test_name_bio_and_sequence_boundaries);
    RUN_TEST(test_default_save_reload_and_slot_rotation);
    RUN_TEST(test_schema1_record_loads_with_empty_bio);
    RUN_TEST(test_default_only_store_rejects_updates);
    RUN_TEST(test_busy_bounds_abort_and_failures_keep_old_snapshot);
    RUN_TEST(test_readback_or_commit_failure_never_publishes);
    RUN_TEST(test_header_image_and_crc_write_failures_stay_uncommitted);
    return UNITY_END();
}
