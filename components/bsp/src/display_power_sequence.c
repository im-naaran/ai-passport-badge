#include "display_power_sequence.h"

esp_err_t display_power_apply(display_power_state_t *state, bool suspend,
                              display_power_step_fn apply, void *context) {
    if (state->suspended == suspend) return ESP_OK;
    static const display_power_step_t suspend_steps[] = {
        DISPLAY_POWER_STEP_DISPLAY_OFF,
        DISPLAY_POWER_STEP_WAIT_BEFORE_SLEEP,
        DISPLAY_POWER_STEP_SLEEP_IN,
        DISPLAY_POWER_STEP_WAIT_SLEEP,
    };
    static const display_power_step_t resume_steps[] = {
        DISPLAY_POWER_STEP_SLEEP_OUT,
        DISPLAY_POWER_STEP_WAIT_WAKE,
        DISPLAY_POWER_STEP_DISPLAY_ON,
    };
    const display_power_step_t *steps = suspend ? suspend_steps : resume_steps;
    size_t count = suspend ? sizeof(suspend_steps) / sizeof(suspend_steps[0])
                           : sizeof(resume_steps) / sizeof(resume_steps[0]);
    for (size_t i = 0; i < count; ++i) {
        esp_err_t err = apply(context, steps[i]);
        if (err != ESP_OK) return err;
    }
    // Commit only after every panel command and mandatory delay completed, so failures stay retryable.
    state->suspended = suspend;
    return ESP_OK;
}
