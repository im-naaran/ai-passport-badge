#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "services/badge/badge_http_server.h"

enum { TOKEN = 0x12345678u };

static uint8_t badge_flash[BADGE_PARTITION_SIZE];
static uint8_t default_image[BADGE_IMAGE_BYTES];
static badge_store_t badge_store;
static uint8_t custom_flash[CUSTOM_PARTITION_SIZE];
static uint8_t custom_body[CUSTOM_IMAGE_BYTES];
static custom_store_t custom_store;
static badge_wifi_service_t wifi_service;
static badge_http_server_t http_server;
static uint8_t response_body[CUSTOM_IMAGE_BYTES + 256];
static size_t response_length;
static int response_status;
static char response_type[48];
static unsigned custom_write_calls;
static unsigned custom_fail_write_call;
static unsigned custom_fail_read_call;
static unsigned custom_read_calls;

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
    size_t chunk;
    int terminal;
    size_t max_capacity;
} body_reader_t;

static badge_store_result_t badge_erase(void *context, size_t offset, size_t length) {
    (void)context;
    memset(badge_flash + offset, 0xFF, length);
    return BADGE_STORE_OK;
}

static badge_store_result_t badge_write(void *context, size_t offset,
                                        const uint8_t *data, size_t length) {
    (void)context;
    memcpy(badge_flash + offset, data, length);
    return BADGE_STORE_OK;
}

static badge_store_result_t badge_read(void *context, size_t offset,
                                       uint8_t *data, size_t length) {
    (void)context;
    memcpy(data, badge_flash + offset, length);
    return BADGE_STORE_OK;
}

static custom_store_result_t custom_erase(void *context, size_t offset, size_t length) {
    (void)context;
    memset(custom_flash + offset, 0xFF, length);
    return CUSTOM_STORE_OK;
}

static custom_store_result_t custom_write(void *context, size_t offset,
                                          const uint8_t *data, size_t length) {
    (void)context;
    ++custom_write_calls;
    if (custom_fail_write_call && custom_write_calls == custom_fail_write_call)
        return CUSTOM_STORE_IO_ERROR;
    for (size_t i = 0; i < length; ++i) {
        if ((custom_flash[offset + i] & data[i]) != data[i]) return CUSTOM_STORE_IO_ERROR;
        custom_flash[offset + i] &= data[i];
    }
    return CUSTOM_STORE_OK;
}

static custom_store_result_t custom_read(void *context, size_t offset,
                                         uint8_t *data, size_t length) {
    (void)context;
    ++custom_read_calls;
    if (custom_fail_read_call && custom_read_calls == custom_fail_read_call)
        return CUSTOM_STORE_IO_ERROR;
    memcpy(data, custom_flash + offset, length);
    return CUSTOM_STORE_OK;
}

static void no_lock(void *context) { (void)context; }

static int read_body(void *context, uint8_t *output, size_t capacity) {
    body_reader_t *reader = context;
    if (capacity > reader->max_capacity) reader->max_capacity = capacity;
    if (reader->offset == reader->length) return reader->terminal;
    size_t take = reader->length - reader->offset;
    if (take > reader->chunk) take = reader->chunk;
    if (take > capacity) take = capacity;
    memcpy(output, reader->data + reader->offset, take);
    reader->offset += take;
    return (int)take;
}

static bool capture_response(void *context, int status, const char *type,
                             const uint8_t *data, size_t length) {
    (void)context;
    response_status = status;
    response_length = length;
    snprintf(response_type, sizeof(response_type), "%s", type);
    if (length <= sizeof(response_body)) memcpy(response_body, data, length);
    if (length < sizeof(response_body)) response_body[length] = '\0';
    return true;
}

static int64_t fixed_now(void *context) {
    (void)context;
    return 500;
}

static badge_http_response_t response(void) {
    return (badge_http_response_t){.send = capture_response};
}

static badge_http_request_t get_request(const char *path) {
    return (badge_http_request_t){.method = BADGE_HTTP_GET, .path = path};
}

static badge_http_request_t post_request(const char *path, body_reader_t *reader,
                                         const char *token, const char *content_type,
                                         size_t content_length) {
    return (badge_http_request_t){
        .method = BADGE_HTTP_POST,
        .path = path,
        .content_type = content_type,
        .session_token = token,
        .content_length = content_length,
        .context = reader,
        .read = read_body,
    };
}

static badge_http_request_t delete_request(const char *path, const char *token,
                                           size_t content_length) {
    return (badge_http_request_t){
        .method = BADGE_HTTP_DELETE,
        .path = path,
        .session_token = token,
        .content_length = content_length,
    };
}

static void drain_events(void) {
    badge_wifi_event_t event;
    while (badge_wifi_service_poll(&wifi_service, &event)) {}
}

static unsigned count_personalization_events(void) {
    unsigned count = 0;
    badge_wifi_event_t event;
    while (badge_wifi_service_poll(&wifi_service, &event))
        if (event.type == BADGE_WIFI_EVENT_PERSONALIZATION_UPDATED) ++count;
    return count;
}

static void save_direct(uint8_t slot, uint8_t value) {
    memset(custom_body, value, sizeof(custom_body));
    TEST_ASSERT_EQUAL(CUSTOM_STORE_OK, custom_store_begin_update(&custom_store, slot));
    TEST_ASSERT_EQUAL(CUSTOM_STORE_OK,
                      custom_store_write(&custom_store, custom_body, sizeof(custom_body)));
    TEST_ASSERT_EQUAL(CUSTOM_STORE_OK, custom_store_finish(&custom_store));
}

void setUp(void) {
    memset(badge_flash, 0xFF, sizeof(badge_flash));
    memset(default_image, 0, sizeof(default_image));
    badge_profile_snapshot_t defaults = {
        .name = "AI Passport",
        .image = default_image,
        .width = BADGE_IMAGE_WIDTH,
        .height = BADGE_IMAGE_HEIGHT,
        .stride = BADGE_IMAGE_STRIDE,
        .image_format = BADGE_IMAGE_FORMAT_RGB565_LE,
        .is_default = true,
        .bio = "我的 AI 身份",
        .shape = BADGE_PHOTO_SHAPE_SQUARE,
    };
    badge_store_backend_t badge_backend = {
        .mapped = badge_flash,
        .size = sizeof(badge_flash),
        .erase = badge_erase,
        .write = badge_write,
        .read = badge_read,
    };
    TEST_ASSERT_EQUAL(BADGE_STORE_DEFAULTED,
                      badge_store_init(&badge_store, badge_backend, defaults));

    memset(custom_flash, 0xFF, sizeof(custom_flash));
    custom_write_calls = custom_read_calls = 0;
    custom_fail_write_call = custom_fail_read_call = 0;
    custom_store_backend_t custom_backend = {
        .mapped = custom_flash,
        .size = sizeof(custom_flash),
        .erase = custom_erase,
        .write = custom_write,
        .read = custom_read,
    };
    TEST_ASSERT_EQUAL(CUSTOM_STORE_EMPTY, custom_store_init(&custom_store, custom_backend));
    for (size_t i = 0; i < sizeof(custom_body); ++i) custom_body[i] = (uint8_t)i;

    badge_wifi_service_init(&wifi_service,
        (badge_wifi_adapter_t){.lock = no_lock, .unlock = no_lock});
    wifi_service.machine.state = BADGE_WIFI_WAITING_CLIENT;
    badge_http_server_init(&http_server, &badge_store, (badge_http_assets_t){0},
                           fixed_now, NULL);
    badge_http_server_attach_personalization(&http_server, &custom_store);
    badge_http_server_begin_session(&http_server, &wifi_service, TOKEN);
    response_length = 0;
    response_status = 0;
    response_type[0] = '\0';
}

void tearDown(void) {}

static void test_metadata_photo_and_strict_slot_paths(void) {
    TEST_ASSERT_EQUAL(200, badge_http_server_handle(&http_server,
        &(badge_http_request_t){.method = BADGE_HTTP_GET,
                                .path = "/api/personalization"}, response()));
    TEST_ASSERT_NOT_NULL(strstr((char *)response_body, "\"slotCount\":3"));
    TEST_ASSERT_NOT_NULL(strstr((char *)response_body, "\"slot\":1,\"occupied\":false"));
    save_direct(1, 0x2A);

    badge_http_request_t photo = get_request("/api/personalization/photo/2");
    TEST_ASSERT_EQUAL(200, badge_http_server_handle(&http_server, &photo, response()));
    TEST_ASSERT_EQUAL(CUSTOM_IMAGE_BYTES, response_length);
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", response_type);
    TEST_ASSERT_EQUAL_HEX8(0x2A, response_body[0]);

    badge_http_request_t empty = get_request("/api/personalization/photo/1");
    TEST_ASSERT_EQUAL(404, badge_http_server_handle(&http_server, &empty, response()));
    TEST_ASSERT_NOT_NULL(strstr((char *)response_body, "slot_empty"));
    const char *invalid[] = {"/api/personalization/photo/0",
                             "/api/personalization/photo/4",
                             "/api/personalization/photo/01",
                             "/api/personalization/photo/2/extra"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        badge_http_request_t request = get_request(invalid[i]);
        TEST_ASSERT_EQUAL(422, badge_http_server_handle(&http_server, &request, response()));
    }
}

static void test_post_streams_fixed_image_and_publishes_update(void) {
    body_reader_t reader = {custom_body, sizeof(custom_body), 0, 997, 0, 0};
    badge_http_request_t request = post_request("/api/personalization/slot/3", &reader,
        "0000000012345678", "application/octet-stream", sizeof(custom_body));
    TEST_ASSERT_EQUAL(200, badge_http_server_handle(&http_server, &request, response()));
    TEST_ASSERT_EQUAL(CUSTOM_STREAM_CHUNK_BYTES, reader.max_capacity);
    const custom_slot_snapshot_t *snapshot = custom_store_snapshot(&custom_store, 2);
    TEST_ASSERT_TRUE(snapshot->occupied);
    TEST_ASSERT_EQUAL(1, snapshot->sequence);
    TEST_ASSERT_EQUAL_HEX8(custom_body[0], snapshot->image[0]);
    TEST_ASSERT_EQUAL_HEX8(custom_body[CUSTOM_IMAGE_BYTES - 1],
                           snapshot->image[CUSTOM_IMAGE_BYTES - 1]);
    TEST_ASSERT_EQUAL(1, count_personalization_events());
    TEST_ASSERT_TRUE(wifi_service.machine.save_success);
}

static void test_post_accepts_uniform_and_varied_pixel_bytes(void) {
    TEST_ASSERT_EQUAL_UINT32(8192, BADGE_HTTP_SERVER_TASK_STACK_BYTES);

    memset(custom_body, 0x39, sizeof(custom_body));
    body_reader_t uniform = {custom_body, sizeof(custom_body), 0, 733, 0, 0};
    badge_http_request_t request = post_request("/api/personalization/slot/1", &uniform,
        "0000000012345678", "application/octet-stream", sizeof(custom_body));
    TEST_ASSERT_EQUAL(200, badge_http_server_handle(&http_server, &request, response()));
    drain_events();

    uint32_t state = 0x6D2B79F5u;
    for (size_t i = 0; i < sizeof(custom_body); ++i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        custom_body[i] = (uint8_t)state;
    }
    body_reader_t varied = {custom_body, sizeof(custom_body), 0, 911, 0, 0};
    request = post_request("/api/personalization/slot/1", &varied,
        "0000000012345678", "application/octet-stream", sizeof(custom_body));
    TEST_ASSERT_EQUAL(200, badge_http_server_handle(&http_server, &request, response()));
    TEST_ASSERT_EQUAL(CUSTOM_STREAM_CHUNK_BYTES, varied.max_capacity);
    TEST_ASSERT_EQUAL(2, custom_store_snapshot(&custom_store, 0)->sequence);
}

static void test_post_rejects_headers_paths_busy_truncation_and_timeout(void) {
    body_reader_t reader = {custom_body, sizeof(custom_body), 0, 1024, 0, 0};
    badge_http_request_t request = post_request("/api/personalization/slot/1", &reader,
        "bad", "application/octet-stream", sizeof(custom_body));
    TEST_ASSERT_EQUAL(403, badge_http_server_handle(&http_server, &request, response()));
    request = post_request("/api/personalization/slot/1", &reader,
        "0000000012345678", "multipart/form-data", sizeof(custom_body));
    TEST_ASSERT_EQUAL(415, badge_http_server_handle(&http_server, &request, response()));
    request = post_request("/api/personalization/slot/1", &reader,
        "0000000012345678", "application/octet-stream", sizeof(custom_body) - 1);
    TEST_ASSERT_EQUAL(400, badge_http_server_handle(&http_server, &request, response()));
    request = post_request("/api/personalization/slot/0", &reader,
        "0000000012345678", "application/octet-stream", sizeof(custom_body));
    TEST_ASSERT_EQUAL(422, badge_http_server_handle(&http_server, &request, response()));

    TEST_ASSERT_TRUE(badge_wifi_service_begin_upload(&wifi_service));
    request = post_request("/api/personalization/slot/1", &reader,
        "0000000012345678", "application/octet-stream", sizeof(custom_body));
    TEST_ASSERT_EQUAL(409, badge_http_server_handle(&http_server, &request, response()));
    badge_wifi_service_finish_upload(&wifi_service, false);

    body_reader_t truncated = {custom_body, 100, 0, 17, 0, 0};
    request = post_request("/api/personalization/slot/1", &truncated,
        "0000000012345678", "application/octet-stream", sizeof(custom_body));
    TEST_ASSERT_EQUAL(400, badge_http_server_handle(&http_server, &request, response()));
    TEST_ASSERT_FALSE(custom_store_snapshot(&custom_store, 0)->occupied);

    body_reader_t timeout = {custom_body, 100, 0, 17, BADGE_HTTP_READ_TIMEOUT, 0};
    request = post_request("/api/personalization/slot/1", &timeout,
        "0000000012345678", "application/octet-stream", sizeof(custom_body));
    TEST_ASSERT_EQUAL(408, badge_http_server_handle(&http_server, &request, response()));
    TEST_ASSERT_FALSE(custom_store_snapshot(&custom_store, 0)->occupied);

    body_reader_t storage = {custom_body, sizeof(custom_body), 0, 1024, 0, 0};
    custom_fail_write_call = custom_write_calls + 2; /* Header succeeds, first image chunk fails. */
    request = post_request("/api/personalization/slot/1", &storage,
        "0000000012345678", "application/octet-stream", sizeof(custom_body));
    TEST_ASSERT_EQUAL(500, badge_http_server_handle(&http_server, &request, response()));
    TEST_ASSERT_FALSE(custom_store_snapshot(&custom_store, 0)->occupied);
}

static void test_delete_is_transactional_idempotent_and_validated(void) {
    save_direct(0, 0x5A);
    drain_events();
    badge_http_request_t request = delete_request("/api/personalization/slot/1",
                                                   "0000000012345678", 0);
    TEST_ASSERT_EQUAL(200, badge_http_server_handle(&http_server, &request, response()));
    TEST_ASSERT_FALSE(custom_store_snapshot(&custom_store, 0)->occupied);
    TEST_ASSERT_EQUAL(2, custom_store_snapshot(&custom_store, 0)->sequence);
    TEST_ASSERT_EQUAL(1, count_personalization_events());

    drain_events();
    TEST_ASSERT_EQUAL(200, badge_http_server_handle(&http_server, &request, response()));
    TEST_ASSERT_EQUAL(0, count_personalization_events());
    request = delete_request("/api/personalization/slot/1",
                             "0000000012345678", 1);
    TEST_ASSERT_EQUAL(400, badge_http_server_handle(&http_server, &request, response()));
    request = delete_request("/api/personalization/slot/4",
                             "0000000012345678", 0);
    TEST_ASSERT_EQUAL(422, badge_http_server_handle(&http_server, &request, response()));

    save_direct(0, 0x5B);
    custom_fail_write_call = custom_write_calls + 2; /* Tombstone CRC write fails. */
    request = delete_request("/api/personalization/slot/1",
                             "0000000012345678", 0);
    TEST_ASSERT_EQUAL(500, badge_http_server_handle(&http_server, &request, response()));
    TEST_ASSERT_TRUE(custom_store_snapshot(&custom_store, 0)->occupied);
    TEST_ASSERT_EQUAL_HEX8(0x5B, custom_store_snapshot(&custom_store, 0)->image[0]);
}

static void test_unavailable_store_returns_503_without_mutation(void) {
    custom_store_t unavailable;
    TEST_ASSERT_EQUAL(CUSTOM_STORE_EMPTY, custom_store_init_empty(&unavailable));
    badge_http_server_attach_personalization(&http_server, &unavailable);
    badge_http_request_t get = get_request("/api/personalization");
    TEST_ASSERT_EQUAL(503, badge_http_server_handle(&http_server, &get, response()));
    body_reader_t reader = {custom_body, sizeof(custom_body), 0, 1024, 0, 0};
    badge_http_request_t post = post_request("/api/personalization/slot/1", &reader,
        "0000000012345678", "application/octet-stream", sizeof(custom_body));
    TEST_ASSERT_EQUAL(503, badge_http_server_handle(&http_server, &post, response()));
    badge_http_request_t clear = delete_request("/api/personalization/slot/1",
                                                 "0000000012345678", 0);
    TEST_ASSERT_EQUAL(503, badge_http_server_handle(&http_server, &clear, response()));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_metadata_photo_and_strict_slot_paths);
    RUN_TEST(test_post_streams_fixed_image_and_publishes_update);
    RUN_TEST(test_post_accepts_uniform_and_varied_pixel_bytes);
    RUN_TEST(test_post_rejects_headers_paths_busy_truncation_and_timeout);
    RUN_TEST(test_delete_is_transactional_idempotent_and_validated);
    RUN_TEST(test_unavailable_store_returns_503_without_mutation);
    return UNITY_END();
}
