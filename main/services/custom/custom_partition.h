#pragma once

#include "custom_store.h"
#include "esp_partition.h"

typedef struct {
    const esp_partition_t *partition;
    const uint8_t *mapped;
    esp_partition_mmap_handle_t mmap_handle;
} custom_partition_t;

custom_store_result_t custom_partition_open(custom_partition_t *owner,
                                            custom_store_backend_t *backend);
