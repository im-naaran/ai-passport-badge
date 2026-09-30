#include "badge_http_server.h"
#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#endif

static badge_store_result_t store_begin(void *context, const badge_upload_meta_t *meta) {
    return badge_store_begin_update(context, meta);
}
static badge_store_result_t store_write(void *context, const uint8_t *data, size_t length) {
    return badge_store_write(context, data, length);
}
static badge_store_result_t store_finish(void *context) { return badge_store_finish(context); }
static void store_abort(void *context) { badge_store_abort(context); }

static bool parse_token(const char *text, uint64_t *value) {
    if (!text || !value || strlen(text) != 16) return false;
    uint64_t parsed = 0;
    for (size_t i = 0; i < 16; ++i) {
        unsigned digit;
        if (text[i] >= '0' && text[i] <= '9') digit = (unsigned)(text[i] - '0');
        else if (text[i] >= 'a' && text[i] <= 'f') digit = (unsigned)(text[i] - 'a' + 10);
        else if (text[i] >= 'A' && text[i] <= 'F') digit = (unsigned)(text[i] - 'A' + 10);
        else return false;
        parsed = (parsed << 4) | digit;
    }
    *value = parsed;
    return true;
}

static int send_body(badge_http_response_t response, int status, const char *type,
                     const void *body, size_t length) {
    if (!response.send || !response.send(response.context, status, type, body, length)) return -1;
    return status;
}

static int send_error(badge_http_response_t response, int status, const char *code) {
    char body[96];
    int length = snprintf(body, sizeof(body), "{\"error\":\"%s\"}", code);
    return send_body(response, status, "application/json; charset=utf-8", body, (size_t)length);
}

static const badge_http_asset_t *asset_for(const badge_http_server_t *server, const char *path) {
    size_t path_length = strcspn(path, "?");
    if (path_length == 1 && strncmp(path, "/", path_length) == 0)
        return &server->assets.index_html;
    if (path_length == strlen("/style.css") &&
        strncmp(path, "/style.css", path_length) == 0)
        return &server->assets.style_css;
    if (path_length == strlen("/badge_image.js") &&
        strncmp(path, "/badge_image.js", path_length) == 0)
        return &server->assets.image_js;
    if (path_length == strlen("/app.js") &&
        strncmp(path, "/app.js", path_length) == 0)
        return &server->assets.app_js;
    return NULL;
}

static size_t json_escape(char *output, size_t capacity, const char *text) {
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p && used + 2 < capacity; ++p) {
        if (*p == '"' || *p == '\\') { output[used++] = '\\'; output[used++] = (char)*p; }
        else if (*p < 0x20) continue;
        else output[used++] = (char)*p;
    }
    output[used] = '\0';
    return used;
}

static const char *shape_name(badge_photo_shape_t shape) {
    switch (shape) {
    case BADGE_PHOTO_SHAPE_SQUARE: return "square";
    case BADGE_PHOTO_SHAPE_ROUNDED: return "rounded";
    case BADGE_PHOTO_SHAPE_CIRCLE: return "circle";
    default: return NULL;
    }
}

typedef enum { SLOT_PATH_NO_MATCH, SLOT_PATH_INVALID, SLOT_PATH_VALID } slot_path_result_t;

static slot_path_result_t parse_slot_path(const char *path, const char *prefix,
                                          uint8_t *logical_slot) {
    size_t prefix_length = strlen(prefix);
    if (strncmp(path, prefix, prefix_length) != 0) return SLOT_PATH_NO_MATCH;
    const char *suffix = path + prefix_length;
    if (suffix[0] < '1' || suffix[0] > '3' || suffix[1] != '\0')
        return SLOT_PATH_INVALID;
    if (logical_slot) *logical_slot = (uint8_t)(suffix[0] - '1');
    return SLOT_PATH_VALID;
}

static int custom_unavailable(badge_http_server_t *server, badge_http_response_t response) {
    return !server->custom_store || !custom_store_available(server->custom_store) ?
        send_error(response, 503, "storage_unavailable") : 0;
}

static int handle_custom_get(badge_http_server_t *server, const badge_http_request_t *request,
                             badge_http_response_t response) {
    if (strcmp(request->path, "/api/personalization") == 0) {
        int unavailable = custom_unavailable(server, response);
        if (unavailable) return unavailable;
        char body[256];
        int length = snprintf(body, sizeof(body),
            "{\"slotCount\":3,\"width\":240,\"height\":320,\"imageBytes\":153600,"
            "\"slots\":[{\"slot\":1,\"occupied\":%s},{\"slot\":2,\"occupied\":%s},"
            "{\"slot\":3,\"occupied\":%s}]}",
            custom_store_snapshot(server->custom_store, 0)->occupied ? "true" : "false",
            custom_store_snapshot(server->custom_store, 1)->occupied ? "true" : "false",
            custom_store_snapshot(server->custom_store, 2)->occupied ? "true" : "false");
        return send_body(response, 200, "application/json; charset=utf-8", body, (size_t)length);
    }
    uint8_t slot;
    slot_path_result_t path = parse_slot_path(request->path,
                                              "/api/personalization/photo/", &slot);
    if (path == SLOT_PATH_NO_MATCH) return 0;
    if (path == SLOT_PATH_INVALID) return send_error(response, 422, "invalid_slot");
    int unavailable = custom_unavailable(server, response);
    if (unavailable) return unavailable;
    const custom_slot_snapshot_t *snapshot = custom_store_snapshot(server->custom_store, slot);
    if (!snapshot || !snapshot->occupied) return send_error(response, 404, "slot_empty");
    return send_body(response, 200, "application/octet-stream", snapshot->image,
                     CUSTOM_IMAGE_BYTES);
}

static int handle_get(badge_http_server_t *server, const badge_http_request_t *request,
                      badge_http_response_t response) {
    const badge_http_asset_t *asset = asset_for(server, request->path);
    if (asset) {
        if (!asset->data || !asset->length) return send_error(response, 503, "assets_unavailable");
        return send_body(response, 200, asset->content_type, asset->data, asset->length);
    }
    badge_wifi_status_t status = badge_wifi_service_status(server->wifi);
    if (strcmp(request->path, "/api/status") == 0) {
        char body[192];
        int length = snprintf(body, sizeof(body),
            "{\"token\":\"%016llx\",\"width\":200,\"height\":200,\"imageBytes\":80000,"
            "\"nameMaxBytes\":48,\"connected\":%s}",
            (unsigned long long)server->session_token, status.client_count ? "true" : "false");
        return send_body(response, 200, "application/json; charset=utf-8", body, (size_t)length);
    }
    int custom = handle_custom_get(server, request, response);
    if (custom) return custom;
    const badge_profile_snapshot_t *profile = badge_store_snapshot(server->store);
    if (!profile) return send_error(response, 500, "storage_error");
    if (strcmp(request->path, "/api/profile") == 0) {
        const char *shape = shape_name(profile->shape);
        if (!shape) return send_error(response, 500, "storage_error");
        char escaped[BADGE_NAME_MAX_BYTES * 2 + 1];
        char escaped_bio[BADGE_BIO_MAX_BYTES * 2 + 1];
        json_escape(escaped, sizeof(escaped), profile->name);
        json_escape(escaped_bio, sizeof(escaped_bio), profile->bio);
        char body[BADGE_NAME_MAX_BYTES * 2 + BADGE_BIO_MAX_BYTES * 2 + 128];
        int length = snprintf(body, sizeof(body),
            "{\"name\":\"%s\",\"bio\":\"%s\",\"shape\":\"%s\",\"width\":%u,\"height\":%u,\"isDefault\":%s}",
            escaped, escaped_bio, shape, profile->width, profile->height,
            profile->is_default ? "true" : "false");
        return send_body(response, 200, "application/json; charset=utf-8", body, (size_t)length);
    }
    if (strcmp(request->path, "/api/photo") == 0)
        return send_body(response, 200, "application/octet-stream", profile->image,
                         BADGE_IMAGE_BYTES);
    return send_error(response, 404, "not_found");
}

static int custom_result_status(custom_store_result_t result, const char **code) {
    switch (result) {
    case CUSTOM_STORE_BUSY: *code = "busy"; return 409;
    case CUSTOM_STORE_INVALID: *code = "invalid_request"; return 400;
    case CUSTOM_STORE_IO_ERROR: *code = "storage_error"; return 500;
    default: *code = "storage_error"; return 500;
    }
}

static int custom_mutation_complete(badge_http_server_t *server,
                                    badge_http_response_t response,
                                    const char *body, size_t length,
                                    bool publish_update) {
    badge_wifi_service_finish_upload(server->wifi, true);
    if (publish_update) badge_wifi_service_personalization_updated(server->wifi);
    int sent = send_body(response, 200, "application/json; charset=utf-8", body, length);
    if (sent == 200 && server->now_us)
        badge_wifi_service_response_finished(server->wifi, server->now_us(server->clock_context));
    return sent;
}

static int handle_custom_post(badge_http_server_t *server,
                              const badge_http_request_t *request,
                              badge_http_response_t response, uint8_t slot) {
    int unavailable = custom_unavailable(server, response);
    if (unavailable) return unavailable;
    if (!request->content_type || strcmp(request->content_type, "application/octet-stream") != 0)
        return send_error(response, 415, "invalid_content_type");
    if (request->content_length != CUSTOM_IMAGE_BYTES)
        return send_error(response, 400, "invalid_length");
    uint64_t supplied = 0;
    if (!parse_token(request->session_token, &supplied) || supplied != server->session_token)
        return send_error(response, 403, "invalid_token");
    if (!badge_wifi_service_begin_upload(server->wifi)) return send_error(response, 409, "busy");

    custom_store_result_t result = custom_store_begin_update(server->custom_store, slot);
    if (result != CUSTOM_STORE_OK) {
        badge_wifi_service_finish_upload(server->wifi, false);
        const char *code;
        return send_error(response, custom_result_status(result, &code), code);
    }
    size_t received = 0;
    uint8_t buffer[CUSTOM_STREAM_CHUNK_BYTES];
    while (received < request->content_length) {
        size_t capacity = request->content_length - received;
        if (capacity > sizeof(buffer)) capacity = sizeof(buffer);
        int count = request->read ? request->read(request->context, buffer, capacity) :
                                    BADGE_HTTP_READ_ERROR;
        if (count <= 0) {
            custom_store_abort(server->custom_store);
            badge_wifi_service_finish_upload(server->wifi, false);
            return send_error(response, count == BADGE_HTTP_READ_TIMEOUT ? 408 : 400,
                              count == BADGE_HTTP_READ_TIMEOUT ? "timeout" : "invalid_length");
        }
        result = custom_store_write(server->custom_store, buffer, (size_t)count);
        if (result != CUSTOM_STORE_OK) {
            custom_store_abort(server->custom_store);
            badge_wifi_service_finish_upload(server->wifi, false);
            const char *code;
            return send_error(response, custom_result_status(result, &code), code);
        }
        received += (size_t)count;
    }
    result = custom_store_finish(server->custom_store);
    if (result != CUSTOM_STORE_OK) {
        custom_store_abort(server->custom_store);
        badge_wifi_service_finish_upload(server->wifi, false);
        const char *code;
        return send_error(response, custom_result_status(result, &code), code);
    }
    static const char saved[] = "{\"ok\":true}";
    return custom_mutation_complete(server, response, saved, sizeof(saved) - 1, true);
}

static int handle_delete(badge_http_server_t *server, const badge_http_request_t *request,
                         badge_http_response_t response) {
    uint8_t slot;
    slot_path_result_t path = parse_slot_path(request->path,
                                              "/api/personalization/slot/", &slot);
    if (path == SLOT_PATH_NO_MATCH) return send_error(response, 404, "not_found");
    if (path == SLOT_PATH_INVALID) return send_error(response, 422, "invalid_slot");
    int unavailable = custom_unavailable(server, response);
    if (unavailable) return unavailable;
    if (request->content_length != 0) return send_error(response, 400, "invalid_length");
    uint64_t supplied = 0;
    if (!parse_token(request->session_token, &supplied) || supplied != server->session_token)
        return send_error(response, 403, "invalid_token");
    if (!badge_wifi_service_begin_upload(server->wifi)) return send_error(response, 409, "busy");
    bool occupied = custom_store_snapshot(server->custom_store, slot)->occupied;
    custom_store_result_t result = custom_store_clear(server->custom_store, slot);
    if (result != CUSTOM_STORE_OK) {
        badge_wifi_service_finish_upload(server->wifi, false);
        const char *code;
        return send_error(response, custom_result_status(result, &code), code);
    }
    static const char cleared[] = "{\"ok\":true,\"occupied\":false}";
    return custom_mutation_complete(server, response, cleared, sizeof(cleared) - 1, occupied);
}

static int result_status(badge_http_result_t result, const char **code) {
    switch (result) {
    case BADGE_HTTP_ERROR_TOKEN: *code = "invalid_token"; return 403;
    case BADGE_HTTP_ERROR_BUSY: *code = "busy"; return 409;
    case BADGE_HTTP_ERROR_NAME: *code = "invalid_name"; return 422;
    case BADGE_HTTP_ERROR_BIO: *code = "invalid_bio"; return 422;
    case BADGE_HTTP_ERROR_SHAPE: *code = "invalid_shape"; return 422;
    case BADGE_HTTP_ERROR_LENGTH:
    case BADGE_HTTP_ERROR_EXTRA_DATA: *code = "invalid_length"; return 400;
    case BADGE_HTTP_ERROR_TIMEOUT: *code = "timeout"; return 408;
    case BADGE_HTTP_ERROR_STORAGE: *code = "storage_error"; return 500;
    default: *code = "invalid_request"; return 400;
    }
}

static int handle_post(badge_http_server_t *server, const badge_http_request_t *request,
                       badge_http_response_t response) {
    uint8_t slot;
    slot_path_result_t custom_path = parse_slot_path(request->path,
                                                     "/api/personalization/slot/", &slot);
    if (custom_path == SLOT_PATH_INVALID) return send_error(response, 422, "invalid_slot");
    if (custom_path == SLOT_PATH_VALID)
        return handle_custom_post(server, request, response, slot);
    if (strcmp(request->path, "/api/profile") != 0) return send_error(response, 404, "not_found");
    if (!request->content_type || strcmp(request->content_type, "application/octet-stream") != 0)
        return send_error(response, 415, "invalid_content_type");
    uint64_t supplied = 0;
    if (!parse_token(request->session_token, &supplied))
        return send_error(response, 403, "invalid_token");
    if (!badge_wifi_service_begin_upload(server->wifi)) return send_error(response, 409, "busy");

    badge_http_writer_t writer = {server->store, store_begin, store_write, store_finish, store_abort};
    badge_http_parser_t parser;
    badge_http_result_t result = badge_http_parser_init(&parser, request->content_length,
                                                        supplied, server->session_token, writer);
    uint8_t buffer[1024];
    while (result == BADGE_HTTP_MORE && parser.received < request->content_length) {
        int count = request->read ? request->read(request->context, buffer, sizeof(buffer)) :
                                    BADGE_HTTP_READ_ERROR;
        if (count > 0) result = badge_http_parser_feed(&parser, buffer, (size_t)count);
        else result = badge_http_parser_end(&parser, count == BADGE_HTTP_READ_TIMEOUT ?
                                             BADGE_HTTP_END_TIMEOUT : BADGE_HTTP_END_DISCONNECTED);
    }
    if (result == BADGE_HTTP_MORE) result = badge_http_parser_end(&parser, BADGE_HTTP_END_DISCONNECTED);
    if (result != BADGE_HTTP_COMPLETE) {
        badge_wifi_service_finish_upload(server->wifi, false);
        const char *code;
        return send_error(response, result_status(result, &code), code);
    }

    badge_wifi_service_finish_upload(server->wifi, true);
    badge_wifi_service_profile_updated(server->wifi);
    static const char saved[] = "{\"ok\":true}";
    int sent = send_body(response, 200, "application/json; charset=utf-8", saved, sizeof(saved) - 1);
    if (sent == 200 && server->now_us)
        badge_wifi_service_response_finished(server->wifi, server->now_us(server->clock_context));
    return sent;
}

void badge_http_server_init(badge_http_server_t *server, badge_store_t *store,
                            badge_http_assets_t assets,
                            int64_t (*now_us)(void *context), void *clock_context) {
    if (!server) return;
    *server = (badge_http_server_t){.store = store, .assets = assets, .now_us = now_us,
                                   .clock_context = clock_context};
}

void badge_http_server_attach_personalization(badge_http_server_t *server,
                                              custom_store_t *store) {
    if (server) server->custom_store = store;
}

void badge_http_server_begin_session(badge_http_server_t *server,
                                     badge_wifi_service_t *wifi, uint64_t token) {
    if (!server) return;
    server->wifi = wifi;
    server->session_token = token ? token : 1;
}

int badge_http_server_handle(badge_http_server_t *server,
                             const badge_http_request_t *request,
                             badge_http_response_t response) {
    if (!server || !server->store || !server->wifi || !request || !request->path)
        return send_error(response, 500, "server_error");
    /* A queued DHCP ACK only proves that the server handed a frame to the Wi-Fi stack.
     * Actual HTTP traffic proves that the phone accepted its address and can reach us. */
    badge_wifi_service_on_station(server->wifi, true);
    switch (request->method) {
    case BADGE_HTTP_GET: return handle_get(server, request, response);
    case BADGE_HTTP_POST: return handle_post(server, request, response);
    case BADGE_HTTP_DELETE: return handle_delete(server, request, response);
    default: return send_error(response, 405, "method_not_allowed");
    }
}

__attribute__((weak)) badge_http_assets_t badge_http_embedded_assets(void) {
    return (badge_http_assets_t){0};
}

#ifdef ESP_PLATFORM
static const char *TAG = "badge_http";

typedef struct { httpd_req_t *request; } esp_request_context_t;

static int esp_read_body(void *context, uint8_t *buffer, size_t capacity) {
    esp_request_context_t *request = context;
    int result = httpd_req_recv(request->request, (char *)buffer, capacity);
    return result == HTTPD_SOCK_ERR_TIMEOUT ? BADGE_HTTP_READ_TIMEOUT : result;
}

static bool esp_send(void *context, int status, const char *content_type,
                     const uint8_t *body, size_t length) {
    httpd_req_t *request = context;
    const char *status_text = "500 Internal Server Error";
    switch (status) {
        case 200: status_text = "200 OK"; break;
        case 400: status_text = "400 Bad Request"; break;
        case 401: status_text = "401 Unauthorized"; break;
        case 403: status_text = "403 Forbidden"; break;
        case 404: status_text = "404 Not Found"; break;
        case 405: status_text = "405 Method Not Allowed"; break;
        case 408: status_text = "408 Request Timeout"; break;
        case 409: status_text = "409 Conflict"; break;
        case 413: status_text = "413 Payload Too Large"; break;
        case 415: status_text = "415 Unsupported Media Type"; break;
        case 422: status_text = "422 Unprocessable Content"; break;
        case 500: status_text = "500 Internal Server Error"; break;
        case 503: status_text = "503 Service Unavailable"; break;
    }
    httpd_resp_set_status(request, status_text);
    httpd_resp_set_type(request, content_type);
    /* The configuration page is firmware-owned and may change between flashes. iOS captive
     * portals otherwise reuse stale HTML/JS for the same fixed AP address. */
    httpd_resp_set_hdr(request, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(request, "Pragma", "no-cache");
    httpd_resp_set_hdr(request, "Expires", "0");
    return httpd_resp_send(request, (const char *)body, length) == ESP_OK;
}

static esp_err_t esp_handler(httpd_req_t *request) {
    badge_http_server_t *server = request->user_ctx;
    if ((request->method == HTTP_GET && strcmp(request->uri, "/") == 0) ||
        request->method == HTTP_POST || request->method == HTTP_DELETE) {
        const char *method = request->method == HTTP_GET ? "GET" :
            (request->method == HTTP_POST ? "POST" : "DELETE");
        ESP_LOGI(TAG, "%s %s content_len=%u",
                 method, request->uri, (unsigned)request->content_len);
    }
    char content_type[40] = {0};
    char token[32] = {0};
    if (httpd_req_get_hdr_value_len(request, "Content-Type") < sizeof(content_type))
        httpd_req_get_hdr_value_str(request, "Content-Type", content_type, sizeof(content_type));
    if (httpd_req_get_hdr_value_len(request, "X-Passport-Session") < sizeof(token))
        httpd_req_get_hdr_value_str(request, "X-Passport-Session", token, sizeof(token));
    esp_request_context_t read_context = {.request = request};
    badge_http_request_t core = {
        .method = request->method == HTTP_GET ? BADGE_HTTP_GET :
            (request->method == HTTP_DELETE ? BADGE_HTTP_DELETE : BADGE_HTTP_POST),
        .path = request->uri,
        .content_type = content_type,
        .session_token = token,
        .content_length = request->content_len,
        .context = &read_context,
        .read = esp_read_body,
    };
    badge_http_response_t response = {.context = request, .send = esp_send};
    return badge_http_server_handle(server, &core, response) < 0 ? ESP_FAIL : ESP_OK;
}

static bool runtime_start(void *context, badge_wifi_service_t *wifi) {
    badge_http_server_t *server = context;
    uint64_t token = ((uint64_t)esp_random() << 32) | esp_random();
    badge_http_server_begin_session(server, wifi, token);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* A raw image upload nests socket receive and transactional Flash verification.
       Keep explicit headroom above ESP-IDF's 4 KiB default for the worst path. */
    config.stack_size = BADGE_HTTP_SERVER_TASK_STACK_BYTES;
    config.max_uri_handlers = 12;
    config.uri_match_fn = httpd_uri_match_wildcard;
    httpd_handle_t handle = NULL;
    esp_err_t result = httpd_start(&handle, &config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(result));
        return false;
    }
    static const struct { const char *path; httpd_method_t method; } routes[] = {
        {"/", HTTP_GET}, {"/style.css", HTTP_GET}, {"/badge_image.js", HTTP_GET},
        {"/app.js", HTTP_GET}, {"/api/status", HTTP_GET}, {"/api/profile", HTTP_GET},
        {"/api/photo", HTTP_GET}, {"/api/profile", HTTP_POST},
        {"/api/personalization", HTTP_GET},
        {"/api/personalization/photo/*", HTTP_GET},
        {"/api/personalization/slot/*", HTTP_POST},
        {"/api/personalization/slot/*", HTTP_DELETE},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        httpd_uri_t uri = {.uri = routes[i].path, .method = routes[i].method,
                           .handler = esp_handler, .user_ctx = server};
        result = httpd_register_uri_handler(handle, &uri);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "route registration failed at %u: %s",
                     (unsigned)i, esp_err_to_name(result));
            httpd_stop(handle);
            return false;
        }
    }
    server->platform_handle = handle;
    ESP_LOGI(TAG, "HTTP ready stack=%u", (unsigned)config.stack_size);
    return true;
}

static void runtime_stop(void *context) {
    badge_http_server_t *server = context;
    if (server->platform_handle) httpd_stop(server->platform_handle);
    server->platform_handle = NULL;
    server->session_token = 0;
    server->wifi = NULL;
}
#else
static bool runtime_start(void *context, badge_wifi_service_t *wifi) {
    badge_http_server_begin_session(context, wifi, 1);
    return true;
}
static void runtime_stop(void *context) {
    badge_http_server_t *server = context;
    server->session_token = 0; server->wifi = NULL;
}
#endif

badge_wifi_http_runtime_t badge_http_server_runtime(badge_http_server_t *server) {
    return (badge_wifi_http_runtime_t){.context = server, .start = runtime_start,
                                       .stop = runtime_stop};
}
