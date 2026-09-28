#include <unity.h>

#include "button_gesture.h"
#include "display_lifecycle.h"
#include "display_power_sequence.h"
#include "display_activity.h"
#include "display_render_policy.h"

void setUp(void) {}
void tearDown(void) {}

typedef struct {
    int steps[16];
    size_t count;
    int fail_step;
} display_steps_t;

static esp_err_t record_power_step(void *context, display_power_step_t step) {
    display_steps_t *record = context;
    record->steps[record->count++] = step;
    return record->fail_step == (int)step ? ESP_FAIL : ESP_OK;
}

static esp_err_t record_lifecycle_step(void *context, display_lifecycle_step_t step) {
    display_steps_t *record = context;
    record->steps[record->count++] = step;
    return record->fail_step == (int)step ? ESP_FAIL : ESP_OK;
}

static void test_gesture_thresholds_and_release(void) {
    button_gesture_t state = {0};
    TEST_ASSERT_EQUAL(BUTTON_GESTURE_NONE,
                      button_gesture_update(&state, BUTTON_EDGE_UP, 0));
    TEST_ASSERT_EQUAL(BUTTON_GESTURE_NONE,
                      button_gesture_update(&state, BUTTON_EDGE_DOWN, 100));
    TEST_ASSERT_EQUAL(BUTTON_GESTURE_CLICK,
                      button_gesture_update(&state, BUTTON_EDGE_UP, 150));

    TEST_ASSERT_EQUAL(BUTTON_GESTURE_NONE,
                      button_gesture_update(&state, BUTTON_EDGE_DOWN, 200));
    TEST_ASSERT_EQUAL(BUTTON_GESTURE_NONE,
                      button_gesture_update(&state, BUTTON_EDGE_TICK, 999));
    TEST_ASSERT_EQUAL(BUTTON_GESTURE_LONG,
                      button_gesture_update(&state, BUTTON_EDGE_TICK, 1000));
    TEST_ASSERT_EQUAL(BUTTON_GESTURE_NONE,
                      button_gesture_update(&state, BUTTON_EDGE_UP, 1001));
}

static void test_pressed_mask_starts_and_stops_shared_timer(void) {
    uint8_t mask = 0;
    TEST_ASSERT_EQUAL(BUTTON_TIMER_START,
                      button_pressed_mask_update(&mask, 1u, true));
    TEST_ASSERT_EQUAL(BUTTON_TIMER_UNCHANGED,
                      button_pressed_mask_update(&mask, 2u, true));
    TEST_ASSERT_EQUAL(BUTTON_TIMER_UNCHANGED,
                      button_pressed_mask_update(&mask, 1u, false));
    TEST_ASSERT_EQUAL(BUTTON_TIMER_STOP,
                      button_pressed_mask_update(&mask, 2u, false));
}

static void test_display_power_sequence_is_ordered_and_retryable(void) {
    display_power_state_t state = {0};
    display_steps_t record = {.fail_step = -1};
    const int suspend[] = {
        DISPLAY_POWER_STEP_DISPLAY_OFF,
        DISPLAY_POWER_STEP_WAIT_BEFORE_SLEEP,
        DISPLAY_POWER_STEP_SLEEP_IN,
        DISPLAY_POWER_STEP_WAIT_SLEEP,
    };
    TEST_ASSERT_EQUAL(ESP_OK,
                      display_power_apply(&state, true, record_power_step, &record));
    TEST_ASSERT_TRUE(state.suspended);
    TEST_ASSERT_EQUAL_INT_ARRAY(suspend, record.steps, 4);
    TEST_ASSERT_EQUAL(ESP_OK,
                      display_power_apply(&state, true, record_power_step, &record));
    TEST_ASSERT_EQUAL(4, record.count);

    state.suspended = false;
    record = (display_steps_t){.fail_step = DISPLAY_POWER_STEP_SLEEP_IN};
    TEST_ASSERT_EQUAL(ESP_FAIL,
                      display_power_apply(&state, true, record_power_step, &record));
    TEST_ASSERT_FALSE(state.suspended);
}

static void test_display_lifecycle_unlocks_and_retains_retry_state(void) {
    display_lifecycle_t state = {0};
    display_steps_t record = {.fail_step = -1};
    const int suspend[] = {
        DISPLAY_LIFECYCLE_LOCK,
        DISPLAY_LIFECYCLE_PORT_STOP,
        DISPLAY_LIFECYCLE_PANEL_SUSPEND,
        DISPLAY_LIFECYCLE_UNLOCK,
    };
    TEST_ASSERT_EQUAL(ESP_OK,
                      display_lifecycle_suspend(&state, record_lifecycle_step, &record));
    TEST_ASSERT_TRUE(state.suspended);
    TEST_ASSERT_TRUE(state.port_stopped);
    TEST_ASSERT_EQUAL_INT_ARRAY(suspend, record.steps, 4);

    record = (display_steps_t){.fail_step = DISPLAY_LIFECYCLE_PORT_RESUME};
    TEST_ASSERT_EQUAL(ESP_FAIL,
                      display_lifecycle_resume(&state, record_lifecycle_step, &record));
    TEST_ASSERT_TRUE(state.suspended);
    TEST_ASSERT_TRUE(state.port_stopped);

    record = (display_steps_t){.fail_step = -1};
    TEST_ASSERT_EQUAL(ESP_OK,
                      display_lifecycle_resume(&state, record_lifecycle_step, &record));
    TEST_ASSERT_FALSE(state.suspended);
    TEST_ASSERT_FALSE(state.port_stopped);
}

static void test_display_activity_sleeps_and_consumes_wake_gesture(void) {
    display_activity_t activity;
    display_activity_init(&activity, 0, 1);
    TEST_ASSERT_FALSE(display_activity_should_sleep(&activity, 59999999));
    TEST_ASSERT_TRUE(display_activity_should_sleep(&activity, 60000000));
    activity.screen_on = false;
    TEST_ASSERT_TRUE(display_activity_down(&activity, 1u, 70000000));
    TEST_ASSERT_FALSE(display_activity_business_allowed(&activity));
    TEST_ASSERT_TRUE(display_activity_consume_gesture(&activity, 1u));
    TEST_ASSERT_FALSE(display_activity_consume_gesture(&activity, 1u));
    display_activity_mark_rendered(&activity, 70000001);
    TEST_ASSERT_TRUE(display_activity_up(&activity, 1u, 70000002));
    TEST_ASSERT_TRUE(display_activity_business_allowed(&activity));
}

static void test_render_policy_coalesces_requests_and_waits_while_dark(void) {
    display_render_policy_t policy;
    display_render_policy_init(&policy, false);
    TEST_ASSERT_FALSE(display_render_policy_should_render(&policy, true, false));
    display_render_policy_request(&policy);
    display_render_policy_request(&policy);
    TEST_ASSERT_FALSE(display_render_policy_should_render(&policy, false, false));
    TEST_ASSERT_TRUE(display_render_policy_should_render(&policy, false, true));
    display_render_policy_mark_rendered(&policy);
    TEST_ASSERT_FALSE(display_render_policy_should_render(&policy, true, false));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_gesture_thresholds_and_release);
    RUN_TEST(test_pressed_mask_starts_and_stops_shared_timer);
    RUN_TEST(test_display_power_sequence_is_ordered_and_retryable);
    RUN_TEST(test_display_lifecycle_unlocks_and_retains_retry_state);
    RUN_TEST(test_display_activity_sleeps_and_consumes_wake_gesture);
    RUN_TEST(test_render_policy_coalesces_requests_and_waits_while_dark);
    return UNITY_END();
}
