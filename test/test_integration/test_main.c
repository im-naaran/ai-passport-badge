#include <unity.h>

#include "app_controller.h"
#include "services/badge/badge_store.h"
#include "services/config/settings_store.h"

static unsigned hotspot_stops;

static settings_result_t read_failed(void *context, uint8_t *data, size_t *length) {
    (void)context;
    (void)data;
    (void)length;
    return SETTINGS_IO_ERROR;
}

static void stop_hotspot(void *context) {
    (void)context;
    ++hotspot_stops;
}

static mode_t plain_mode(uint32_t id, const char *name) {
    return (mode_t){.id = id, .name = name};
}

typedef struct {
    settings_store_t store;
    settings_page_t settings;
    mode_t business[2];
    mode_t settings_mode;
    navigation_t navigation;
    display_activity_t display;
    display_render_policy_t render;
    app_controller_t controller;
} fixture_t;

static void fixture_init(fixture_t *fixture) {
    fixture->store = (settings_store_t){
        .value = {20, 10, 3, 60},
    };
    fixture->settings = (settings_page_t){
        .config = &fixture->store,
        .stop_hotspot = stop_hotspot,
    };
    fixture->business[0] = plain_mode(4, "工牌");
    fixture->business[1] = plain_mode(2, "个性化");
    fixture->settings_mode = settings_page_descriptor(&fixture->settings);
    TEST_ASSERT_TRUE(navigation_init(&fixture->navigation, fixture->business, 2,
                                     &fixture->settings_mode));
    display_activity_init(&fixture->display, 0, 3);
    display_render_policy_init(&fixture->render, false);
    app_controller_init(&fixture->controller, &fixture->navigation,
                        &fixture->settings, &fixture->display, &fixture->render);
}

void setUp(void) { hotspot_stops = 0; }
void tearDown(void) {}

static void test_storage_failures_keep_safe_defaults(void) {
    settings_store_t settings;
    TEST_ASSERT_EQUAL(SETTINGS_IO_ERROR,
        settings_store_init(&settings, (settings_backend_t){.read = read_failed}));
    TEST_ASSERT_EQUAL(3, settings.value.screen_off_minutes);
    TEST_ASSERT_EQUAL(60, settings.value.brightness_percent);

    badge_store_t badge;
    static const uint8_t default_pixel[] = {0, 0};
    badge_profile_snapshot_t defaults = {
        .name = "AI Passport",
        .image = default_pixel,
        .width = BADGE_IMAGE_WIDTH,
        .height = BADGE_IMAGE_HEIGHT,
        .stride = BADGE_IMAGE_STRIDE,
        .image_format = BADGE_IMAGE_FORMAT_RGB565_LE,
        .is_default = true,
        .bio = "我的 AI 身份",
        .shape = BADGE_PHOTO_SHAPE_SQUARE,
    };
    TEST_ASSERT_EQUAL(BADGE_STORE_DEFAULTED,
                      badge_store_init_defaults(&badge, defaults));
    TEST_ASSERT_TRUE(badge_store_snapshot(&badge)->is_default);
    TEST_ASSERT_EQUAL_STRING("AI Passport", badge_store_snapshot(&badge)->name);
}

static void test_wake_gesture_renders_before_business_input(void) {
    fixture_t fixture;
    fixture_init(&fixture);
    display_activity_mark_slept(&fixture.display);
    app_action_t down = app_controller_button_down(&fixture.controller, 1u, 10);
    TEST_ASSERT_BITS_HIGH(APP_ACTION_WAKE_DISPLAY | APP_ACTION_RENDER, down);
    TEST_ASSERT_TRUE(display_render_policy_should_render(&fixture.render, false, true));

    display_render_policy_mark_rendered(&fixture.render);
    display_activity_mark_rendered(&fixture.display, 11);
    app_controller_button_up(&fixture.controller, 1u, 12);
    app_action_t gesture = app_controller_gesture(&fixture.controller,
                                                   MODE_KEY_UP, 1u, true, 13);
    TEST_ASSERT_EQUAL(APP_ACTION_WAKE_GESTURE_CONSUMED, gesture);
    TEST_ASSERT_EQUAL(4, navigation_active(&fixture.navigation)->id);
}

static void test_hotspot_page_consumes_long_key_before_navigation(void) {
    fixture_t fixture;
    fixture_init(&fixture);
    app_controller_gesture(&fixture.controller, MODE_KEY_OK, 4u, true, 1);
    TEST_ASSERT_TRUE(fixture.navigation.settings_active);
    fixture.settings.page = SETTINGS_WIFI_CONFIG;
    display_render_policy_mark_rendered(&fixture.render);

    app_action_t action = app_controller_gesture(&fixture.controller,
                                                  MODE_KEY_DOWN, 2u, true, 2);
    TEST_ASSERT_BITS_HIGH(APP_ACTION_HOTSPOT_EXIT | APP_ACTION_RENDER, action);
    TEST_ASSERT_TRUE(fixture.navigation.settings_active);
    TEST_ASSERT_EQUAL(SETTINGS_LIST, fixture.settings.page);
    TEST_ASSERT_EQUAL(1, hotspot_stops);
}

static void test_profile_wifi_and_settings_battery_events_coalesce_rendering(void) {
    fixture_t fixture;
    fixture_init(&fixture);
    TEST_ASSERT_BITS_HIGH(APP_ACTION_RENDER,
        app_controller_profile_updated(&fixture.controller, 1));
    TEST_ASSERT_EQUAL(APP_ACTION_NONE,
        app_controller_profile_updated(&fixture.controller, 1));
    display_render_policy_mark_rendered(&fixture.render);
    TEST_ASSERT_BITS_HIGH(APP_ACTION_RENDER,
        app_controller_profile_updated(&fixture.controller, 2));
    display_render_policy_mark_rendered(&fixture.render);

    TEST_ASSERT_EQUAL(APP_ACTION_NONE,
        app_controller_battery_result(&fixture.controller, 75));
    TEST_ASSERT_EQUAL(75, fixture.settings.battery_soc);
    TEST_ASSERT_EQUAL(APP_ACTION_NONE,
        app_controller_battery_result(&fixture.controller, 75));
    TEST_ASSERT_EQUAL(APP_ACTION_NONE,
        app_controller_battery_result(&fixture.controller, -1));
    display_render_policy_mark_rendered(&fixture.render);

    app_controller_gesture(&fixture.controller, MODE_KEY_OK, 4u, true, 3);
    display_render_policy_mark_rendered(&fixture.render);
    TEST_ASSERT_BITS_HIGH(APP_ACTION_RENDER,
        app_controller_battery_result(&fixture.controller, 80));
    TEST_ASSERT_EQUAL(80, fixture.settings.battery_soc);
    display_render_policy_mark_rendered(&fixture.render);
    fixture.settings.page = SETTINGS_EDIT_BRIGHTNESS;
    TEST_ASSERT_EQUAL(APP_ACTION_NONE,
        app_controller_battery_result(&fixture.controller, 81));
    TEST_ASSERT_EQUAL(81, fixture.settings.battery_soc);

    TEST_ASSERT_BITS_HIGH(APP_ACTION_RENDER,
        app_controller_wifi_changed(&fixture.controller));
    display_render_policy_mark_rendered(&fixture.render);
    TEST_ASSERT_BITS_HIGH(APP_ACTION_RENDER,
        app_controller_personalization_updated(&fixture.controller));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_storage_failures_keep_safe_defaults);
    RUN_TEST(test_wake_gesture_renders_before_business_input);
    RUN_TEST(test_hotspot_page_consumes_long_key_before_navigation);
    RUN_TEST(test_profile_wifi_and_settings_battery_events_coalesce_rendering);
    return UNITY_END();
}
