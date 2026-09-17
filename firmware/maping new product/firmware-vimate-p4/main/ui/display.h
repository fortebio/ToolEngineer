/**
 * display.h — Single unified Display module cho VIMATE.
 *
 * Architecture theo xiaozhi pattern:
 *   1. HW init (display_init) — SPI + esp_lcd + LVGL port + backlight.
 *   2. UI setup (display_setup_ui) — tạo TẤT CẢ widgets lên lv_screen_active()
 *      MỘT LẦN duy nhất sau LVGL init.
 *   3. Display task pri 10 core 0 drain sched_q → execute callbacks
 *      (replace lv_async_call — tránh race với LVGL render task).
 *   4. Public API ASYNC — push to queue, return ngay. Caller (WS task,
 *      MCP task) KHÔNG bao giờ touch LVGL trực tiếp.
 *
 * KHÔNG bao giờ:
 *   - lv_scr_load() — chỉ làm việc trên lv_screen_active.
 *   - lv_layer_top — dễ z-order bug, dùng widget HIDDEN flag thay thế.
 *   - lv_async_call — không thread-safe khi LVGL đang flush.
 *   - asset_pack_has_file / SPIFFS check — đã gây crash trong path đó.
 *   - transform_rgb565a8 (inner_align STRETCH với A8 PNG) — crash path.
 *     Dùng lv_image_set_scale (pixel multiply integer) cho built-in PNG.
 *
 * Tất cả LVGL ops chạy TRONG display task (drain queue) → KHÔNG cần
 * display_lock cho widget setters. display_lock/unlock chỉ export cho legacy.
 */
#pragma once

#include "vimate.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== HW + bootstrap ========== */
/* Step 1: SPI + LVGL port bring-up. Call from app_main early. */
esp_err_t display_init(void);
/* Step 2: tạo widgets sau LVGL ready. Call AFTER display_init, BEFORE
 * any display_set_* API. Idempotent (gọi nhiều lần = no-op log warn). */
esp_err_t display_setup_ui(void);

/* Backlight 0..100 */
void display_set_backlight(int percent);
/* Tự tắt backlight sau N giây không có hoạt động hiển thị; 0 = không tắt. */
void display_set_sleep_timeout(int seconds);
/* Input thật (touch/nút) reset timeout và đánh thức màn đồng hồ chờ. */
void display_note_user_activity(void);
bool display_idle_clock_visible(void);
/* Runtime display health for heartbeat/Admin diagnostics. */
bool display_hw_ready(void);
bool display_hdmi_ready(void);
const char *display_runtime_status(void);
/* Force a high-contrast full-screen diagnostic panel. */
void display_show_test_pattern(const char *reason);
/* LCD panel handle/size for low-level renderers such as MP4 playback. Callers
 * must still serialize with display_lock/display_unlock around direct draws. */
esp_lcd_panel_handle_t display_lcd_panel_handle(void);
int display_lcd_width(void);
int display_lcd_height(void);
/* Vẽ một dải RGB565 (đã đúng thứ tự byte của panel) THẲNG lên màn, không qua
 * LVGL — dùng cho MP4 player. Trên panel DPI (P4, chống xé hình) driver giữ 2
 * frame buffer và lvgl_port đổi qua lại; ghi bằng esp_lcd_panel_draw_bitmap()
 * chỉ vào fb đang hiển thị nên video mất ngay khi LVGL đổi khung. Hàm này ghi
 * vào MỌI fb rồi msync. Board khác gọi thẳng esp_lcd_panel_draw_bitmap().
 * Caller PHẢI giữ display_lock() để LVGL không vẽ chồng. */
esp_err_t display_panel_blit(int x, int y, int w, int h, const void *rgb565);
/* Số lần flush đã lên panel (LV_EVENT_FLUSH_FINISH) — diag in mỗi 60s. Đứng
 * yên trong khi UI đáng ra đang đổi = display treo (flush chờ vsync không về). */
uint32_t display_flush_count(void);
/* Thống kê xoay màn bằng PPA (P4 DSI landscape). Trả false nếu board không xoay. */
bool display_rotate_stats(uint32_t *full_frames, uint32_t *area_blits, uint32_t *full_us_max);

/* Lock/unlock — exposed cho legacy callers. Bên trong display module
 * KHÔNG cần dùng (display task đã serialize). */
void display_lock(void);
void display_unlock(void);

/* ========== Async public API (ALL push to display task queue) ========== */
/* Mọi hàm sau ĐỀU non-blocking. Caller có thể gọi từ bất cứ task nào
 * (WS receive, MCP task, button task, main task). */

/* Set state label top — "Sẵn sàng", "Đang nghe...", v.v. */
void display_set_state(vimate_dev_state_t s);
/* Icon sóng WiFi góc trên phải (4 vạch + dBm). connected=false → vạch xám + "--".
 * Thread-safe (qua hàng đợi display). Gọi ~3 s/lần (telemetry.c). */
void display_set_wifi_rssi(int rssi_dbm, bool connected);
/* Icon Bluetooth bên trái icon WiFi. state = ble_prov_state_t (core/ble_wifi_prov.h):
 * UNAVAILABLE/OFF → xám, ADVERTISING → xanh, CONNECTED → xanh + 2 chấm.
 * Thread-safe (qua hàng đợi display); ble_wifi_prov.c gọi theo sự kiện. */
void display_set_ble_state(int state);
/* Set emotion full màn. PNG/GIF built-in 128×128 → overscan ~380px và crop theo LCD. */
void display_set_emotion(vimate_emotion_t e);
/* Current applied emotion for low-level HDMI direct renderers. */
vimate_emotion_t display_current_emotion(void);
/* Mặt robot toàn màn (board có BOARD_FACE_GIF_FULLSCREEN, xem ui/face.h): giữ một
 * biểu tượng tính năng trên mặt — key = tên clip ("sym_alarm", "sym_music",
 * "sym_weather_rain"…); NULL hoặc "" = bỏ. Tự nhường khi vào lượt nghe/nghĩ/nói.
 * Board khác: no-op. Trạng thái/cảm xúc tự chọn mặt qua display_set_state/emotion. */
void display_show_face(const char *key);
/* Set chat bubble (bottom). role chỉ để log; chỉ text được render. */
void display_set_chat_message(const char *role, const char *text);
/* Show preview image (lesson image). caller GIỮ ownership của dsc->data
 * đến khi ảnh bị thay/ẩn. NULL dsc = ẩn ngay. */
void display_show_preview_image(const lv_image_dsc_t *dsc);
/* Như trên nhưng as_slideshow=true → ảnh là frame màn chờ slideshow (giữ chế độ
 * đồng hồ-nền-ảnh). as_slideshow=false (ảnh nội dung) khi slideshow đang chạy sẽ
 * DIỆT slideshow để ảnh gia đình không đè lại lên nội dung. */
void display_show_preview_image_ex(const lv_image_dsc_t *dsc, bool as_slideshow);
/* Hiển thị popup info: title (vàng) + body (xám). Dùng cho error/activation. */
void display_set_message(const char *title, const char *body);
/* Hiển thị popup info rồi tự ẩn sau timeout_ms. Dùng cho xác nhận cấu hình. */
void display_set_message_timed(const char *title, const char *body, int timeout_ms);
/* Hiển thị activation code (font 48, letter-space lớn) + message. */
void display_show_activation(const char *code, const char *message);
/* Hiển thị reward stars (0-5) — auto-hide sau 3s. */
void display_show_reward(int stars);

/* Home chọn hoạt động trên máy. screen="courses" dùng carousel bìa lớn; các màn
 * còn lại giữ lưới icon. cover_urls có thể NULL/rỗng. */
void display_show_home(const char *screen, const char *const *ids,
                       const char *const *titles, const char *const *subtitles,
                       const char *const *cover_urls, int n,
                       const char *greeting, int stars);
void display_hide_home(void);

/* EDU quiz — màn trắc nghiệm 4 nút chạm. show hiện câu hỏi + đáp án; hit_index
 * trả nút bị chạm (0-3) hoặc -1. */
void display_show_quiz(const char *question, const char *const *opts, int n,
                       int step, int total);
void display_hide_quiz(void);
bool display_quiz_visible(void);
int  display_quiz_hit_index(int x, int y);
/* Set mốc giờ từ server_time (OTA) — header home + màn Đồng hồ dùng chung. */
void display_set_server_time(int64_t epoch_ms, int tz_offset_min);
/* EDU thống kê (màn 10): 4 số phút/bài/ngày/sao. */
void display_show_stats(int minutes, int lessons, int days, int stars);
void display_hide_stats(void);
bool display_stats_visible(void);
/* EDU uống nước (màn 9): ml + 2 nút chạm (0=Đã uống, 1=Để sau). */
void display_show_water(int todayMl, int goalMl);
void display_hide_water(void);
bool display_water_visible(void);
int  display_water_hit_index(int x, int y);
/* EDU lộ trình học (màn 6): tiêu đề + % + list bài (tên/sao/khóa). */
void display_show_progress(const char *title, int percent,
                           const char *const *names, const int *stars,
                           const bool *locked, int n);
void display_hide_progress(void);
bool display_progress_visible(void);
/* Nút "Trang chủ" nổi ở đáy màn con — trả true nếu chạm trúng. */
bool display_nav_home_hit(int x, int y);
/* Phản hồi chạm: pressed=true → tìm nút/ô dưới (x,y) và bật LV_STATE_PRESSED (nền
 * tối/đổi màu, style đặt lúc tạo); pressed=false → tắt. Gọi từ touch task lúc DOWN/UP. */
void display_touch_feedback(int x, int y, bool pressed);
/* true nếu (x,y) đang nằm trên một nút/ô bấm được (nav Home, bar, quiz, nước, ô Home,
 * mũi tên khoá học) — touch.c dùng để phân biệt "gõ lại nút" với "double-tap chỗ trống". */
bool display_touch_target_at(int x, int y);
/* Cờ "đang trong phiên AI" (chat/bài học): chạm màn chỉ gọi AI khi cờ này bật.
 * Agent là chế độ con có bottom bar cố định và ảnh nằm trong vùng phía trên. */
void display_set_ai_active(bool on);
bool display_ai_active(void);
void display_set_agent_active(bool on);
bool display_agent_active(void);
/* API tương thích server cũ; màn chờ hiện tại luôn nền đen, chữ trắng. */
void display_set_clock_bg(bool on);
const char *display_home_hit_id(int x, int y);
/* Carousel khóa học: hit trả -1/+1 cho nút trái/phải; navigate cũng xử lý swipe. */
int  display_home_course_nav_hit(int x, int y);
bool display_home_course_navigate(int delta);
/* ui_image gọi sau khi decode vào khung bìa; URL loại frame cũ khi vuốt nhanh. */
void display_show_home_cover_image(const char *url, const lv_image_dsc_t *dsc);
const char *display_home_bar_hit_id(int x, int y); /* bottom bar 5 app (mockup) */
bool display_home_visible(void); /* home đang hiện? (chạm ngoài icon → bỏ qua) */

/* Màn nhắc dễ thương (Hẹn giờ/Uống nước). is_water=true → giọt nước xanh, else đồng hồ cam.
 * Tự ẩn sau ~14s; chạm để tắt sớm. */
void display_show_alarm(const char *text, bool is_water);
void display_hide_alarm(void);
bool display_alarm_visible(void);

/* Thời khóa biểu EDU trên màn 3.5" — bảng 5 ngày, 2 buổi, dữ liệu do server
 * gom từ reminder kind=study. morning/afternoon là text gọn cho bảng;
 * *_detail là text đầy đủ cho màn chi tiết có thể vuốt. */
#define DISPLAY_TIMETABLE_DAYS 5
void display_show_timetable(const char *class_label, const char *const *morning,
                            const char *const *afternoon,
                            const char *const *morning_detail,
                            const char *const *afternoon_detail, int day_count);
void display_hide_timetable(void);
bool display_timetable_visible(void);
bool display_timetable_handle_tap(int x, int y);
bool display_timetable_scroll_detail(int direction);

/* Đếm ngược realtime từ app/icon thiết bị. total_seconds sẽ được kẹp 1..36000. */
void display_show_countdown(const char *label, int total_seconds);
void display_hide_countdown(void);
bool display_countdown_visible(void);

/* App Đồng hồ: hiện full màn giờ + ngày. epoch_local = epoch UTC + lệch múi giờ
 * (giây) do server gửi (FW không tự set TZ). Tick 1s; chạm để về Home. */
void display_show_clock(int64_t epoch_local, bool wallpaper);
void display_hide_clock(void);
bool display_clock_visible(void);

/* Slideshow màn chờ (ảnh gia đình + giờ). enabled=false → idle dùng đồng hồ đen.
 * urls: mảng URL ảnh; clock_local = epoch+lệch giờ cho lớp giờ. */
void display_set_slideshow(bool enabled, int interval_sec, bool show_clock,
                           int idle_after_sec, int64_t clock_local,
                           const char *const *urls, int n);
bool display_slideshow_active(void);
void display_stop_slideshow(void);
/* Bật slideshow NGAY (icon "Màn hình nghỉ") — bỏ qua nếu chưa nạp ảnh. */
void display_start_slideshow(void);

#ifdef __cplusplus
}
#endif
