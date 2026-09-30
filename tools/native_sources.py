Import("env")

from pathlib import Path


root = Path(env.subst("$PROJECT_DIR"))
env.Append(CPPPATH=[
    str(root / "test" / "host" / "stubs"),
    str(root / "components" / "bsp" / "include"),
    str(root / "components" / "text" / "include"),
    str(root / "main"),
    str(root / "main" / "services" / "config"),
    str(root / "main" / "services" / "badge"),
    str(root / "main" / "services" / "custom"),
    str(root / "main" / "services" / "display"),
    str(root / "main" / "settings"),
])
env.BuildSources("$BUILD_DIR/text", str(root / "components" / "text" / "src"))
env.BuildSources(
    "$BUILD_DIR/badge",
    str(root / "main" / "services" / "badge"),
    src_filter=[
        "+<badge_record.c>",
        "+<badge_store.c>",
        "+<badge_wifi_state.c>",
        "+<badge_wifi_service.c>",
        "+<badge_http_protocol.c>",
        "+<badge_http_server.c>",
    ],
)
env.BuildSources(
    "$BUILD_DIR/custom_storage",
    str(root / "main" / "services" / "custom"),
    src_filter=["+<custom_record.c>", "+<custom_store.c>"],
)
env.BuildSources(
    "$BUILD_DIR/config",
    str(root / "main" / "services" / "config"),
    src_filter=["+<settings_store.c>"],
)
env.BuildSources(
    "$BUILD_DIR/bsp_logic",
    str(root / "components" / "bsp" / "src"),
    src_filter=[
        "+<button_gesture.c>",
        "+<display_power_sequence.c>",
        "+<display_lifecycle.c>",
    ],
)
env.BuildSources("$BUILD_DIR/navigation", str(root / "main" / "navigation"))
env.BuildSources(
    "$BUILD_DIR/custom_mode",
    str(root / "main" / "modes" / "custom"),
    src_filter=["+<custom_mode.c>"],
)
env.BuildSources(
    "$BUILD_DIR/badge_mode",
    str(root / "main" / "modes" / "badge"),
    src_filter=["+<badge_mode.c>"],
)
env.BuildSources("$BUILD_DIR/display_logic", str(root / "main" / "services" / "display"))
env.BuildSources(
    "$BUILD_DIR/settings_page",
    str(root / "main" / "settings"),
    src_filter=["+<settings_page.c>"],
)
env.BuildSources(
    "$BUILD_DIR/app_controller",
    str(root / "main"),
    src_filter=["+<app_controller.c>"],
)
