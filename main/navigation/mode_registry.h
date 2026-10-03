#pragma once

#include <stdbool.h>
#include "navigation.h"

enum {
    MODE_REGISTRY_CUSTOM_COUNT = 3,
    MODE_REGISTRY_BUSINESS_COUNT = 1 + MODE_REGISTRY_CUSTOM_COUNT,
};

typedef struct {
    mode_t business[MODE_REGISTRY_BUSINESS_COUNT];
    mode_t system_settings;
} mode_registry_t;

// Fixed array positions define the user-visible order; stable IDs define settings return targets.
bool mode_registry_init(mode_registry_t *registry, mode_t badge,
                        const mode_t custom[MODE_REGISTRY_CUSTOM_COUNT],
                        mode_t settings);
