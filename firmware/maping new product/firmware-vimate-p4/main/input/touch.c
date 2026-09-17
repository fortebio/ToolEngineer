/**
 * touch.c — FT6236G touch via I2C, tap màn = BTN_PRESS.
 * Giữ màn hình không được phép xóa WiFi; factory setup chỉ qua nút BOOT vật lý.
 *
 * I2C bus shared với audio codec (ES8311) — pin SCL=15, SDA=16, addr 0x38.
 * Poll mỗi 30ms — đủ responsive, tiết kiệm CPU.
 *
 * Register layout (FT6236G):
 *   0x02: # touches (0-2)
 *   0x03-0x06: P1 event/X high/X low/Y high
 *   ...
 */
#include "touch.h"
#include "vimate.h"
#include "boards/board.h"
#include "ui/display.h"
#include "protocol/envelope.h"
#include "audio/audio_pipeline.h"
#include "media/video_control.h"
#include "ui/ui_image.h"
#include "core/task_profile.h"
#include "network/ota_client.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

#ifndef TOUCH_LONG_MS
#define TOUCH_LONG_MS        5000
#endif

#define FT_REG_NUM_TOUCHES   0x02
#define FT_REG_P1_XH         0x03

#define TOUCH_LONG_MS        5000

#ifndef BOARD_TOUCH_USE_CST816S
#define BOARD_TOUCH_USE_CST816S 0
#endif
#ifndef BOARD_TOUCH_USE_CAPSENSE
#define BOARD_TOUCH_USE_CAPSENSE 0
#endif
#ifndef BOARD_TOUCH_USE_ST77922
#define BOARD_TOUCH_USE_ST77922 0
#endif
#ifndef BOARD_TOUCH_USE_ST7123
#define BOARD_TOUCH_USE_ST7123 0
#endif
/* ST7123 (panel ST7102 MIPI 4.3" tren board ESP32-P4) dung CHINH XAC giao thuc
 * register 16-bit cua touch tich hop ST77922: 0x0001 status, 0x0009 max points,
 * 0x0010 info (bit 0x08 = co toa do), 0x0014 report 7 byte/diem (byte0 bit7 =
 * valid, bit5-0 = X cao). Khac duy nhat dia chi I2C (0x55) -> dung chung code
 * path, khong nhan ban driver. */
#define BOARD_TOUCH_USE_REG16 (BOARD_TOUCH_USE_ST77922 || BOARD_TOUCH_USE_ST7123)

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;

static uint64_t touch_gpio_mask(int pin) {
    if (pin < 0 || pin >= 64) {
        return 0;
    }
    return 1ULL << (unsigned)pin;
}

/* Đọc 1 byte từ register */
static esp_err_t ft_read(uint8_t reg, uint8_t *out, size_t len) {
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, len, pdMS_TO_TICKS(50));
}

/* Ghi 1 byte vào register */
static esp_err_t ft_write(uint8_t reg, uint8_t val) {
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_dev, b, 2, pdMS_TO_TICKS(50));
}

#if BOARD_TOUCH_USE_REG16
static int s_st_maxpts = 1;   /* số điểm chip hỗ trợ (đọc lúc init) — đọc đủ 7*n byte như demo */
/* ST77922 touch tích hợp: địa chỉ register 16-bit big-endian (vendor bsp_touch.c). */
static esp_err_t st77922_read16(uint16_t reg, uint8_t *out, size_t len) {
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t w[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_master_transmit_receive(s_dev, w, 2, out, len, pdMS_TO_TICKS(50));
}
#endif

static bool touch_read_point(int *out_x, int *out_y) {
    uint8_t buf[4] = {0};
    if (ft_read(FT_REG_P1_XH, buf, sizeof(buf)) != ESP_OK) {
        return false;
    }
    int x = ((buf[0] & 0x0F) << 8) | buf[1];
    int y = ((buf[2] & 0x0F) << 8) | buf[3];
    /* Khớp phép xoay màn (esp_lcd: swap_xy → mirror_x/y) để toạ độ chạm trùng
     * không gian LVGL landscape. genu-v6: SWAP_XY+MIRROR_X+MIRROR_Y. */
#if BOARD_LCD_SWAP_XY
    { int t = x; x = y; y = t; }
#endif
#if BOARD_LCD_MIRROR_X
    x = (BOARD_LCD_H_RES - 1) - x;
#endif
    /* KHÔNG mirror Y cho CẢM ỨNG dù màn cấu hình MIRROR_Y. Bằng chứng: home lưới
     * 2 hàng — chạm hàng DƯỚI (Đồng hồ) map nhầm lên ô hàng TRÊN (server log
     * home_select id=chat khi chạm Đồng hồ). Trục Y cảm ứng đã thuận chiều màn nên
     * mirror là thừa → lật ngược. Home 1-hàng cũ không lộ vì cùng 1 băng y giữa màn.
     * (MIRROR_X vẫn cần — trục ngang đã đúng.) */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= BOARD_LCD_H_RES) x = BOARD_LCD_H_RES - 1;
    if (y >= BOARD_LCD_V_RES) y = BOARD_LCD_V_RES - 1;
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    return true;
}

/* Lấy 1 mẫu chạm: trả true nếu đang chạm + toạ độ trong không gian màn. */
#if BOARD_TOUCH_USE_REG16
static int s_native_x, s_native_y;   /* mẫu gần nhất, toạ độ chip (chẩn đoán) */
static bool touch_sample(int *out_x, int *out_y) {
    /* TOUCH_INFO(0x0010): bit 0x08 = có chạm. Điểm 0 ở TOUCH_POINT0(0x0014),
     * 7 byte: byte0 bit7=valid, bit5-0=X cao; byte1=X thấp; byte2 bit5-0=Y cao;
     * byte3=Y thấp (X/Y 14-bit). (vendor bsp_touch.c) */
    uint8_t info = 0;
    if (st77922_read16(0x0010, &info, 1) != ESP_OK) return false;
    if (!(info & 0x08)) return false;
    /* Đọc ĐỦ 7*max_points byte (đọc thiếu → chip trả rác, valid=0). Phát hiện 28/06:
     * đọc 7 byte → p[0]=0x00; đọc 35 byte (max=5) → toạ độ đúng. */
    uint8_t p[7 * 10] = {0};
    int rn = 7 * s_st_maxpts;
    if (rn < 7) rn = 7;
    if (rn > (int)sizeof(p)) rn = sizeof(p);
    if (st77922_read16(0x0014, p, rn) != ESP_OK) return false;
    if (!(p[0] & 0x80)) return false;
    int x = ((p[0] & 0x3F) << 8) | p[1];
    int y = ((p[2] & 0x3F) << 8) | p[3];
    s_native_x = x; s_native_y = y;
    /* Map toạ độ chạm NATIVE → LOGICAL bằng knob BOARD_TOUCH_* (tách khỏi knob màn
     * — màn xoay bằng LVGL sw_rotate, chip touch vẫn trả native dọc). */
#ifndef BOARD_TOUCH_SWAP_XY
#define BOARD_TOUCH_SWAP_XY  0
#endif
#ifndef BOARD_TOUCH_MIRROR_X
#define BOARD_TOUCH_MIRROR_X 0
#endif
#ifndef BOARD_TOUCH_MIRROR_Y
#define BOARD_TOUCH_MIRROR_Y 0
#endif
#if BOARD_TOUCH_SWAP_XY
    { int t = x; x = y; y = t; }
#endif
#if BOARD_TOUCH_MIRROR_X
    x = (BOARD_LCD_H_RES - 1) - x;
#endif
#if BOARD_TOUCH_MIRROR_Y
    y = (BOARD_LCD_V_RES - 1) - y;
#endif
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= BOARD_LCD_H_RES) x = BOARD_LCD_H_RES - 1;
    if (y >= BOARD_LCD_V_RES) y = BOARD_LCD_V_RES - 1;
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    return true;
}
#else
static bool touch_sample(int *out_x, int *out_y) {
    uint8_t n = 0;
    if (ft_read(FT_REG_NUM_TOUCHES, &n, 1) != ESP_OK) return false;
    if (!(n > 0 && n < 5)) return false;
    return touch_read_point(out_x, out_y);
}
#endif

/* Navigation là lệnh thoát chương trình, không chỉ đổi màn hình.
 * Dừng local trước để audio/ảnh không tiếp tục trong thời gian chờ server. */
static void stop_active_program_for_navigation(void) {
    video_control_stop();
    audio_pipeline_mic_stop();
    audio_pipeline_speaker_stop();
    display_set_ai_active(false);
    ui_image_hide();
    if (g_vimate_events) {
        xEventGroupClearBits(g_vimate_events,
                             VIMATE_EVT_AUTO_LISTEN | VIMATE_EVT_BTN_PRESS |
                             VIMATE_EVT_BARGE_IN);
        xEventGroupSetBits(g_vimate_events, VIMATE_EVT_NAV_STOP);
    }
    envelope_send_abort();
}

/* Chu kỳ quét. 30 ms cũ + cửa "held > 30 ms" bên dưới làm tap nhanh (trẻ gõ 40–60 ms)
 * chỉ lọt 1 mẫu → bị bỏ. 10 ms: đọc 1 byte TOUCH_INFO khi rảnh (~0,25 ms bus I2C
 * 400 kHz), đọc thêm 35 byte chỉ khi đang chạm (max points = 5) → ≤ 10 % bus, còn
 * rộng cho codec/PMIC dùng chung. Chip đã debounce; ta chỉ đòi ≥ 2 mẫu liên tiếp
 * để loại "ma" 1 mẫu. (13/09/2026, README-P4 §6.7) */
#define TOUCH_POLL_MS        10
#define TOUCH_MIN_SAMPLES    2
/* Tap chỉ 1 mẫu nhưng DOWN→UP cách ≥ 40 ms tick vẫn là tap thật (task bị chặn giữa
 * chừng, không phải "ma" — ma chỉ 1 chu kỳ quét ~10 ms). Log 13/09: 14 tap 190–390 ms
 * bị bỏ vì samples=1 khi emoji đang render. */
#define TOUCH_MIN_HELD_MS    40
/* Ngón lệch quá ngưỡng này giữa DOWN và UP = VUỐT, không phải tap. Chỉ hai màn hiểu
 * vuốt (khoá học: ngang; lịch học: dọc); còn lại bỏ qua — trước đây vuốt rơi xuống
 * chuỗi tap và bị hiểu là "tap ngoài" → về Home / mở lượt nghe (log 13/09 15:2x). */
#define TOUCH_SWIPE_PX       34
/* DOWN mới cách UP trước < 80 ms = ngón dội (nhấc-chạm lại), không phải tap thứ hai.
 * Log 13/09 17:00: UP 110 390 → DOWN 110 430 (40 ms) bị hiểu là double-tap → về Home
 * ngay lúc vừa mở lượt nghe. Người không gõ hai lần cách 40 ms. */
#define TOUCH_BOUNCE_MS      80
#ifndef BOARD_TOUCH_TASK_PRIO
#define BOARD_TOUCH_TASK_PRIO VIMATE_TASK_PRIO_NETWORK
#endif
static int touch_int_level(void) {
    return BOARD_TOUCH_INT_GPIO >= 0 ? gpio_get_level((gpio_num_t)BOARD_TOUCH_INT_GPIO) : -1;
}
/* Cổng INT: rảnh + INT cao (không chạm) → bỏ đọc I2C 2/3 chu kỳ. Chu kỳ thứ 3 vẫn đọc
 * I2C để không phụ thuộc hoàn toàn vào INT (nếu INT chỉ xung thì DOWN trễ ≤ 30 ms). */
static bool touch_idle_skip_i2c(bool was_touched) {
#if BOARD_TOUCH_INT_ACTIVE_LOW
    static unsigned n;
    if (was_touched || BOARD_TOUCH_INT_GPIO < 0) return false;
    if (touch_int_level() != 1) return false;
    return (++n % 3) != 0;
#else
    (void)was_touched;
    return false;
#endif
}

static void touch_task(void *arg) {
    bool was_touched = false;
    TickType_t down_at = 0;
    TickType_t last_tap_up = 0;   /* mốc tap trước — phát hiện double-tap về Home */
    TickType_t last_up_any = 0;   /* mốc UP gần nhất (mọi loại) — phát hiện dội */
    bool bounce = false;
    int last_tap_x = 0, last_tap_y = 0;
    bool last_tap_on_target = false;   /* tap trước rơi vào nút → không mở chuỗi double-tap */
    bool last_tap_crisp = false;       /* tap trước gọn (ngắn, không trượt) */
    bool long_fired = false;
    int down_x = 0;
    int down_y = 0;
    int cur_x = 0;
    int cur_y = 0;
    int samples = 0;              /* số mẫu "đang chạm" trong lần chạm này */
    bool down_woke_idle = false;

    while (1) {
        {
            int sx = 0, sy = 0;
            bool is_touched = touch_idle_skip_i2c(was_touched) ? false : touch_sample(&sx, &sy);
            TickType_t now = xTaskGetTickCount();

            if (is_touched && !was_touched) {
                /* Touch DOWN */
                bounce = last_up_any != 0 &&
                         (now - last_up_any) * portTICK_PERIOD_MS < TOUCH_BOUNCE_MS;
                down_at = now;
                long_fired = false;
                down_x = sx; down_y = sy;
                cur_x = sx; cur_y = sy;
                samples = 1;
                down_woke_idle = display_idle_clock_visible();
                display_note_user_activity();
                /* Phản hồi nhìn thấy NGAY khi chạm (nút tối/đổi nền), không đợi nhấc tay. */
                display_touch_feedback(sx, sy, true);
#if CONFIG_VIMATE_DIAG_ENABLE
                ESP_LOGI(TAG_MAIN, "Touch DOWN native=(%d,%d) logical=(%d,%d) int=%d",
                         s_native_x, s_native_y, sx, sy, touch_int_level());
#endif
                was_touched = true;
            } else if (is_touched && was_touched) {
                cur_x = sx; cur_y = sy;
                samples++;
            } else if (!is_touched && was_touched) {
                /* Touch UP */
                TickType_t held = (now - down_at) * portTICK_PERIOD_MS;
                display_touch_feedback(0, 0, false);
                last_up_any = now;
                if (bounce) {
                    ESP_LOGI(TAG_MAIN, "Dội chạm (%lu ms) — bỏ qua", (unsigned long)held);
                    bounce = false;
                    was_touched = false;
                    continue;
                }
#if CONFIG_VIMATE_DIAG_ENABLE
                ESP_LOGI(TAG_MAIN, "Touch UP held=%lu ms samples=%d moved=(%d,%d) int=%d%s",
                         (unsigned long)held, samples, cur_x - down_x, cur_y - down_y,
                         touch_int_level(),
                         (samples < TOUCH_MIN_SAMPLES && held < TOUCH_MIN_HELD_MS) ? " -> BO (qua ngan)" : "");
#endif
                if (!long_fired && held < TOUCH_LONG_MS &&
                    (samples >= TOUCH_MIN_SAMPLES || held >= TOUCH_MIN_HELD_MS)) {
                    int dx = cur_x - down_x;
                    int dy = cur_y - down_y;
#if CONFIG_VIMATE_DIAG_ENABLE
                    /* Đo thời gian 4 hit-test đầu (mỗi cái lấy lock LVGL): > 20 ms là
                     * taskLVGL đang giữ lock (render emoji) — số này giải thích "nhấn chậm". */
                    int64_t t_hit0 = esp_timer_get_time();
#endif
                    if (down_woke_idle) {
                        ESP_LOGI(TAG_MAIN, "Idle clock tap → đánh thức về HOME");
                        display_set_ai_active(false);
                        envelope_send_home_select("home");
                        down_woke_idle = false;
                        was_touched = false;
                        continue;
                    }
                    if (abs(dx) > TOUCH_SWIPE_PX || abs(dy) > TOUCH_SWIPE_PX) {
                        /* VUỐT — không rơi xuống chuỗi tap, không tính vào double-tap. */
                        if (abs(dx) > abs(dy)) {
                            if (display_home_course_navigate(dx < 0 ? 1 : -1)) {
                                ESP_LOGI(TAG_MAIN, "Vuốt %s → khoá học trang %s",
                                         dx < 0 ? "trái" : "phải", dx < 0 ? "sau" : "trước");
                            } else {
                                ESP_LOGI(TAG_MAIN, "Vuốt ngang (%d,%d) — bỏ qua", dx, dy);
                            }
                        } else if (display_timetable_visible() &&
                                   display_timetable_scroll_detail(dy < 0 ? 1 : -1)) {
                            ESP_LOGI(TAG_MAIN, "Timetable detail swipe %s (%d,%d)->(%d,%d)",
                                     dy < 0 ? "up" : "down", down_x, down_y, cur_x, cur_y);
                        } else {
                            ESP_LOGI(TAG_MAIN, "Vuốt dọc (%d,%d) — bỏ qua", dx, dy);
                        }
                        last_tap_up = 0;
                        was_touched = false;
                        continue;
                    }
                    /* DOUBLE-TAP trên CHỖ TRỐNG → VỀ HOME (spec 001 US4/FR-015): ngắt lời +
                     * dọn overlay cục bộ + xin server đẩy lại lưới Home. Đang ở Home → vô hại.
                     *
                     * 13/09 15:xx, 89 lần chạm: 17 lần "Double-tap → VỀ HOME" đều là gõ LẠI
                     * cùng một nút sau 140–430 ms (trẻ gõ dồn khi chưa thấy phản hồi).
                     * 13/09 18:xx: bar → Lịch rồi 265 ms sau chạm ô ngày (không nằm trong
                     * pressable_at) → vẫn bị về Home. Quy tắc chốt:
                     *   - tap trúng NÚT (hoặc đang ở màn Lịch, ô ngày là nút): cùng chỗ trong
                     *     450 ms → bỏ qua (chống dội); khác chỗ → tap thường;
                     *   - double-tap chỉ khi CẢ HAI tap đều rơi vào chỗ trống, cả hai đều
                     *     "gọn" (giữ ≤ 180 ms, trượt ≤ 15 px) và cùng chỗ (≤ 60 px).
                     * 13/09 18:xx soak: tap nghe (67 ms) → 81 ms sau ngón đặt lại 231 ms,
                     * trượt 30 px → về Home giữa bài học. Double-tap thật là 2 cú gõ gọn;
                     * cú đặt-lại-ngón dài và trượt thì không. */
                    bool on_target = display_touch_target_at(down_x, down_y) ||
                                     display_timetable_visible();
                    bool crisp = held <= 180 && abs(dx) <= 15 && abs(dy) <= 15;
                    bool quick = last_tap_up != 0 &&
                                 (now - last_tap_up) * portTICK_PERIOD_MS <= 450;
                    if (quick && on_target &&
                        abs(down_x - last_tap_x) <= 40 && abs(down_y - last_tap_y) <= 40) {
                        ESP_LOGI(TAG_MAIN, "Tap lặp cùng nút (%d,%d) sau %lu ms — bỏ qua",
                                 down_x, down_y,
                                 (unsigned long)((now - last_tap_up) * portTICK_PERIOD_MS));
                        last_tap_up = now;
                        was_touched = false;
                        continue;
                    }
                    if (quick && !on_target && !last_tap_on_target && crisp && last_tap_crisp &&
                        abs(down_x - last_tap_x) <= 60 && abs(down_y - last_tap_y) <= 60) {
                        last_tap_up = 0;  /* reset — triple-tap không thành 2 double */
                        ESP_LOGI(TAG_MAIN, "Double-tap (%d,%d) → VỀ HOME", down_x, down_y);
                        stop_active_program_for_navigation();
                        display_hide_alarm();
                        display_hide_countdown();
                        display_hide_clock();
                        display_hide_timetable();
                        display_hide_quiz();
                        display_hide_stats();
                        display_hide_water();
                        display_hide_progress();
                        envelope_send_home_select("home");
                        was_touched = false;
                        vTaskDelay(pdMS_TO_TICKS(30));
                        continue;
                    }
                    last_tap_up = now;
                    last_tap_x = down_x; last_tap_y = down_y;
                    last_tap_on_target = on_target;
                    last_tap_crisp = crisp;
                    /* Nút Home tròn giữa đáy — luôn về danh sách khóa học. Khi đang
                     * ở một lưới khác (AI Agent), server thay nội dung lưới tại chỗ;
                     * khi ở màn con thì dọn overlay trước. */
                    bool nav_home = display_nav_home_hit(down_x, down_y);
#if CONFIG_VIMATE_DIAG_ENABLE
                    {
                        int64_t w = (esp_timer_get_time() - t_hit0) / 1000;
                        if (w > 20) ESP_LOGW(TAG_MAIN, "Touch hit-test cho lock LVGL %lld ms", (long long)w);
                    }
#endif
                    /* 13/09/2026: KHÔNG ẩn màn cũ trước khi server trả màn mới — ẩn
                     * trước = màn trống/lộ màn dưới 150–400 ms rồi màn mới vẽ đè, người
                     * dùng thấy "load lại cả màn". Màn mới (apply_home/timetable/
                     * water/…) tự ẩn mọi màn cũ trong cùng một lần lock
                     * (screens_dismiss_locked). Chỉ ẩn tại chỗ khi GỬI THẤT BẠI (WS
                     * rớt) để trẻ không kẹt; nhắc (alarm) là gián đoạn → tắt ngay. */
                    if (nav_home) {
                        bool grid_visible = display_home_visible();
                        ESP_LOGI(TAG_MAIN, "Nav Home → danh sách khóa học (grid=%d)",
                                 grid_visible);
                        stop_active_program_for_navigation();
                        display_hide_alarm();
                        if (envelope_send_home_select("home") != ESP_OK && !grid_visible) {
                            display_hide_stats();
                            display_hide_water();
                            display_hide_progress();
                            display_hide_timetable();
                            display_hide_countdown();
                            display_hide_clock();
                            display_hide_quiz();
                        }
                        was_touched = false;
                        continue;
                    }
                    /* Bottom bar (global) — chạm 1 app: ưu tiên trước handler màn con
                     * (vì bar nổi trên cùng ở mọi màn). Ẩn màn con hiện → server đẩy
                     * màn mới. */
                    const char *bbid = display_home_bar_hit_id(down_x, down_y);
                    if (bbid && bbid[0]) {
                        ESP_LOGI(TAG_MAIN, "Bottom bar → %s", bbid);
                        stop_active_program_for_navigation();
                        display_hide_alarm();
                        if (envelope_send_home_select(bbid) != ESP_OK) {
                            display_hide_stats();
                            display_hide_water();
                            display_hide_progress();
                            display_hide_timetable();
                            display_hide_countdown();
                            display_hide_clock();
                        }
                        was_touched = false;
                        continue;
                    }
                    /* Quiz đang hiện + chạm trúng 1 nút đáp án → gửi quizans_<i>
                     * (server map index→label → chấm như voice). Ưu tiên trước home. */
                    int qi = display_quiz_hit_index(down_x, down_y);
                    if (display_quiz_visible() && qi >= 0) {
                        char aid[24];
                        snprintf(aid, sizeof(aid), "quizans_%d", qi);
                        ESP_LOGI(TAG_MAIN, "Quiz tap → %s", aid);
                        envelope_send_home_select(aid);
                        was_touched = false;
                        continue;
                    }
                    /* Màn uống nước: 0=Đã uống, 1=Để sau, 2=Tắt nhắc. */
                    int wi = display_water_hit_index(down_x, down_y);
                    if (wi >= 0) {
                        const char *wsel = wi == 0 ? "water_drank"
                                         : wi == 1 ? "water_snooze" : "water_off";
                        ESP_LOGI(TAG_MAIN, "Water tap → %s", wsel);
                        envelope_send_home_select(wsel);
                        if (wi != 0) display_hide_water();  /* Để sau/Tắt → ẩn ngay */
                        was_touched = false;
                        continue;
                    }
                    /* Pha C: nếu lưới home đang hiện + chạm trúng 1 ô → chọn hoạt
                     * động đó (gửi home_select + ẩn lưới). Ngoài lưới → BTN_PRESS
                     * (mở lượt nghe như cũ). */
                    int course_nav = display_home_course_nav_hit(down_x, down_y);
                    const char *hid = display_home_hit_id(down_x, down_y);
	                    if (display_alarm_visible()) {
	                        /* Màn nhắc (Hẹn giờ/Uống nước) đang hiện → chạm để tắt. */
	                        ESP_LOGI(TAG_MAIN, "Alarm tap → tắt nhắc");
	                        display_hide_alarm();
	                    } else if (display_countdown_visible()) {
	                        /* Nội dung countdown không còn là nút thoát. Dùng Home
	                         * trên bottom bar để tránh chạm nhầm làm mất đồng hồ. */
	                        ESP_LOGI(TAG_MAIN, "Countdown tap ngoài bottom bar — bỏ qua");
                    } else if (display_timetable_visible()) {
                        if (display_timetable_handle_tap(down_x, down_y)) {
                            ESP_LOGI(TAG_MAIN, "Timetable tap → chi tiết/ngược lại (%d,%d)",
                                     down_x, down_y);
                        } else {
                            /* Chạm ngoài vùng ngày → về Home THẬT: chỉ ẩn lịch thì màn
                             * trống tới khi chạm lần nữa (log 13/09 81 863 → 84 048). */
                            ESP_LOGI(TAG_MAIN, "Timetable tap ngoài ngày → về Home");
                            if (envelope_send_home_select("home") != ESP_OK) {
                                display_hide_timetable();
                            }
                        }
	                    } else if (display_clock_visible()) {
                        /* App Đồng hồ đang hiện → chạm để về Home. */
                        ESP_LOGI(TAG_MAIN, "Clock tap → về Home");
                        display_hide_clock();
                    } else if (!g_vimate_server.activated ||
                               g_vimate_server.device_token[0] == '\0') {
                        /* Màn kích hoạt chưa có WS nên gửi home_select không thể
                         * tạo phản hồi nhìn thấy. Xác nhận tap ngay trên màn và
                         * chủ động kiểm tra lại trạng thái activation. */
                        ESP_LOGI(TAG_MAIN, "Activation tap → kiểm tra máy chủ");
                        display_show_activation(
                            g_vimate_server.activation_code,
                            "Đã nhận chạm. Đang kiểm tra trạng thái kích hoạt...");
                        if (g_vimate_server.activation_code[0] != '\0') {
                            ota_client_start_activation_poll(
                                g_vimate_server.activation_code);
                        } else {
                            ota_client_start_registration_recovery();
                        }
                    } else if (course_nav != 0 &&
                               display_home_course_navigate(course_nav)) {
                        ESP_LOGI(TAG_MAIN, "Home course → trang %s",
                                 course_nav < 0 ? "trước" : "sau");
                    } else if (hid && hid[0]) {
                        /* Chạm TRÚNG 1 icon → mở đúng chức năng đó. */
                        ESP_LOGI(TAG_MAIN, "Home tap → select '%s'", hid);
                        envelope_send_home_select(hid);
                        display_hide_home();
                    } else if (display_home_visible()) {
                        /* Đang ở Home, chạm NGOÀI icon → bỏ qua (như iOS, không tự
                         * kích hoạt chức năng). Chỉ icon mới vào chức năng. */
                        ESP_LOGI(TAG_MAIN, "Home tap ngoài icon (%d,%d) — bỏ qua",
                                 down_x, down_y);
                    } else if (display_ai_active()) {
                        /* ĐANG trong phiên AI (chat/bài học) → tap = mở lượt nghe. */
                        xEventGroupSetBits(g_vimate_events, VIMATE_EVT_BTN_PRESS);
                        ESP_LOGI(TAG_MAIN, "Touch tap → nghe (AI active) (%lu ms) at (%d,%d)",
                                 (unsigned long)held, down_x, down_y);
                    } else {
                        /* Chưa vào AI (mặt cười/màn chờ/slideshow) → chạm KHÔNG gọi AI,
                         * chỉ ĐÁNH THỨC về lưới Home để trẻ chọn. */
                        ESP_LOGI(TAG_MAIN, "Touch tap (chưa vào AI) → đánh thức về HOME");
                        display_stop_slideshow();
                        envelope_send_home_select("home");
                    }
                }
                down_woke_idle = false;
                was_touched = false;
            } else if (is_touched && was_touched && !long_fired) {
                /* Touch held → check long-press */
                TickType_t held = (now - down_at) * portTICK_PERIOD_MS;
                if (held >= TOUCH_LONG_MS) {
                    long_fired = true;
                    ESP_LOGI(TAG_MAIN, "Touch long-press ignored (WiFi reset requires BOOT)");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
    }
}

esp_err_t touch_init(void) {
#if BOARD_TOUCH_USE_CAPSENSE
    ESP_LOGI(TAG_MAIN,
             "Touch pads configured for board %s: head GPIO%d, body GPIO%d, back GPIO%d; I2C touch driver disabled",
             BOARD_NAME,
             BOARD_TOUCH_PAD_HEAD_GPIO,
             BOARD_TOUCH_PAD_BODY_GPIO,
             BOARD_TOUCH_PAD_BACK_GPIO);
    return ESP_OK;
#endif
    /* Reset pulse — FT6236 cần LOW≥5ms rồi HIGH≥300ms để khởi tạo I2C.
     * Nhiều board ES3N28P treo RST trên GPIO18 (active low) — không pulse
     * → chip stuck, NUM_TOUCHES luôn trả 0. */
    const int rst_pin = BOARD_TOUCH_RST_GPIO;
    if (rst_pin >= 0) {
        gpio_config_t rst_cfg = {
            .pin_bit_mask = touch_gpio_mask(rst_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&rst_cfg);
        gpio_set_level((gpio_num_t)rst_pin, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level((gpio_num_t)rst_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(300));
        ESP_LOGI(TAG_MAIN, "Touch RST pulse done (GPIO%d)", rst_pin);
    }

    /* Reuse I2C bus đã tạo bởi ES8311 codec (cùng port 0 SCL=15 SDA=16). */
    esp_err_t r = i2c_master_get_bus_handle(BOARD_TOUCH_I2C_NUM, &s_i2c_bus);
    if (r == ESP_OK && s_i2c_bus != NULL) {
        ESP_LOGI(TAG_MAIN, "Touch: reuse I2C bus from ES8311 codec");
    } else {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port = BOARD_TOUCH_I2C_NUM,
            .sda_io_num = BOARD_TOUCH_I2C_SDA,
            .scl_io_num = BOARD_TOUCH_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        r = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
        if (r != ESP_OK) {
            ESP_LOGE(TAG_MAIN, "Touch I2C bus init fail: %s", esp_err_to_name(r));
            return r;
        }
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_TOUCH_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    r = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_dev);
    if (r != ESP_OK) {
        ESP_LOGE(TAG_MAIN, "Touch FT6236 add device fail: %s", esp_err_to_name(r));
        return r;
    }

    /* Probe — ST77922 touch: đọc MAX_TOUCHES (0x0009, reg 16-bit). FT6x36: 0xA3;
     * CST816S: 0xA7. Read fail → log WARN nhưng vẫn spawn task (retry sau). */
#if BOARD_TOUCH_USE_REG16
    /* INT (GPIO) = input pull-up (chip kéo xuống khi chạm; lib chính chủ cấu hình vậy). */
    if (BOARD_TOUCH_INT_GPIO >= 0) {
        gpio_config_t int_cfg = {
            .pin_bit_mask = touch_gpio_mask(BOARD_TOUCH_INT_GPIO),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&int_cfg);
    }
    /* Reset dài hơn (low 100ms/high 100ms) + HANDSHAKE: chờ STATUS(0x0001) nibble
     * thấp = 0 (chip báo hết init) rồi mới dùng. Thiếu bước này → engine quét chạm
     * KHÔNG chạy → TOUCH_INFO luôn 0x00 dù có chạm. (lib chính chủ ST77922_Touch.cpp) */
    if (BOARD_TOUCH_RST_GPIO >= 0) {
        gpio_set_level((gpio_num_t)BOARD_TOUCH_RST_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level((gpio_num_t)BOARD_TOUCH_RST_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    {
        uint8_t st = 0xFF; int tries = 0;
        do {
            if (st77922_read16(0x0001, &st, 1) != ESP_OK) { st = 0xFF; break; }
            if (!(st & 0x0F)) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        } while (++tries < 60);
        uint8_t maxpts = 0;
        if (st77922_read16(0x0009, &maxpts, 1) == ESP_OK) {
            s_st_maxpts = (maxpts >= 1 && maxpts <= 10) ? maxpts : 1;
            ESP_LOGI(TAG_MAIN, "Touch reg16 OK (addr 0x%02X, status=0x%02X, max points=%d)",
                     BOARD_TOUCH_I2C_ADDR, st, maxpts);
        } else {
            ESP_LOGW(TAG_MAIN, "Touch reg16 khong response I2C (addr 0x%02X) - kiem day/RST",
                     BOARD_TOUCH_I2C_ADDR);
        }
    }
#else
    uint8_t chip_id = 0;
    uint8_t fw_ver = 0;
#if BOARD_TOUCH_USE_CST816S
    if (ft_read(0xA7, &chip_id, 1) == ESP_OK) {
        ft_read(0xA9, &fw_ver, 1);
        ESP_LOGI(TAG_MAIN, "CST816S chip ID: 0x%02X fw=0x%02X", chip_id, fw_ver);
    } else {
        ESP_LOGW(TAG_MAIN, "CST816S chip không response I2C (addr 0x%02X) — touch sẽ không hoạt động",
                 BOARD_TOUCH_I2C_ADDR);
    }
#else
    if (ft_read(0xA3, &chip_id, 1) == ESP_OK) {
        ft_read(0xA6, &fw_ver, 1);
        ESP_LOGI(TAG_MAIN, "FT6x36 chip ID: 0x%02X fw=0x%02X", chip_id, fw_ver);
    } else {
        ESP_LOGW(TAG_MAIN, "FT6x36 chip không response I2C (addr 0x%02X) — touch sẽ không hoạt động",
                 BOARD_TOUCH_I2C_ADDR);
    }
#endif
#endif /* BOARD_TOUCH_USE_REG16 probe */

#if !BOARD_TOUCH_USE_CST816S && !BOARD_TOUCH_USE_REG16
    /* LONG-SHOT cứu touch (13/06): chip FT6236 SỐNG (ID 0x64) + I2C OK nhưng
     * TD_STATUS luôn 0. RST=NC nên không reset cứng được → thử "đá" chip vào chế
     * độ quét bằng I2C: DEVICE_MODE=normal, TẮT monitor (luôn quét), hạ threshold
     * (nhạy hơn), power=active. Nếu vẫn 0 chạm → lỗi phần cứng cảm ứng unit này. */
    ft_write(0x00, 0x00); /* DEVICE_MODE = working/normal (thoát factory/test) */
    ft_write(0x86, 0x00); /* ID_G_CTRL = 0 → KHÔNG vào monitor, luôn active scan */
    ft_write(0xA5, 0x00); /* ID_G_PMODE = active */
    ft_write(0xA4, 0x00); /* ID_G_MODE = polling (INT=NC) */
    ft_write(0x80, 0x0A); /* ID_G_THGROUP = threshold thấp (~10) → nhạy hơn */
    {
        uint8_t v00 = 0xFF, v86 = 0xFF, v80 = 0xFF;
        ft_read(0x00, &v00, 1); ft_read(0x86, &v86, 1); ft_read(0x80, &v80, 1);
        ESP_LOGW(TAG_MAIN, "Touch cfg after write: mode=0x%02X ctrl=0x%02X thg=0x%02X",
                 v00, v86, v80);
    }
#endif

    /* Core 0 — input poll. Cùng Core 0 với main + WS receive → tap event
     * tới main loop nhanh, không phải cross-core context switch. */
    /* Ưu tiên theo board: P4 đặt 7 (trên taskLVGL 6) để quét không bị khối render giữ;
     * mặc định = NETWORK (5) như S3. */
    xTaskCreatePinnedToCore(touch_task, "touch", 3072, NULL,
                            BOARD_TOUCH_TASK_PRIO, NULL, VIMATE_TASK_CORE_UI);
    ESP_LOGI(TAG_MAIN, "Touch %s init OK (SCL=%d SDA=%d addr=0x%02X)",
#if BOARD_TOUCH_USE_ST7123
             "ST7123",
#elif BOARD_TOUCH_USE_ST77922
             "ST77922",
#elif BOARD_TOUCH_USE_CST816S
             "CST816S",
#else
             "FT6236G",
#endif
             BOARD_TOUCH_I2C_SCL, BOARD_TOUCH_I2C_SDA, BOARD_TOUCH_I2C_ADDR);
    return ESP_OK;
}
