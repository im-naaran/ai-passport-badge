#include "battery_schedule.h"
#include <limits.h>

#define BATTERY_LIT_PERIOD_US 30000000LL
#define BATTERY_DARK_PERIOD_US 300000000LL
#define BATTERY_RETRY_PERIOD_US 1000000LL

static int64_t add_period(int64_t now, int64_t period) {
    return now > INT64_MAX - period ? INT64_MAX : now + period;
}

void battery_schedule_init(battery_schedule_t *s, int64_t now_us, bool screen_on) {
    *s = (battery_schedule_t){.screen_on = screen_on, .next_sample_us = now_us};
}

void battery_schedule_set_screen(battery_schedule_t *s, bool screen_on, int64_t now_us) {
    if (s->screen_on == screen_on) return;
    s->screen_on = screen_on;
    // A wake is an explicit immediate sample; entering dark mode only changes the cadence.
    s->next_sample_us = screen_on ? now_us : add_period(now_us, BATTERY_DARK_PERIOD_US);
}

void battery_schedule_request_sample(battery_schedule_t *s, int64_t now_us) {
    if (now_us < s->next_sample_us) s->next_sample_us = now_us;
}

bool battery_schedule_due(const battery_schedule_t *s, int64_t now_us) {
    return now_us >= s->next_sample_us;
}

void battery_schedule_sampled(battery_schedule_t *s, int64_t now_us, bool success) {
    int64_t period = success ? (s->screen_on ? BATTERY_LIT_PERIOD_US : BATTERY_DARK_PERIOD_US)
                             : BATTERY_RETRY_PERIOD_US;
    s->next_sample_us = add_period(now_us, period);
}
