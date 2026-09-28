#include "display_render_policy.h"

void display_render_policy_init(display_render_policy_t *policy, bool pending) {
    policy->pending = pending;
}

void display_render_policy_request(display_render_policy_t *policy) {
    policy->pending = true;
}

bool display_render_policy_should_render(const display_render_policy_t *policy, bool screen_on,
                                         bool wake_frame_pending) {
    // Background mutations stay dirty while dark; only an explicit wake frame may render before screen_on flips.
    return policy->pending && (screen_on || wake_frame_pending);
}

void display_render_policy_mark_rendered(display_render_policy_t *policy) {
    policy->pending = false;
}
