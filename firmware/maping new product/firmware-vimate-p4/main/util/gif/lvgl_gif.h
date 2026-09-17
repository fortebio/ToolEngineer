/**
 * lvgl_gif.h — C port của xiaozhi's LvglGif controller.
 *
 * Driver thiết kế:
 *   - gifdec.c (pure C GIF89a decoder) decode 1 frame vào canvas ARGB8888.
 *   - lv_timer_t (10ms) tick trên LVGL task context, gọi next_frame().
 *   - next_frame() check delay → gd_get_frame → gd_render_frame → frame_cb().
 *   - frame_cb() do display.c gắn: lv_image_set_src(widget, image_dsc()) để
 *     LVGL invalidate + redraw vùng widget. Vì cb chạy trong LVGL task
 *     (timer context) → thread-safe, không cần lvgl_port_lock.
 *
 * KHÁC xiaozhi:
 *   - C struct + functions thay class C++.
 *   - Drop loop_delay / pause / resume / loop_count getter — chưa cần.
 *   - Drop std::function → C function pointer.
 *
 * Chế độ "mặt toàn màn" (P4 4.3", 15/09/2026 — lvgl_gif_create_ex với opts):
 *   - opaque: GIF đã có nền đen kín → xuất RGB565 KHÔNG alpha, MỘT buffer ổn định
 *     (không xoay 3 buffer). LVGL blit thẳng (memcpy từng dòng), không blend.
 *   - scale N: nhân N pixel theo cả hai chiều ngay lúc publish (nearest, ghi 32-bit)
 *     → 400×240 ×2 = 800×480 = đúng màn logical, widget không phải transform.
 *   - publish chỉ chuyển HỘP BẨN (hợp của rect khung trước và khung này) và báo lại
 *     qua lvgl_gif_last_dirty() để display.c lv_obj_invalidate_area() đúng vùng đó
 *     thay vì set_src (set_src = vẽ lại + PPA xoay cả 800×480 mỗi khung).
 *   - loop / once + done_cb: clip "phản ứng" (emo_*, idle_*, boot_up…) chạy một
 *     lần rồi báo xong để display.c quay về mặt nền.
 */
#pragma once

#include "gifdec.h"
#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lvgl_gif_t lvgl_gif_t;
typedef void (*lvgl_gif_frame_cb_t)(void *user_data);
typedef void (*lvgl_gif_done_cb_t)(void *user_data);

typedef struct {
    uint8_t  scale;          /* 0/1 = 1:1 như cũ; 2..4 = nhân pixel lúc publish */
    bool     opaque;         /* true = RGB565 không alpha, 1 buffer, publish theo hộp bẩn */
    uint16_t min_frame_ms;   /* 0 = LVGL_GIF_MIN_FRAME_MS (150) */
} lvgl_gif_opts_t;

/**
 * Mở GIF từ image descriptor (data + data_size).
 * Trả về NULL nếu decode header thất bại.
 * Render frame đầu tiên ngay vào canvas → image_dsc() usable ngay.
 */
lvgl_gif_t *lvgl_gif_create(const lv_image_dsc_t *src_dsc);

/** Như lvgl_gif_create nhưng có tuỳ chọn (NULL = mặc định = lvgl_gif_create). */
lvgl_gif_t *lvgl_gif_create_ex(const lv_image_dsc_t *src_dsc, const lvgl_gif_opts_t *opts);

/**
 * Destroy controller: stop timer, close gifdec, free canvas.
 * MUST gọi khi LVGL lock đã held (timer_delete đụng LVGL internal list).
 */
void lvgl_gif_destroy(lvgl_gif_t *gif);

/**
 * ARGB8888 descriptor trỏ vào canvas của gifdec. Pointer stable trong
 * suốt life của controller — display chỉ cần set_src 1 lần, sau đó
 * frame_cb chỉ cần invalidate (set_src lại cũng OK, LVGL detect same ptr).
 */
const lv_image_dsc_t *lvgl_gif_image_dsc(const lvgl_gif_t *gif);

bool lvgl_gif_is_loaded(const lvgl_gif_t *gif);

/**
 * Start (hoặc restart) animation. Idempotent — gọi nhiều lần OK.
 */
void lvgl_gif_start(lvgl_gif_t *gif);

/**
 * Stop animation + rewind về frame đầu. Timer paused (giữ lại để start lại).
 */
void lvgl_gif_stop(lvgl_gif_t *gif);

/** Tạm dừng tại khung hiện tại (không rewind). lvgl_gif_start() chạy tiếp. */
void lvgl_gif_pause(lvgl_gif_t *gif);

/**
 * Frame callback chạy trên LVGL task (timer context). Dùng để trigger
 * widget refresh. NULL = no-op.
 */
void lvgl_gif_set_frame_cb(lvgl_gif_t *gif, lvgl_gif_frame_cb_t cb, void *user_data);

/**
 * true (mặc định) = lặp theo NETSCAPE loop của file (0 = vô hạn); false = chạy đúng
 * một lượt rồi dừng ở khung cuối và gọi done_cb. Đặt lại được giữa chừng.
 */
void lvgl_gif_set_loop(lvgl_gif_t *gif, bool loop);

/** Gọi (trên LVGL task) khi clip không lặp chạy hết. Không gọi khi loop = true. */
void lvgl_gif_set_done_cb(lvgl_gif_t *gif, lvgl_gif_done_cb_t cb, void *user_data);

/**
 * Hộp bẩn của lần publish gần nhất, toạ độ pixel của ảnh ĐÃ scale (0-based, x2/y2
 * inclusive như lv_area_t). false nếu chưa có khung nào. Chỉ có nghĩa ở opaque.
 */
bool lvgl_gif_last_dirty(const lvgl_gif_t *gif, lv_area_t *out);

/** Thống kê từ lần gọi trước (rồi xoá): số khung đã publish, tổng µs decode+publish,
 * tổng pixel (đã scale) của hộp bẩn. Để log "face 10s:" đo chi phí trên board. */
void lvgl_gif_take_stats(lvgl_gif_t *gif, uint32_t *frames, uint32_t *us, uint32_t *dirty_px);

#ifdef __cplusplus
}
#endif
