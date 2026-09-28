#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    DISPLAY_POWER_STEP_DISPLAY_OFF,
    DISPLAY_POWER_STEP_WAIT_BEFORE_SLEEP,
    DISPLAY_POWER_STEP_SLEEP_IN,
    DISPLAY_POWER_STEP_WAIT_SLEEP,
    DISPLAY_POWER_STEP_SLEEP_OUT,
    DISPLAY_POWER_STEP_WAIT_WAKE,
    DISPLAY_POWER_STEP_DISPLAY_ON,
} display_power_step_t;

typedef esp_err_t (*display_power_step_fn)(void *context, display_power_step_t step);

typedef struct {
    bool suspended;
} display_power_state_t;

esp_err_t display_power_apply(display_power_state_t *, bool suspend,
                              display_power_step_fn, void *context);
