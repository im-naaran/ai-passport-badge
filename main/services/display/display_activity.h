#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool screen_on;
    bool wake_guard;
    uint8_t pressed_mask;
    uint8_t consume_gesture_mask;
    uint8_t timeout_minutes;
    int64_t last_activity_us;
} display_activity_t;

void display_activity_init(display_activity_t *, int64_t now_us, uint8_t timeout_minutes);
void display_activity_set_timeout(display_activity_t *, uint8_t timeout_minutes, int64_t now_us);
bool display_activity_down(display_activity_t *, uint8_t key_mask, int64_t now_us);
bool display_activity_up(display_activity_t *, uint8_t key_mask, int64_t now_us);
bool display_activity_consume_gesture(display_activity_t *, uint8_t key_mask);
bool display_activity_business_allowed(const display_activity_t *);
bool display_activity_should_sleep(const display_activity_t *, int64_t now_us);
void display_activity_mark_rendered(display_activity_t *, int64_t now_us);
void display_activity_mark_slept(display_activity_t *);
