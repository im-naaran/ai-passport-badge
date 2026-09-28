#include "mode_registry.h"

bool mode_registry_init(mode_registry_t *registry, mode_t badge, mode_t custom,
                        mode_t settings) {
    if (!registry || badge.id == custom.id || badge.id == settings.id ||
        custom.id == settings.id) {
        return false;
    }
    registry->business[0] = badge;
    registry->business[1] = custom;
    registry->system_settings = settings;
    return true;
}
