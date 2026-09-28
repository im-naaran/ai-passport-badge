#include "display_lifecycle.h"

static esp_err_t finish_locked(display_lifecycle_step_fn apply, void *context, esp_err_t result) {
    esp_err_t unlock = apply(context, DISPLAY_LIFECYCLE_UNLOCK);
    return result != ESP_OK ? result : unlock;
}

esp_err_t display_lifecycle_suspend(display_lifecycle_t *state,
                                    display_lifecycle_step_fn apply, void *context) {
    if (state->suspended) return ESP_OK;
    esp_err_t err = apply(context, DISPLAY_LIFECYCLE_LOCK);
    if (err != ESP_OK) return err;
    if (!state->port_stopped) {
        err = apply(context, DISPLAY_LIFECYCLE_PORT_STOP);
        if (err != ESP_OK) return finish_locked(apply, context, err);
        state->port_stopped = true;
    }
    err = apply(context, DISPLAY_LIFECYCLE_PANEL_SUSPEND);
    if (err != ESP_OK) {
        // Roll the timer back when possible; if that also fails, retain port_stopped for the next retry.
        if (apply(context, DISPLAY_LIFECYCLE_PORT_RESUME) == ESP_OK) state->port_stopped = false;
        return finish_locked(apply, context, err);
    }
    state->suspended = true;
    return finish_locked(apply, context, ESP_OK);
}

esp_err_t display_lifecycle_resume(display_lifecycle_t *state,
                                   display_lifecycle_step_fn apply, void *context) {
    if (!state->suspended) return ESP_OK;
    esp_err_t err = apply(context, DISPLAY_LIFECYCLE_LOCK);
    if (err != ESP_OK) return err;
    err = apply(context, DISPLAY_LIFECYCLE_PANEL_RESUME);
    if (err != ESP_OK) return finish_locked(apply, context, err);
    if (state->port_stopped) {
        err = apply(context, DISPLAY_LIFECYCLE_PORT_RESUME);
        if (err != ESP_OK) return finish_locked(apply, context, err);
        state->port_stopped = false;
    }
    err = apply(context, DISPLAY_LIFECYCLE_INVALIDATE);
    if (err != ESP_OK) return finish_locked(apply, context, err);
    state->suspended = false;
    return finish_locked(apply, context, ESP_OK);
}
