#include "display_activity.h"

void display_activity_init(display_activity_t *s, int64_t now, uint8_t timeout) {
    *s = (display_activity_t){.screen_on = true, .timeout_minutes = timeout, .last_activity_us = now};
}
void display_activity_set_timeout(display_activity_t *s, uint8_t timeout, int64_t now) {
    s->timeout_minutes = timeout; s->last_activity_us = now;
}
bool display_activity_down(display_activity_t *s, uint8_t mask, int64_t now) {
    bool was_off = !s->screen_on;
    s->pressed_mask |= mask; s->last_activity_us = now;
    if (was_off) {
        s->wake_guard = true;
        s->consume_gesture_mask |= mask;
    }
    return was_off;
}
bool display_activity_up(display_activity_t *s, uint8_t mask, int64_t now) {
    s->pressed_mask &= (uint8_t)~mask; s->last_activity_us = now;
    if (!s->pressed_mask) s->wake_guard = false;
    return !s->wake_guard;
}
bool display_activity_consume_gesture(display_activity_t *s, uint8_t mask) {
    if (!(s->consume_gesture_mask & mask)) return false;
    s->consume_gesture_mask &= (uint8_t)~mask;
    return true;
}
bool display_activity_business_allowed(const display_activity_t *s) { return s->screen_on && !s->wake_guard; }
bool display_activity_should_sleep(const display_activity_t *s, int64_t now) {
    return s->screen_on && !s->pressed_mask && s->timeout_minutes && now - s->last_activity_us >= (int64_t)s->timeout_minutes * 60000000;
}
void display_activity_mark_rendered(display_activity_t *s, int64_t now) { s->screen_on = true; s->last_activity_us = now; }
void display_activity_mark_slept(display_activity_t *s) { s->screen_on = false; }
