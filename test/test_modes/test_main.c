#include <unity.h>

#include "modes/badge/badge_mode.h"
#include "modes/custom/custom_mode.h"
#include "navigation/mode_registry.h"

typedef struct {
    unsigned inits;
    unsigned enters;
    unsigned leaves;
    unsigned keys;
    unsigned renders;
    bool active;
    bool available;
} fake_mode_t;

static void fake_init(void *context) { ++((fake_mode_t *)context)->inits; }
static void fake_enter(void *context) {
    fake_mode_t *state = context;
    TEST_ASSERT_FALSE(state->active);
    state->active = true;
    ++state->enters;
}
static void fake_leave(void *context) {
    fake_mode_t *state = context;
    TEST_ASSERT_TRUE(state->active);
    state->active = false;
    ++state->leaves;
}
static mode_action_t fake_key(void *context, mode_key_t key, int64_t now_us) {
    (void)key;
    (void)now_us;
    ++((fake_mode_t *)context)->keys;
    return MODE_RETURN;
}
static void fake_render(void *context) { ++((fake_mode_t *)context)->renders; }
static bool fake_available(void *context) { return ((fake_mode_t *)context)->available; }
static mode_t fake_descriptor(uint32_t id, fake_mode_t *state) {
    return (mode_t){.id = id, .name = "fake", .context = state, .init = fake_init,
        .enter = fake_enter, .leave = fake_leave, .handle_key = fake_key,
        .render = fake_render};
}

void setUp(void) {}
void tearDown(void) {}

static void test_registry_contains_exactly_two_business_pages(void) {
    fake_mode_t badge_state = {0}, settings_state = {0};
    mode_registry_t registry;
    custom_mode_t custom_state;
    mode_t custom = custom_mode_descriptor(&custom_state);
    TEST_ASSERT_TRUE(mode_registry_init(&registry,
        fake_descriptor(4, &badge_state), custom,
        fake_descriptor(100, &settings_state)));
    TEST_ASSERT_EQUAL(2, sizeof(registry.business) / sizeof(registry.business[0]));
    TEST_ASSERT_EQUAL(4, registry.business[0].id);
    TEST_ASSERT_EQUAL(2, registry.business[1].id);
    TEST_ASSERT_EQUAL_STRING("个性化", registry.business[1].name);
    TEST_ASSERT_NULL(registry.business[1].handle_key);
    TEST_ASSERT_FALSE(registry.business[1].is_available(registry.business[1].context));
    custom_mode_set_content_available(&custom_state, true);
    TEST_ASSERT_TRUE(registry.business[1].is_available(registry.business[1].context));
}

static void test_badge_mode_exposes_default_and_user_snapshots(void) {
    uint8_t image = 0;
    badge_profile_snapshot_t defaults = {.name = "AI Passport", .image = &image,
        .width = BADGE_IMAGE_WIDTH, .height = BADGE_IMAGE_HEIGHT,
        .stride = BADGE_IMAGE_STRIDE, .image_format = BADGE_IMAGE_FORMAT_RGB565_LE,
        .sequence = 0, .is_default = true, .bio = "我的 AI 身份"};
    badge_store_t store;
    TEST_ASSERT_EQUAL(BADGE_STORE_DEFAULTED,
                      badge_store_init_defaults(&store, defaults));
    badge_mode_t badge;
    mode_t descriptor = badge_mode_descriptor(&badge, &store);
    TEST_ASSERT_EQUAL(4, descriptor.id);
    TEST_ASSERT_EQUAL_STRING("工牌", descriptor.name);
    TEST_ASSERT_TRUE(badge_mode_snapshot(&badge)->is_default);
    TEST_ASSERT_EQUAL_STRING("AI Passport", badge_mode_snapshot(&badge)->name);
    TEST_ASSERT_EQUAL_STRING("我的 AI 身份", badge_mode_snapshot(&badge)->bio);

    store.snapshot = (badge_profile_snapshot_t){.name = "张三", .image = &image,
        .width = BADGE_IMAGE_WIDTH, .height = BADGE_IMAGE_HEIGHT,
        .stride = BADGE_IMAGE_STRIDE, .image_format = BADGE_IMAGE_FORMAT_RGB565_LE,
        .sequence = 9, .is_default = false, .bio = "保持好奇"};
    TEST_ASSERT_FALSE(badge_mode_snapshot(&badge)->is_default);
    TEST_ASSERT_EQUAL_STRING("张三", badge_mode_snapshot(&badge)->name);
    TEST_ASSERT_EQUAL(9, badge_mode_snapshot(&badge)->sequence);
    TEST_ASSERT_EQUAL_STRING("保持好奇", badge_mode_snapshot(&badge)->bio);
}

static void test_unavailable_mode_is_skipped_without_lifecycle_churn(void) {
    fake_mode_t state[3] = {0};
    state[1].available = false;
    mode_t business[] = {fake_descriptor(4, &state[0]), fake_descriptor(2, &state[1])};
    business[1].is_available = fake_available;
    mode_t settings = fake_descriptor(100, &state[2]);
    navigation_t navigation;
    TEST_ASSERT_TRUE(navigation_init(&navigation, business, 2, &settings));
    TEST_ASSERT_EQUAL(1, state[0].enters);
    navigation_key(&navigation, MODE_KEY_UP, true, 1);
    navigation_key(&navigation, MODE_KEY_DOWN, true, 2);
    TEST_ASSERT_EQUAL(4, navigation_active(&navigation)->id);
    TEST_ASSERT_EQUAL(1, state[0].enters);
    TEST_ASSERT_EQUAL(0, state[0].leaves);
    TEST_ASSERT_FALSE(navigation_activate(&navigation, 2));

    state[1].available = true;
    navigation_key(&navigation, MODE_KEY_DOWN, true, 3);
    TEST_ASSERT_EQUAL(2, navigation_active(&navigation)->id);
    TEST_ASSERT_EQUAL(1, state[0].leaves);
    TEST_ASSERT_EQUAL(1, state[1].enters);
}

static void test_settings_return_falls_back_when_previous_mode_becomes_unavailable(void) {
    fake_mode_t state[3] = {0};
    state[0].available = state[1].available = true;
    mode_t business[] = {fake_descriptor(4, &state[0]), fake_descriptor(2, &state[1])};
    business[0].is_available = fake_available;
    business[1].is_available = fake_available;
    mode_t settings = fake_descriptor(100, &state[2]);
    navigation_t navigation;
    TEST_ASSERT_TRUE(navigation_init(&navigation, business, 2, &settings));
    navigation_key(&navigation, MODE_KEY_DOWN, true, 1);
    navigation_key(&navigation, MODE_KEY_OK, true, 2);
    state[1].available = false;
    navigation_key(&navigation, MODE_KEY_OK, false, 3);
    TEST_ASSERT_EQUAL(4, navigation_active(&navigation)->id);
}

static void test_navigation_cycles_two_pages_and_returns_from_settings(void) {
    fake_mode_t state[3] = {0};
    mode_t business[] = {fake_descriptor(4, &state[0]), fake_descriptor(2, &state[1])};
    mode_t settings = fake_descriptor(100, &state[2]);
    navigation_t navigation;
    TEST_ASSERT_TRUE(navigation_init(&navigation, business, 2, &settings));
    TEST_ASSERT_EQUAL(4, navigation_active(&navigation)->id);

    navigation_key(&navigation, MODE_KEY_UP, true, 1);
    TEST_ASSERT_EQUAL(2, navigation_active(&navigation)->id);
    navigation_key(&navigation, MODE_KEY_DOWN, true, 2);
    TEST_ASSERT_EQUAL(4, navigation_active(&navigation)->id);
    navigation_key(&navigation, MODE_KEY_DOWN, true, 3);
    navigation_key(&navigation, MODE_KEY_OK, true, 4);
    TEST_ASSERT_EQUAL(100, navigation_active(&navigation)->id);
    TEST_ASSERT_EQUAL(2, navigation.return_id);
    navigation_key(&navigation, MODE_KEY_OK, false, 5);
    TEST_ASSERT_EQUAL(2, navigation_active(&navigation)->id);
}

static void test_short_key_stays_and_activation_uses_stable_id(void) {
    fake_mode_t state[3] = {0};
    mode_t business[] = {fake_descriptor(40, &state[0]), fake_descriptor(7, &state[1])};
    mode_t settings = fake_descriptor(99, &state[2]);
    navigation_t navigation;
    TEST_ASSERT_TRUE(navigation_init(&navigation, business, 2, &settings));
    navigation_key(&navigation, MODE_KEY_DOWN, false, 1);
    TEST_ASSERT_EQUAL(40, navigation_active(&navigation)->id);
    TEST_ASSERT_EQUAL(1, state[0].keys);
    TEST_ASSERT_TRUE(navigation_activate(&navigation, 7));
    TEST_ASSERT_EQUAL(7, navigation_active(&navigation)->id);
    TEST_ASSERT_FALSE(navigation_activate(&navigation, 99));
    navigation_render(&navigation);
    TEST_ASSERT_EQUAL(1, state[1].renders);
}

static void test_empty_and_duplicate_registries_are_rejected(void) {
    navigation_t navigation;
    fake_mode_t state[3] = {0};
    mode_t duplicate[] = {fake_descriptor(4, &state[0]), fake_descriptor(4, &state[1])};
    mode_t settings = fake_descriptor(100, &state[2]);
    TEST_ASSERT_FALSE(navigation_init(&navigation, NULL, 0, &settings));
    TEST_ASSERT_FALSE(navigation_init(&navigation, duplicate, 2, &settings));
    mode_t unavailable[] = {fake_descriptor(5, &state[0])};
    unavailable[0].is_available = fake_available;
    state[0].available = false;
    TEST_ASSERT_FALSE(navigation_init(&navigation, unavailable, 1, &settings));
    mode_registry_t registry;
    TEST_ASSERT_FALSE(mode_registry_init(&registry, duplicate[0], duplicate[1], settings));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_registry_contains_exactly_two_business_pages);
    RUN_TEST(test_badge_mode_exposes_default_and_user_snapshots);
    RUN_TEST(test_unavailable_mode_is_skipped_without_lifecycle_churn);
    RUN_TEST(test_settings_return_falls_back_when_previous_mode_becomes_unavailable);
    RUN_TEST(test_navigation_cycles_two_pages_and_returns_from_settings);
    RUN_TEST(test_short_key_stays_and_activation_uses_stable_id);
    RUN_TEST(test_empty_and_duplicate_registries_are_rejected);
    return UNITY_END();
}
