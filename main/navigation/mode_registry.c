#include "mode_registry.h"

bool mode_registry_init(mode_registry_t *registry, mode_t badge,
                        const mode_t custom[MODE_REGISTRY_CUSTOM_COUNT],
                        mode_t settings) {
    if (!registry || !custom) return false;
    registry->business[0] = badge;
    for (size_t i = 0; i < MODE_REGISTRY_CUSTOM_COUNT; ++i)
        registry->business[i + 1] = custom[i];
    for (size_t i = 0; i < MODE_REGISTRY_BUSINESS_COUNT; ++i) {
        if (registry->business[i].id == settings.id) return false;
        for (size_t j = 0; j < i; ++j)
            if (registry->business[i].id == registry->business[j].id) return false;
    }
    registry->system_settings = settings;
    return true;
}
