#include "badge_http_server.h"

#ifdef ESP_PLATFORM
/* Link the source files directly into read-only application flash. This avoids a filesystem and
 * also avoids PlatformIO treating ESP-IDF's generated EMBED_FILES assembly as a SCons source. */
__asm__(
    ".section .rodata.badge_assets,\"a\",@progbits\n"
    ".balign 4\n.global index_html_start\nindex_html_start:\n"
    ".incbin \"" BADGE_INDEX_HTML_PATH "\"\n.global index_html_end\nindex_html_end:\n"
    ".balign 4\n.global style_css_start\nstyle_css_start:\n"
    ".incbin \"" BADGE_STYLE_CSS_PATH "\"\n.global style_css_end\nstyle_css_end:\n"
    ".balign 4\n.global badge_image_js_start\nbadge_image_js_start:\n"
    ".incbin \"" BADGE_IMAGE_JS_PATH "\"\n.global badge_image_js_end\nbadge_image_js_end:\n"
    ".balign 4\n.global app_js_start\napp_js_start:\n"
    ".incbin \"" BADGE_APP_JS_PATH "\"\n.global app_js_end\napp_js_end:\n"
    ".previous\n");

extern const uint8_t index_html_start[], index_html_end[];
extern const uint8_t style_css_start[], style_css_end[];
extern const uint8_t badge_image_js_start[], badge_image_js_end[];
extern const uint8_t app_js_start[], app_js_end[];

static badge_http_asset_t asset(const uint8_t *start, const uint8_t *end,
                                const char *content_type) {
    return (badge_http_asset_t){.data = start, .length = (size_t)(end - start),
                                .content_type = content_type};
}

badge_http_assets_t badge_http_embedded_assets(void) {
    return (badge_http_assets_t){
        .index_html = asset(index_html_start, index_html_end, "text/html; charset=utf-8"),
        .style_css = asset(style_css_start, style_css_end, "text/css; charset=utf-8"),
        .image_js = asset(badge_image_js_start, badge_image_js_end,
                          "application/javascript; charset=utf-8"),
        .app_js = asset(app_js_start, app_js_end, "application/javascript; charset=utf-8"),
    };
}
#endif
