#include "custom_partition.h"

#include <string.h>

enum {
    CUSTOM_PARTITION_SUBTYPE = 0x41,
    CUSTOM_PARTITION_ADDRESS = 0x35A000,
    CUSTOM_ERASE_SECTOR_SIZE = 0x1000,
};

static bool range_valid(size_t offset, size_t length) {
    return offset <= CUSTOM_PARTITION_SIZE && length <= CUSTOM_PARTITION_SIZE - offset;
}

static custom_store_result_t erase(void *context, size_t offset, size_t length) {
    custom_partition_t *owner = context;
    if (!owner || !owner->partition || !range_valid(offset, length) ||
        offset % CUSTOM_ERASE_SECTOR_SIZE || length % CUSTOM_ERASE_SECTOR_SIZE)
        return CUSTOM_STORE_INVALID;
    return esp_partition_erase_range(owner->partition, offset, length) == ESP_OK ?
        CUSTOM_STORE_OK : CUSTOM_STORE_IO_ERROR;
}

static custom_store_result_t write(void *context, size_t offset,
                                   const uint8_t *data, size_t length) {
    custom_partition_t *owner = context;
    if (!owner || !owner->partition || (!data && length) || !range_valid(offset, length))
        return CUSTOM_STORE_INVALID;
    return esp_partition_write(owner->partition, offset, data, length) == ESP_OK ?
        CUSTOM_STORE_OK : CUSTOM_STORE_IO_ERROR;
}

static custom_store_result_t read(void *context, size_t offset,
                                  uint8_t *data, size_t length) {
    custom_partition_t *owner = context;
    if (!owner || !owner->partition || (!data && length) || !range_valid(offset, length))
        return CUSTOM_STORE_INVALID;
    return esp_partition_read(owner->partition, offset, data, length) == ESP_OK ?
        CUSTOM_STORE_OK : CUSTOM_STORE_IO_ERROR;
}

custom_store_result_t custom_partition_open(custom_partition_t *owner,
                                            custom_store_backend_t *backend) {
    if (!owner || !backend) return CUSTOM_STORE_INVALID;
    memset(owner, 0, sizeof(*owner));
    owner->partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
        (esp_partition_subtype_t)CUSTOM_PARTITION_SUBTYPE, "custom_data");
    if (!owner->partition || owner->partition->address != CUSTOM_PARTITION_ADDRESS ||
        owner->partition->size != CUSTOM_PARTITION_SIZE)
        return CUSTOM_STORE_IO_ERROR;

    const void *mapped = NULL;
    /* The application-lifetime mapping lets LVGL read one active image without a RAM copy. */
    if (esp_partition_mmap(owner->partition, 0, CUSTOM_PARTITION_SIZE,
                           ESP_PARTITION_MMAP_DATA, &mapped,
                           &owner->mmap_handle) != ESP_OK)
        return CUSTOM_STORE_IO_ERROR;
    owner->mapped = mapped;
    *backend = (custom_store_backend_t){
        .context = owner,
        .mapped = owner->mapped,
        .size = owner->partition->size,
        .erase = erase,
        .write = write,
        .read = read,
    };
    return CUSTOM_STORE_OK;
}
