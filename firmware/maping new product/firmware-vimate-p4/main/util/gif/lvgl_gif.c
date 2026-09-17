/**
 * lvgl_gif.c — pure C port của xiaozhi's LvglGif class.
 * Xem lvgl_gif.h cho contract chi tiết.
 */
#include "lvgl_gif.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <stdlib.h>
#include <string.h>

#define TAG "LvglGif"

#ifndef LVGL_GIF_TIMER_PERIOD_MS
#define LVGL_GIF_TIMER_PERIOD_MS 20
#endif

#ifndef LVGL_GIF_MIN_FRAME_MS
#define LVGL_GIF_MIN_FRAME_MS 150
#endif

#ifndef LVGL_GIF_MAX_FRAME_MS
#define LVGL_GIF_MAX_FRAME_MS 1000
#endif

#define LVGL_GIF_FRAME_BUFFERS 3

struct lvgl_gif_t {
    gd_GIF              *gif;          /* gifdec handle (owns canvas) */
    lv_timer_t          *timer;        /* LVGL timer driver */
    lv_image_dsc_t       img_dsc[LVGL_GIF_FRAME_BUFFERS];   /* stable display frame descriptors */
    uint8_t             *frame_buf[LVGL_GIF_FRAME_BUFFERS]; /* RGB565A8 (hoặc RGB565 opaque) */
    int                  n_bufs;       /* 3 (alpha, xoay vòng) hoặc 1 (opaque) */
    int                  active_buf;
    uint32_t             last_call;    /* tick lần render trước */
    bool                 loaded;
    bool                 playing;
    bool                 loop;         /* false = một lượt rồi done_cb */
    bool                 completed;    /* clip một lượt đã chạy hết → start() không chạy lại */
    lvgl_gif_opts_t      opts;         /* scale ≥ 1, opaque, min_frame_ms > 0 */
    uint32_t             timer_ms;
    /* Hộp bẩn (opaque): rect khung trước để hợp với khung này khi publish. */
    bool                 have_prev;
    uint16_t             prev_fx, prev_fy, prev_fw, prev_fh;
    bool                 has_dirty;
    lv_area_t            dirty;        /* toạ độ ảnh đã scale */
    lvgl_gif_frame_cb_t  frame_cb;
    void                *frame_cb_user;
    lvgl_gif_done_cb_t   done_cb;
    void                *done_cb_user;
    uint32_t             st_frames, st_us, st_dirty_px;   /* lvgl_gif_take_stats */
};

/* Forward */
static void timer_cb(lv_timer_t *timer);
static void next_frame(lvgl_gif_t *g);
static uint32_t frame_delay_ms(const lvgl_gif_t *g);
static bool publish_frame(lvgl_gif_t *g);
static void opaque_render_hook(gd_GIF *gif, void *user, int fx, int fy, int fw, int fh, int fill_index);

static void *gif_frame_alloc(size_t bytes)
{
    /* Buffer khung > 4 KB (128² ×3 = 48 KB; mặt P4 800×480×2 = 768 KB) → PSRAM.
     * 64 B aligned: CPU LVGL đọc, PPA không đụng buffer này, nhưng cùng kỷ luật
     * cache line 64 B của P4 để khỏi nghĩ lại nếu sau này blit thẳng. */
    void *p = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_8BIT);
    return p;
}

lvgl_gif_t *lvgl_gif_create(const lv_image_dsc_t *src_dsc)
{
    return lvgl_gif_create_ex(src_dsc, NULL);
}

lvgl_gif_t *lvgl_gif_create_ex(const lv_image_dsc_t *src_dsc, const lvgl_gif_opts_t *opts)
{
    if (!src_dsc || !src_dsc->data) {
        ESP_LOGE(TAG, "Invalid image descriptor");
        return NULL;
    }

    lvgl_gif_t *g = (lvgl_gif_t *)calloc(1, sizeof(*g));
    if (!g) {
        ESP_LOGE(TAG, "alloc lvgl_gif_t failed");
        return NULL;
    }
    g->loop = true;
    if (opts) g->opts = *opts;
    if (g->opts.scale < 1) g->opts.scale = 1;
    if (g->opts.scale > 4) g->opts.scale = 4;
    if (g->opts.min_frame_ms == 0) g->opts.min_frame_ms = LVGL_GIF_MIN_FRAME_MS;
    /* Tick timer = 1/4 sàn khung (kẹp 5..20 ms): khung 50 ms mà tick 20 ms thì bị lượng
     * tử thành 60 (+20 %); mặt P4 sàn 40 → tick 10 ms, S3 sàn 150 → 20 ms như cũ. */
    g->timer_ms = g->opts.min_frame_ms / 4;
    if (g->timer_ms > LVGL_GIF_TIMER_PERIOD_MS) g->timer_ms = LVGL_GIF_TIMER_PERIOD_MS;
    if (g->timer_ms < 5) g->timer_ms = 5;
    g->n_bufs = g->opts.opaque ? 1 : LVGL_GIF_FRAME_BUFFERS;

    g->gif = gd_open_gif_data(src_dsc->data);
    if (!g->gif) {
        ESP_LOGE(TAG, "gd_open_gif_data failed");
        free(g);
        return NULL;
    }

    /* Không expose trực tiếp gif->canvas cho LVGL: LCD SPI có thể còn đang
     * flush frame cũ trong khi timer decode frame mới, tạo xé hình. Dùng
     * triple-buffer ổn định và giữ alpha để GIF trong suốt không bị nền đen.
     * Opaque (P4, nền GIF đen kín, decode + flush cùng LVGL task): MỘT buffer
     * RGB565 đã nhân scale, publish theo hộp bẩn. */
    const uint32_t out_w = (uint32_t)g->gif->width * g->opts.scale;
    const uint32_t out_h = (uint32_t)g->gif->height * g->opts.scale;
    const size_t pixels = (size_t)out_w * (size_t)out_h;
    const size_t rgb_bytes = pixels * 2;
    const size_t frame_bytes = g->opts.opaque ? rgb_bytes : rgb_bytes + pixels; /* + A8 plane */
    for (int i = 0; i < g->n_bufs; ++i) {
        g->frame_buf[i] = (uint8_t *)gif_frame_alloc(frame_bytes);
        if (!g->frame_buf[i]) {
            ESP_LOGE(TAG, "alloc GIF frame buffer failed (%u bytes)", (unsigned)frame_bytes);
            lvgl_gif_destroy(g);
            return NULL;
        }
        memset(&g->img_dsc[i], 0, sizeof(g->img_dsc[i]));
        g->img_dsc[i].header.magic  = LV_IMAGE_HEADER_MAGIC;
        g->img_dsc[i].header.flags  = LV_IMAGE_FLAGS_MODIFIABLE;
        g->img_dsc[i].header.cf     = g->opts.opaque ? LV_COLOR_FORMAT_RGB565
                                                     : LV_COLOR_FORMAT_RGB565A8;
        g->img_dsc[i].header.w      = out_w;
        g->img_dsc[i].header.h      = out_h;
        g->img_dsc[i].header.stride = out_w * 2;
        g->img_dsc[i].data          = g->frame_buf[i];
        g->img_dsc[i].data_size     = frame_bytes;
    }

    if (g->opts.opaque) {
        /* Vẽ thẳng index → RGB565 ×scale vào frame_buf[0], bỏ canvas ARGB trung gian
         * (đo 15/09: 24 → ~12 ms/khung). Buffer ra khởi đầu đen = nền bộ mặt. */
        memset(g->frame_buf[0], 0, frame_bytes);
        g->gif->render_hook = opaque_render_hook;
        g->gif->render_user = g;
    }

    /* Decode frame đầu trước khi render. Nếu render ngay sau gd_open_gif_data(),
     * GIF chưa đọc Graphic Control Extension nên transparent index bị tô thành
     * màu palette 0 (thường là đen) và nền đen kẹt lại dưới các frame sau. */
    if (g->gif->canvas && gd_get_frame(g->gif) == 1) {
        gd_render_frame(g->gif, g->gif->canvas);
        (void)publish_frame(g);
    }

    g->loaded = true;
    ESP_LOGD(TAG, "GIF loaded %dx%d -> %ux%u opaque=%d", g->gif->width, g->gif->height,
             (unsigned)out_w, (unsigned)out_h, (int)g->opts.opaque);
    return g;
}

void lvgl_gif_destroy(lvgl_gif_t *g)
{
    if (!g) return;
    if (g->timer) {
        lv_timer_delete(g->timer);
        g->timer = NULL;
    }
    if (g->gif) {
        gd_close_gif(g->gif);
        g->gif = NULL;
    }
    for (int i = 0; i < LVGL_GIF_FRAME_BUFFERS; ++i) {
        if (g->frame_buf[i]) {
            heap_caps_free(g->frame_buf[i]);
            g->frame_buf[i] = NULL;
        }
    }
    g->loaded  = false;
    g->playing = false;
    free(g);
}

const lv_image_dsc_t *lvgl_gif_image_dsc(const lvgl_gif_t *g)
{
    if (!g || !g->loaded) return NULL;
    return &g->img_dsc[g->active_buf % g->n_bufs];
}

bool lvgl_gif_is_loaded(const lvgl_gif_t *g)
{
    return g && g->loaded;
}

void lvgl_gif_set_frame_cb(lvgl_gif_t *g, lvgl_gif_frame_cb_t cb, void *user_data)
{
    if (!g) return;
    g->frame_cb       = cb;
    g->frame_cb_user  = user_data;
}

void lvgl_gif_set_done_cb(lvgl_gif_t *g, lvgl_gif_done_cb_t cb, void *user_data)
{
    if (!g) return;
    g->done_cb      = cb;
    g->done_cb_user = user_data;
}

void lvgl_gif_set_loop(lvgl_gif_t *g, bool loop)
{
    if (!g) return;
    g->loop = loop;
}

bool lvgl_gif_last_dirty(const lvgl_gif_t *g, lv_area_t *out)
{
    if (!g || !g->has_dirty) return false;
    if (out) *out = g->dirty;
    return true;
}

void lvgl_gif_take_stats(lvgl_gif_t *g, uint32_t *frames, uint32_t *us, uint32_t *dirty_px)
{
    if (!g) { if (frames) *frames = 0; if (us) *us = 0; if (dirty_px) *dirty_px = 0; return; }
    if (frames) *frames = g->st_frames;
    if (us) *us = g->st_us;
    if (dirty_px) *dirty_px = g->st_dirty_px;
    g->st_frames = g->st_us = g->st_dirty_px = 0;
}

void lvgl_gif_start(lvgl_gif_t *g)
{
    if (!g || !g->loaded || !g->gif) {
        ESP_LOGW(TAG, "start: not loaded");
        return;
    }
    /* Clip một lượt đã hết: giữ khung cuối (ONCE_HOLD), không tự chạy lại khi
     * pause/resume; muốn chạy lại thì stop() (rewind) rồi start(). */
    if (g->completed && !g->loop) return;
    if (!g->timer) {
        g->timer = lv_timer_create(timer_cb, g->timer_ms, g);
        if (!g->timer) {
            ESP_LOGE(TAG, "lv_timer_create failed");
            return;
        }
    }
    g->playing   = true;
    g->last_call = lv_tick_get();
    lv_timer_resume(g->timer);
    lv_timer_reset(g->timer);

    /* Render frame ngay (đỡ phải chờ 10ms tick đầu). */
    next_frame(g);
    ESP_LOGD(TAG, "animation started");
}

void lvgl_gif_pause(lvgl_gif_t *g)
{
    if (!g) return;
    g->playing = false;
    if (g->timer) lv_timer_pause(g->timer);
}

void lvgl_gif_stop(lvgl_gif_t *g)
{
    if (!g) return;
    if (g->timer) {
        g->playing = false;
        lv_timer_pause(g->timer);
    }
    g->completed = false;
    if (g->gif) {
        gd_rewind(g->gif);
        g->have_prev = false;   /* canvas về khung 1 → publish cả ảnh */
        if (g->gif->canvas && gd_get_frame(g->gif) == 1) {
            gd_render_frame(g->gif, g->gif->canvas);
            (void)publish_frame(g);
        }
    }
    ESP_LOGD(TAG, "animation stopped");
}

/* ===== Internal ===== */
static void timer_cb(lv_timer_t *timer)
{
    lvgl_gif_t *g = (lvgl_gif_t *)lv_timer_get_user_data(timer);
    if (g) next_frame(g);
}

static uint32_t frame_delay_ms(const lvgl_gif_t *g)
{
    if (!g || !g->gif) return LVGL_GIF_MIN_FRAME_MS;
    uint32_t delay = (uint32_t)g->gif->gce.delay * 10U;
    if (delay < g->opts.min_frame_ms) delay = g->opts.min_frame_ms;
    if (delay > LVGL_GIF_MAX_FRAME_MS) delay = LVGL_GIF_MAX_FRAME_MS;
    return delay;
}

/* Hook của gifdec (opaque): tô rect khung từ gif->frame (index) qua LUT palette →
 * RGB565, nhân scale, thẳng vào frame_buf[0]. fill_index >= 0 = disposal 2 (tô nền).
 * Pixel trong suốt bỏ qua → giữ nội dung cũ (delta frame disposal 1 của Pillow). */
static void opaque_render_hook(gd_GIF *gif, void *user, int fx, int fy, int fw, int fh, int fill_index)
{
    lvgl_gif_t *g = (lvgl_gif_t *)user;
    if (!g || !g->frame_buf[0] || fw <= 0 || fh <= 0) return;
    const uint32_t W = gif->width, s = g->opts.scale, OW = W * s;
    uint16_t lut[256];
    const int ncol = gif->palette->size > 256 ? 256 : gif->palette->size;
    for (int i = 0; i < ncol; ++i) {
        const uint8_t *c = &gif->palette->colors[i * 3];   /* R G B */
        lut[i] = (uint16_t)(((c[0] & 0xF8) << 8) | ((c[1] & 0xFC) << 3) | (c[2] >> 3));
    }
    const int transp = (fill_index < 0 && gif->gce.transparency) ? gif->gce.tindex : -1;
    const uint16_t fillv = fill_index >= 0 ? lut[fill_index & 0xFF] : 0;
    uint8_t *buf = g->frame_buf[0];
    for (int y = 0; y < fh; ++y) {
        const uint8_t *idx = &gif->frame[(size_t)(fy + y) * W + fx];
        uint16_t *dst = (uint16_t *)(buf + (((size_t)(fy + y) * s) * OW + (size_t)fx * s) * 2);
        if (s == 2) {
            uint32_t *d32 = (uint32_t *)dst;
            for (int x = 0; x < fw; ++x) {
                const int ix = idx[x];
                if (fill_index < 0 && ix == transp) { d32++; continue; }
                const uint32_t v = fill_index >= 0 ? fillv : lut[ix];
                *d32++ = v | (v << 16);
            }
        } else {
            uint16_t *d = dst;
            for (int x = 0; x < fw; ++x) {
                const int ix = idx[x];
                if (fill_index < 0 && ix == transp) { d += s; continue; }
                const uint16_t v = fill_index >= 0 ? fillv : lut[ix];
                for (uint32_t k = 0; k < s; ++k) *d++ = v;
            }
        }
        /* Nhân hàng: các hàng s-1 còn lại chép từ hàng đầu (kể cả pixel giữ nguyên —
         * hàng dưới của cùng pixel nguồn cũng đang giữ giá trị cũ giống hệt). */
        for (uint32_t k = 1; k < s; ++k) {
            memcpy((uint8_t *)dst + (size_t)k * OW * 2, dst, (size_t)fw * s * 2);
        }
    }
}

static inline uint16_t bgra_to_rgb565(const uint8_t *px)
{
    /* canvas gifdec: [0]=B [1]=G [2]=R [3]=A; xanh lá đủ 6 bit (0xFC). */
    return (uint16_t)(((px[2] & 0xF8) << 8) | ((px[1] & 0xFC) << 3) | (px[0] >> 3));
}

/* Opaque: pixel đã được opaque_render_hook ghi thẳng vào frame_buf[0] lúc
 * gd_render_frame; ở đây chỉ tính hộp bẩn (hợp rect khung trước ∪ khung này; cả ảnh
 * nếu chưa có khung trước) cho display.c invalidate. */
static bool publish_opaque(lvgl_gif_t *g)
{
    const gd_GIF *gif = g->gif;
    const uint32_t W = gif->width, H = gif->height, s = g->opts.scale;
    uint32_t x0, y0, x1, y1;   /* inclusive, toạ độ nguồn */
    if (!g->have_prev) {
        x0 = 0; y0 = 0; x1 = W - 1; y1 = H - 1;
    } else {
        x0 = gif->fx; y0 = gif->fy;
        x1 = (uint32_t)gif->fx + gif->fw - 1;
        y1 = (uint32_t)gif->fy + gif->fh - 1;
        if (g->prev_fx < x0) x0 = g->prev_fx;
        if (g->prev_fy < y0) y0 = g->prev_fy;
        uint32_t px1 = (uint32_t)g->prev_fx + g->prev_fw - 1;
        uint32_t py1 = (uint32_t)g->prev_fy + g->prev_fh - 1;
        if (px1 > x1) x1 = px1;
        if (py1 > y1) y1 = py1;
    }
    if (x1 >= W) x1 = W - 1;
    if (y1 >= H) y1 = H - 1;
    if (x0 > x1 || y0 > y1) return false;

    g->dirty.x1 = (int32_t)(x0 * s);
    g->dirty.y1 = (int32_t)(y0 * s);
    g->dirty.x2 = (int32_t)((x1 + 1) * s) - 1;
    g->dirty.y2 = (int32_t)((y1 + 1) * s) - 1;
    g->has_dirty = true;
    g->prev_fx = gif->fx; g->prev_fy = gif->fy;
    g->prev_fw = gif->fw; g->prev_fh = gif->fh;
    g->have_prev = true;
    g->active_buf = 0;
    return true;
}

static bool publish_frame(lvgl_gif_t *g)
{
    if (!g || !g->gif || !g->gif->canvas || !g->frame_buf[0]) {
        return false;
    }
    if (g->opts.opaque) return publish_opaque(g);

    int next = (g->active_buf + 1) % g->n_bufs;
    const uint8_t *src = g->gif->canvas;
    const size_t pixels = (size_t)g->gif->width * (size_t)g->gif->height;
    uint8_t *rgb = g->frame_buf[next];
    uint8_t *alpha = rgb + pixels * 2;
    for (size_t i = 0; i < pixels; ++i) {
        const uint8_t b = src[i * 4 + 0];
        const uint8_t g8 = src[i * 4 + 1];
        const uint8_t r = src[i * 4 + 2];
        const uint8_t a = src[i * 4 + 3];
        const uint16_t rgb565 = (uint16_t)(((r & 0xF8) << 8) |
                                           ((g8 & 0xF8) << 3) |
                                           (b >> 3));
        rgb[i * 2 + 0] = (uint8_t)(rgb565 & 0xFF);
        rgb[i * 2 + 1] = (uint8_t)(rgb565 >> 8);
        alpha[i] = a;
    }
    g->active_buf = next;
    return true;
}

static void next_frame(lvgl_gif_t *g)
{
    if (!g->loaded || !g->gif || !g->playing) return;

    /* gifdec stores delay in centiseconds (×10ms). Clamp quá thấp để tránh
     * cảm giác quét từng frame trên LCD SPI production. */
    uint32_t elapsed = lv_tick_elaps(g->last_call);
    if (elapsed < frame_delay_ms(g)) {
        return;
    }
    g->last_call = lv_tick_get();
    int64_t t0 = esp_timer_get_time();

    /* Ép chế độ lặp TRƯỚC mỗi lần đọc: gifdec chỉ đọc NETSCAPE loop khi loop_count < 0
     * (sau gd_rewind), còn ở trailer ';' nó trả 0 khi loop_count == 1 và chạy tiếp
     * khi == 0. Đặt ở đây nên đổi loop giữa chừng cũng có tác dụng ngay. */
    g->gif->loop_count = g->loop ? 0 : 1;

    int has_next = gd_get_frame(g->gif);
    if (has_next <= 0) {
        /* End-of-animation (non-looping) hoặc lỗi decode → pause, giữ khung cuối. */
        g->playing = false;
        g->completed = true;
        if (g->timer) lv_timer_pause(g->timer);
        if (has_next < 0) {
            ESP_LOGW(TAG, "gd_get_frame error -> stop");
        } else {
            ESP_LOGD(TAG, "animation completed");
        }
        if (g->done_cb) g->done_cb(g->done_cb_user);
        return;
    }

    if (g->gif->canvas) {
        gd_render_frame(g->gif, g->gif->canvas);
        if (publish_frame(g)) {
            g->st_frames++;
            g->st_us += (uint32_t)(esp_timer_get_time() - t0);
            if (g->has_dirty) g->st_dirty_px += (uint32_t)lv_area_get_size(&g->dirty);
            if (g->frame_cb) g->frame_cb(g->frame_cb_user);
        }
    }
}
