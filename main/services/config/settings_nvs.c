#include "settings_store.h"
#include "nvs.h"

static settings_result_t read_record(void *context, uint8_t *bytes, size_t *length) {
    (void)context;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("passport_cfg", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return SETTINGS_DEFAULTED;
    if (err != ESP_OK) return SETTINGS_IO_ERROR;
    err = nvs_get_blob(handle, "settings", bytes, length);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NVS_INVALID_LENGTH || err == ESP_ERR_NVS_TYPE_MISMATCH)
        return SETTINGS_DEFAULTED;
    return err == ESP_OK ? SETTINGS_OK : SETTINGS_IO_ERROR;
}
static settings_result_t save_record(void *context, const uint8_t *bytes, size_t length) {
    (void)context;
    nvs_handle_t handle;
    // This product namespace remains independent from data owned by other firmware roles.
    esp_err_t err = nvs_open("passport_cfg", NVS_READWRITE, &handle);
    if (err != ESP_OK) return SETTINGS_IO_ERROR;
    err = nvs_set_blob(handle, "settings", bytes, length);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK ? SETTINGS_OK : SETTINGS_IO_ERROR;
}
settings_backend_t settings_nvs_backend(void) {
    return (settings_backend_t){ .read = read_record, .save = save_record };
}
