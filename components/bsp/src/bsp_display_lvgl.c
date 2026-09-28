// components/bsp/src/bsp_display_lvgl.c
// LVGL 接入单独成文件:不用 LVGL 的开发者删掉本文件 + idf_component.yml 里的两条依赖即可。
#include "bsp_display.h"
#include "bsp_display_internal.h"
#include "display_lifecycle.h"
#include "bsp_pins.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "bsp_lvgl";

static lv_display_t *s_disp;
static display_lifecycle_t s_lifecycle;

static esp_err_t lifecycle_step(void *context, display_lifecycle_step_t step) {
    (void)context;
    switch (step) {
    case DISPLAY_LIFECYCLE_LOCK:
        return lvgl_port_lock(1000) ? ESP_OK : ESP_ERR_TIMEOUT;
    case DISPLAY_LIFECYCLE_UNLOCK:
        lvgl_port_unlock();
        return ESP_OK;
    case DISPLAY_LIFECYCLE_PORT_STOP:
        // Stop the tick while holding the recursive port lock so no refresh can start mid-transition.
        return lvgl_port_stop();
    case DISPLAY_LIFECYCLE_PANEL_SUSPEND:
        return bsp_display_panel_suspend();
    case DISPLAY_LIFECYCLE_PANEL_RESUME:
        return bsp_display_panel_resume();
    case DISPLAY_LIFECYCLE_PORT_RESUME:
        return lvgl_port_resume();
    case DISPLAY_LIFECYCLE_INVALIDATE: {
        lv_obj_t *screen = s_disp ? lv_display_get_screen_active(s_disp) : NULL;
        if (!screen) return ESP_ERR_INVALID_STATE;
        // Sleep invalidates LCD RAM assumptions; force the entire active screen before backlight restoration.
        lv_obj_invalidate(screen);
        return lvgl_port_task_wake(LVGL_PORT_EVENT_DISPLAY, s_disp);
    }
    }
    return ESP_ERR_INVALID_ARG;
}

lv_display_t *bsp_lvgl_init(void) {
    if (s_disp) return s_disp;
    if (!bsp_display_panel()) {
        ESP_LOGE(TAG, "请先成功调用 bsp_display_init()");
        return NULL;
    }

    const lvgl_port_cfg_t pc = ESP_LVGL_PORT_INIT_CONFIG();
    if (lvgl_port_init(&pc) != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init 失败");
        return NULL;
    }

    const lvgl_port_display_cfg_t dc = {
        .panel_handle = bsp_display_panel(),
        .io_handle    = bsp_display_io(),
        // C3 无 PSRAM，LCD 与 Wi-Fi 静态 RX 共用 DMA/内部 DRAM。8 行单缓冲
        // 约 3.84 KiB，仍支持分块全屏刷新，并给按需 SoftAP 留出连续 DMA 块。
        .buffer_size   = (uint32_t)BSP_LCD_W * BSP_LCD_DMA_LINES,
        .double_buffer = false,
        .hres = BSP_LCD_W, .vres = BSP_LCD_H,
        // 旋转/镜像必须在这里配:esp_lvgl_port 注册显示时会重新下发 MADCTL,
        // 覆盖 bsp_display.c 里 esp_lcd_panel_mirror() 的设置。
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        // swap_bytes:LVGL 输出小端 RGB565,ST7789 走 SPI 要大端 → 需交换高低字节。
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    s_disp = lvgl_port_add_disp(&dc);
    if (!s_disp) { ESP_LOGE(TAG, "lvgl_port_add_disp 失败"); return NULL; }
    s_lifecycle = (display_lifecycle_t){0};

    ESP_LOGI(TAG, "LVGL 就绪");
    return s_disp;
}

bool bsp_lvgl_lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_lvgl_unlock(void)         { lvgl_port_unlock(); }

esp_err_t bsp_display_suspend(void) {
    if (!s_disp) return ESP_ERR_INVALID_STATE;
    return display_lifecycle_suspend(&s_lifecycle, lifecycle_step, NULL);
}

esp_err_t bsp_display_resume(void) {
    if (!s_disp) return ESP_ERR_INVALID_STATE;
    return display_lifecycle_resume(&s_lifecycle, lifecycle_step, NULL);
}

bool bsp_display_suspended(void) { return s_lifecycle.suspended; }

esp_err_t bsp_lvgl_refresh_now(void) {
    if (!s_disp || s_lifecycle.suspended || bsp_display_panel_suspended())
        return ESP_ERR_INVALID_STATE;
    lv_refr_now(s_disp);
    return bsp_display_panel_wait_idle();
}
