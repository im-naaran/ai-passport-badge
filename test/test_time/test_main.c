#include <limits.h>
#include <unity.h>

#include "battery_schedule.h"

void setUp(void) {}
void tearDown(void) {}

static void test_lit_and_dark_sampling_cadence(void) {
    battery_schedule_t schedule;
    battery_schedule_init(&schedule, 10, true);
    TEST_ASSERT_TRUE(battery_schedule_due(&schedule, 10));
    battery_schedule_sampled(&schedule, 10, true);
    TEST_ASSERT_EQUAL_INT64(30000010, schedule.next_sample_us);
    battery_schedule_set_screen(&schedule, false, 20);
    TEST_ASSERT_EQUAL_INT64(300000020, schedule.next_sample_us);
    TEST_ASSERT_FALSE(battery_schedule_due(&schedule, 300000019));
    TEST_ASSERT_TRUE(battery_schedule_due(&schedule, 300000020));
    battery_schedule_set_screen(&schedule, true, 30);
    TEST_ASSERT_EQUAL_INT64(30, schedule.next_sample_us);
}

static void test_failure_retries_and_requests_only_move_deadline_earlier(void) {
    battery_schedule_t schedule;
    battery_schedule_init(&schedule, 100, true);
    battery_schedule_sampled(&schedule, 100, false);
    TEST_ASSERT_EQUAL_INT64(1000100, schedule.next_sample_us);
    battery_schedule_request_sample(&schedule, 50);
    TEST_ASSERT_EQUAL_INT64(50, schedule.next_sample_us);
    battery_schedule_request_sample(&schedule, 80);
    TEST_ASSERT_EQUAL_INT64(50, schedule.next_sample_us);
}

static void test_deadline_saturates_on_overflow(void) {
    battery_schedule_t schedule;
    battery_schedule_init(&schedule, INT64_MAX - 5, true);
    battery_schedule_sampled(&schedule, INT64_MAX - 5, true);
    TEST_ASSERT_EQUAL_INT64(INT64_MAX, schedule.next_sample_us);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_lit_and_dark_sampling_cadence);
    RUN_TEST(test_failure_retries_and_requests_only_move_deadline_earlier);
    RUN_TEST(test_deadline_saturates_on_overflow);
    return UNITY_END();
}
