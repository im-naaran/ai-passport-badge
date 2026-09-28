#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    DISPLAY_LIFECYCLE_LOCK,
    DISPLAY_LIFECYCLE_UNLOCK,
    DISPLAY_LIFECYCLE_PORT_STOP,
    DISPLAY_LIFECYCLE_PANEL_SUSPEND,
    DISPLAY_LIFECYCLE_PANEL_RESUME,
    DISPLAY_LIFECYCLE_PORT_RESUME,
    DISPLAY_LIFECYCLE_INVALIDATE,
} display_lifecycle_step_t;

typedef esp_err_t (*display_lifecycle_step_fn)(void *context, display_lifecycle_step_t step);

typedef struct {
    bool suspended;
    bool port_stopped;
} display_lifecycle_t;

esp_err_t display_lifecycle_suspend(display_lifecycle_t *, display_lifecycle_step_fn, void *context);
esp_err_t display_lifecycle_resume(display_lifecycle_t *, display_lifecycle_step_fn, void *context);
