#pragma once

#include "navigation/navigation.h"
#include "services/display/display_activity.h"
#include "services/display/display_render_policy.h"
#include "settings/settings_page.h"

typedef enum {
    APP_ACTION_NONE = 0,
    APP_ACTION_WAKE_DISPLAY = 1u << 0,
    APP_ACTION_RENDER = 1u << 1,
    APP_ACTION_HOTSPOT_EXIT = 1u << 2,
    APP_ACTION_NAVIGATION = 1u << 3,
    APP_ACTION_WAKE_GESTURE_CONSUMED = 1u << 4,
} app_action_t;

typedef struct {
    navigation_t *navigation;
    settings_page_t *settings;
    display_activity_t *display;
    display_render_policy_t *render;
    int battery_soc;
    uint32_t badge_sequence;
    bool badge_sequence_known;
} app_controller_t;

void app_controller_init(app_controller_t *controller, navigation_t *navigation,
                         settings_page_t *settings, display_activity_t *display,
                         display_render_policy_t *render);
app_action_t app_controller_button_down(app_controller_t *controller,
                                        uint8_t key_mask, int64_t now_us);
void app_controller_button_up(app_controller_t *controller, uint8_t key_mask,
                              int64_t now_us);
app_action_t app_controller_gesture(app_controller_t *controller, mode_key_t key,
                                    uint8_t key_mask, bool long_press,
                                    int64_t now_us);
app_action_t app_controller_wifi_changed(app_controller_t *controller);
app_action_t app_controller_profile_updated(app_controller_t *controller,
                                            uint32_t sequence);
app_action_t app_controller_battery_result(app_controller_t *controller, int soc);
