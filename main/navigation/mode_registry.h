#pragma once

#include <stdbool.h>
#include "navigation.h"

typedef struct {
    mode_t business[2];
    mode_t system_settings;
} mode_registry_t;

// Mode IDs remain stable even though the registry owns their array positions.
bool mode_registry_init(mode_registry_t *registry, mode_t badge, mode_t custom,
                        mode_t settings);
