#include "app_controller.h"

static app_action_t request_render(app_controller_t *controller, app_action_t action) {
    display_render_policy_request(controller->render);
    return action | APP_ACTION_RENDER;
}

void app_controller_init(app_controller_t *controller, navigation_t *navigation,
                         settings_page_t *settings, display_activity_t *display,
                         display_render_policy_t *render) {
    *controller = (app_controller_t){
        .navigation = navigation,
        .settings = settings,
        .display = display,
        .render = render,
        .battery_soc = -1,
    };
    settings_page_set_battery(settings, -1);
}

app_action_t app_controller_button_down(app_controller_t *controller,
                                        uint8_t key_mask, int64_t now_us) {
    if (!controller || !controller->display || !controller->render)
        return APP_ACTION_NONE;
    if (!display_activity_down(controller->display, key_mask, now_us))
        return APP_ACTION_NONE;
    // Wake restores the panel first; the application renders a synchronous frame
    // before it marks the display active and restores the persisted backlight.
    return request_render(controller, APP_ACTION_WAKE_DISPLAY);
}

void app_controller_button_up(app_controller_t *controller, uint8_t key_mask,
                              int64_t now_us) {
    if (controller && controller->display)
        display_activity_up(controller->display, key_mask, now_us);
}

app_action_t app_controller_gesture(app_controller_t *controller, mode_key_t key,
                                    uint8_t key_mask, bool long_press,
                                    int64_t now_us) {
    if (!controller || !controller->navigation || !controller->settings ||
        !controller->display || !controller->render)
        return APP_ACTION_NONE;
    if (display_activity_consume_gesture(controller->display, key_mask) ||
        !display_activity_business_allowed(controller->display))
        return APP_ACTION_WAKE_GESTURE_CONSUMED;
    if (settings_page_hotspot_active(controller->settings)) {
        const mode_t *active = navigation_active(controller->navigation);
        if (active && active->handle_key)
            active->handle_key(active->context, key, now_us);
        return request_render(controller, APP_ACTION_HOTSPOT_EXIT);
    }
    navigation_key(controller->navigation, key, long_press, now_us);
    return request_render(controller, APP_ACTION_NAVIGATION);
}

app_action_t app_controller_wifi_changed(app_controller_t *controller) {
    if (!controller || !controller->render) return APP_ACTION_NONE;
    return request_render(controller, APP_ACTION_NONE);
}

app_action_t app_controller_profile_updated(app_controller_t *controller,
                                            uint32_t sequence) {
    if (!controller || !controller->render) return APP_ACTION_NONE;
    if (controller->badge_sequence_known && controller->badge_sequence == sequence)
        return APP_ACTION_NONE;
    controller->badge_sequence = sequence;
    controller->badge_sequence_known = true;
    return request_render(controller, APP_ACTION_NONE);
}

app_action_t app_controller_personalization_updated(app_controller_t *controller) {
    if (!controller || !controller->render) return APP_ACTION_NONE;
    // HTTP only publishes an event; the application loop owns the later LVGL render.
    return request_render(controller, APP_ACTION_NONE);
}

app_action_t app_controller_battery_result(app_controller_t *controller, int soc) {
    if (!controller || !controller->render || !controller->settings ||
        soc < 0 || soc > 100 ||
        controller->battery_soc == soc)
        return APP_ACTION_NONE;
    controller->battery_soc = soc;
    settings_page_set_battery(controller->settings, soc);
    const mode_t *active = controller->navigation ?
        navigation_active(controller->navigation) : NULL;
    // Business pages retain the latest snapshot without redrawing a hidden battery label.
    if (!controller->navigation || !controller->navigation->settings_active ||
        !active || active->context != controller->settings ||
        controller->settings->page != SETTINGS_LIST)
        return APP_ACTION_NONE;
    return request_render(controller, APP_ACTION_NONE);
}
