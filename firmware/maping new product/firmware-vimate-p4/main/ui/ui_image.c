/**
 * ui_image.c — show lesson image full màn.
 *
 * Refactored theo xiaozhi pattern: download image (SPIFFS hoặc HTTP) →
 * JPG decode RGB565, PNG giao cho LVGL LodePNG decoder → preview overlay.
 *
 * KHÔNG còn lv_async_call (race với LVGL flush). KHÔNG còn lv_layer_top.
 * Tất cả chạy qua display schedule queue → drain trong display task.
 *
 * Buffer ownership: static s_rgb_buf — free trước khi assign mới. Display
 * task drain serial → khi receive dsc mới đã chắc dsc cũ ko còn refer.
 */
#include "ui_image.h"
#include "network/http_dl.h"
#include "display.h"
#include "vimate.h"
#include "store/course_media_cache.h"
#include "store/lesson_image_cache.h"
#include "core/task_profile.h"
#include "boards/board.h"
#include "emo/vimate_emotions.h"
#include "util/jpeg_to_image.h"
#include "util/url_rewrite.h"
#include "lvgl.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

static uint8_t       *s_rgb_buf = NULL;
static uint8_t       *s_raw_buf = NULL;
static lv_image_dsc_t s_dsc = {0};
static SemaphoreHandle_t s_image_lock;
static QueueHandle_t s_image_queue;
static TaskHandle_t s_image_worker;

#define UI_IMAGE_JOB_URL_MAX 384
/* Budget được đo BÊN TRONG img_worker, sau khi stack 8KB đã cấp. 24KB tại đây
 * tương đương ngưỡng 32KB trước khi tạo worker; 32KB cũ khiến mọi ảnh HTTP bị
 * hoãn vĩnh viễn dù heap trước worker đủ an toàn. */
#define UI_IMAGE_HTTP_MIN_INTERNAL (24 * 1024)
#define UI_IMAGE_HTTP_MIN_LARGEST  (12 * 1024)
#define UI_IMAGE_SLIDESHOW_WORKER_MIN_INTERNAL (24 * 1024)
#define UI_IMAGE_SLIDESHOW_WORKER_MIN_LARGEST  (12 * 1024)
#define UI_IMAGE_WORKER_IDLE_EXIT_MS 5000
/* 6144 cũ TRÀN STACK → reset khi vào bài học load hình: img_worker chạy NỐI
 * TIẾP trên cùng stack: HTTPS download (mbedTLS handshake + cert-bundle + ECC
 * phần mềm ~5KB) RỒI JPEG SW decode. Trên EDU cache SD tắt → MỌI ảnh đều
 * HTTP-fallback → luôn tràn. Dùng VIMATE_TASK_STACK_IMAGE(8192) cho
 * đúng path này. */
#define UI_IMAGE_WORKER_STACK VIMATE_TASK_STACK_IMAGE
#define UI_IMAGE_WORKER_PRIORITY 3
#define UI_IMAGE_MAX_DOWNLOAD_BYTES (512 * 1024)

static char s_current_url[UI_IMAGE_JOB_URL_MAX];
/* Bìa carousel Home có KHO RIÊNG (13/09/2026): trước đây bìa dùng chung s_rgb_buf với
 * ảnh bài học, nên mọi lần chạm điều hướng (stop_active_program_for_navigation →
 * ui_image_hide) free luôn buffer mà widget bìa đang trỏ vào → bìa nháy/rác, rồi
 * apply_home ẩn bìa, tải + decode lại (~150 ms placeholder) — người dùng thấy "load
 * lại cả màn". Kho riêng: ui_image_hide không đụng; cùng URL → dùng lại tức thì;
 * chỉ free khi bìa KHÁC thay thế (sau khi display đã đổi src). 204×288 RGB565 ≈ 117 KB PSRAM. */
static uint8_t       *s_cover_rgb = NULL;
static uint8_t       *s_cover_raw = NULL;
static lv_image_dsc_t s_cover_dsc = {0};
static char           s_cover_url[UI_IMAGE_JOB_URL_MAX];      /* bìa đang giữ trong kho */
static char           s_cover_job_url[UI_IMAGE_JOB_URL_MAX];  /* job bìa đang decode */
/* Cờ của job đang xử lý (worker tuần tự, 1 job/lần -> an toàn không khóa).
 * swap_preview_image/đường tái dùng đọc cờ này để báo display ảnh là frame
 * slideshow hay nội dung thường. */
static bool s_cur_as_slideshow;
static bool s_cur_as_home_cover;

static void hdmi_slideshow_bandwidth_pause(void) {}
static bool decoded_cache_owns_ptr(const uint8_t *ptr) {
    (void)ptr;
    return false;
}
static void decoded_cache_put(const char *url, uint8_t *rgb, const lv_image_dsc_t *dsc) {
    (void)url;
    (void)rgb;
    (void)dsc;
}

/* Cache ảnh PSRAM (LRU 8 ảnh / 3MB) — bật cho VIMATE Edu. Không
 * cache → slideshow màn-chờ tải lại 4 ảnh MỖI 5s = mở TLS-2 liên tục chồng WS-TLS → kiệt
 * RAM nội (~15KB) → starve task WS → rớt 1006 ~294s khi idle. Cache PSRAM (7MB free) →
 * sau vòng đầu (4 ảnh) là cache-HIT, hết TLS-2 lặp → hết crash. Đây là gốc crash ~5p. */
#define UI_IMAGE_MEM_CACHE_ENABLE 1
#if UI_IMAGE_MEM_CACHE_ENABLE
static void image_cache_key(const char *url, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (!url) {
        out[0] = '\0';
        return;
    }
    strlcpy(out, url, out_size);
}

#define UI_IMAGE_MEM_CACHE_MAX_ITEMS 8
#define UI_IMAGE_MEM_CACHE_MAX_BYTES (3 * 1024 * 1024)
#define UI_IMAGE_MEM_CACHE_MAX_ITEM  (1024 * 1024)

typedef struct {
    char url[UI_IMAGE_JOB_URL_MAX];
    uint8_t *data;
    int len;
    uint32_t tick;
} ui_image_mem_cache_item_t;

static ui_image_mem_cache_item_t s_mem_cache[UI_IMAGE_MEM_CACHE_MAX_ITEMS];
static size_t s_mem_cache_bytes;
static uint32_t s_mem_cache_tick;

static void mem_cache_clear_slot(int idx) {
    if (idx < 0 || idx >= UI_IMAGE_MEM_CACHE_MAX_ITEMS) return;
    if (s_mem_cache[idx].data) {
        heap_caps_free(s_mem_cache[idx].data);
        if (s_mem_cache_bytes >= (size_t)s_mem_cache[idx].len) {
            s_mem_cache_bytes -= (size_t)s_mem_cache[idx].len;
        } else {
            s_mem_cache_bytes = 0;
        }
    }
    memset(&s_mem_cache[idx], 0, sizeof(s_mem_cache[idx]));
}

static int mem_cache_find_oldest(void) {
    int oldest = 0;
    uint32_t tick = UINT32_MAX;
    for (int i = 0; i < UI_IMAGE_MEM_CACHE_MAX_ITEMS; i++) {
        if (!s_mem_cache[i].data) return i;
        if (s_mem_cache[i].tick < tick) {
            tick = s_mem_cache[i].tick;
            oldest = i;
        }
    }
    return oldest;
}

static bool mem_cache_get_copy(const char *url, uint8_t **out, int *out_len) {
    if (!url || !out || !out_len) return false;
    char key[UI_IMAGE_JOB_URL_MAX];
    image_cache_key(url, key, sizeof(key));
    for (int i = 0; i < UI_IMAGE_MEM_CACHE_MAX_ITEMS; i++) {
        if (!s_mem_cache[i].data || strcmp(s_mem_cache[i].url, key) != 0) continue;
        uint8_t *copy = heap_caps_malloc(s_mem_cache[i].len, MALLOC_CAP_SPIRAM);
        if (!copy) copy = heap_caps_malloc(s_mem_cache[i].len, MALLOC_CAP_8BIT);
        if (!copy) return false;
        memcpy(copy, s_mem_cache[i].data, s_mem_cache[i].len);
        s_mem_cache[i].tick = ++s_mem_cache_tick;
        *out = copy;
        *out_len = s_mem_cache[i].len;
        ESP_LOGI(TAG_UI, "ui_image: mem cache hit (%d bytes)", *out_len);
        return true;
    }
    return false;
}

static void mem_cache_put(const char *url, const uint8_t *data, int len) {
    if (!url || !url[0] || !data || len <= 0 || len > UI_IMAGE_MEM_CACHE_MAX_ITEM) {
        return;
    }
    char key[UI_IMAGE_JOB_URL_MAX];
    image_cache_key(url, key, sizeof(key));
    for (int i = 0; i < UI_IMAGE_MEM_CACHE_MAX_ITEMS; i++) {
        if (s_mem_cache[i].data && strcmp(s_mem_cache[i].url, key) == 0) {
            return;
        }
    }
    while (s_mem_cache_bytes + (size_t)len > UI_IMAGE_MEM_CACHE_MAX_BYTES) {
        mem_cache_clear_slot(mem_cache_find_oldest());
    }
    int slot = mem_cache_find_oldest();
    mem_cache_clear_slot(slot);
    uint8_t *copy = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!copy) copy = heap_caps_malloc(len, MALLOC_CAP_8BIT);
    if (!copy) {
        ESP_LOGW(TAG_UI, "ui_image: mem cache alloc failed len=%d", len);
        return;
    }
    memcpy(copy, data, len);
    strlcpy(s_mem_cache[slot].url, key, sizeof(s_mem_cache[slot].url));
    s_mem_cache[slot].data = copy;
    s_mem_cache[slot].len = len;
    s_mem_cache[slot].tick = ++s_mem_cache_tick;
    s_mem_cache_bytes += (size_t)len;
    ESP_LOGI(TAG_UI, "ui_image: mem cached %d bytes total=%u",
             len, (unsigned)s_mem_cache_bytes);
}
#endif

typedef struct {
    char url[UI_IMAGE_JOB_URL_MAX];
    bool as_slideshow; /* true = frame của màn chờ slideshow (giữ chế độ nền đồng hồ) */
    bool as_home_cover; /* true = bìa carousel Home, render vào widget bìa riêng */
} ui_image_job_t;

static void ui_image_worker_task(void *arg) {
    (void)arg;
    ui_image_job_t job;
    ESP_LOGI(TAG_UI, "ui_image worker ready stack=%u priority=%u",
             (unsigned)UI_IMAGE_WORKER_STACK, (unsigned)UI_IMAGE_WORKER_PRIORITY);
    for (;;) {
        if (xQueueReceive(s_image_queue, &job,
                          pdMS_TO_TICKS(UI_IMAGE_WORKER_IDLE_EXIT_MS)) == pdTRUE) {
            if (job.url[0]) {
                s_cur_as_slideshow = job.as_slideshow;
                s_cur_as_home_cover = job.as_home_cover;
                ui_image_show(job.url);
            }
        } else {
            ESP_LOGI(TAG_UI, "ui_image worker idle exit");
            __sync_bool_compare_and_swap(&s_image_worker,
                                         xTaskGetCurrentTaskHandle(), NULL);
            vTaskDelete(NULL);
        }
    }
}

esp_err_t ui_image_worker_start(void) {
    if (!s_image_queue) {
        QueueHandle_t q = xQueueCreate(1, sizeof(ui_image_job_t));
        if (!q) {
            ESP_LOGE(TAG_UI, "ui_image queue create failed");
            return ESP_ERR_NO_MEM;
        }
        if (!__sync_bool_compare_and_swap(&s_image_queue, NULL, q)) {
            vQueueDelete(q);
        }
    }
    if (!s_image_worker) {
        BaseType_t r = xTaskCreatePinnedToCore(ui_image_worker_task,
            "img_worker", UI_IMAGE_WORKER_STACK, NULL, UI_IMAGE_WORKER_PRIORITY,
            &s_image_worker, VIMATE_TASK_CORE_IO);
        if (r != pdPASS) {
            ESP_LOGE(TAG_UI, "ui_image worker create failed ret=%d internal=%u largest=%u",
                     r,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            s_image_worker = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static bool ui_image_enqueue(const char *url, bool as_slideshow, bool as_home_cover) {
    if (!url || !url[0]) return false;
    if (as_slideshow && !s_image_worker) {
        uint32_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        if (internal < UI_IMAGE_SLIDESHOW_WORKER_MIN_INTERNAL ||
            largest < UI_IMAGE_SLIDESHOW_WORKER_MIN_LARGEST) {
            ESP_LOGW(TAG_UI,
                     "slideshow image deferred: internal=%u largest=%u url=%s",
                     (unsigned)internal, (unsigned)largest, url);
            return false;
        }
    }
    if (ui_image_worker_start() != ESP_OK || !s_image_queue) {
        return false;
    }
    ui_image_job_t job = {0};
    strlcpy(job.url, url, sizeof(job.url));
    job.as_slideshow = as_slideshow;
    job.as_home_cover = as_home_cover;
    xQueueOverwrite(s_image_queue, &job);
    return true;
}

bool ui_image_show_async_ex(const char *url, bool as_slideshow) {
    return ui_image_enqueue(url, as_slideshow, false);
}

bool ui_image_show_async(const char *url) {
    return ui_image_show_async_ex(url, false);
}

bool ui_image_home_cover_ready(const char *url) {
    if (!url || !url[0] || !s_cover_url[0] || !(s_cover_rgb || s_cover_raw)) return false;
    char key[UI_IMAGE_JOB_URL_MAX];
    image_cache_key(url, key, sizeof(key));
    return strcmp(key, s_cover_url) == 0;
}

bool ui_image_show_home_cover_async(const char *url) {
    return ui_image_enqueue(url, false, true);
}

static bool lock_image_renderer(void) {
    if (!s_image_lock) {
        SemaphoreHandle_t m = xSemaphoreCreateMutex();
        if (!m) return false;
        if (!__sync_bool_compare_and_swap(&s_image_lock, NULL, m)) {
            vSemaphoreDelete(m);
        }
    }
    return xSemaphoreTake(s_image_lock, pdMS_TO_TICKS(2500)) == pdTRUE;
}

static bool is_png_bytes(const uint8_t *data, int len) {
    static const uint8_t magic[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    return data && len >= (int)sizeof(magic) && memcmp(data, magic, sizeof(magic)) == 0;
}

static bool is_jpeg_bytes(const uint8_t *data, int len) {
    return data && len >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff;
}

static void free_image_buffer(uint8_t *buf) {
    if (buf) {
        if (decoded_cache_owns_ptr(buf)) {
            return;
        }
        heap_caps_free(buf);
    }
}

static void clear_previous_image_buffers(void) {
    bool had_buffer = s_rgb_buf != NULL || s_raw_buf != NULL;
    display_show_preview_image(NULL);
    s_current_url[0] = '\0';
    /* Nhường display task/LCD flush unhook src cũ trước khi free buffer.
     * ST7796S (SPI) full-screen flush cần ~600ms; DSI/PPA trên P4 ~16 ms
     * (BOARD_UI_IMAGE_UNHOOK_MS). KHÔNG ngủ khi không có buffer: hàm này nằm trên
     * đường chạm điều hướng (stop_active_program_for_navigation → ui_image_hide),
     * ngủ vô điều kiện = mỗi lần chạm trễ thêm 0,6 s (đo 13/09/2026). */
    if (had_buffer) {
        vTaskDelay(pdMS_TO_TICKS(BOARD_UI_IMAGE_UNHOOK_MS));
    }
    free_image_buffer(s_rgb_buf);
    free_image_buffer(s_raw_buf);
    s_rgb_buf = NULL;
    s_raw_buf = NULL;
}

static void show_decoded_image(const lv_image_dsc_t *dsc) {
    if (s_cur_as_home_cover) {
        display_show_home_cover_image(s_current_url, dsc);
    } else {
        display_show_preview_image_ex(dsc, s_cur_as_slideshow);
    }
}

static void swap_preview_image(uint8_t *new_rgb, uint8_t *new_raw, const lv_image_dsc_t *new_dsc) {
    if (s_cur_as_home_cover) {
        uint8_t *old_rgb = s_cover_rgb;
        uint8_t *old_raw = s_cover_raw;
        s_cover_rgb = new_rgb;
        s_cover_raw = new_raw;
        s_cover_dsc = *new_dsc;
        strlcpy(s_cover_url, s_cover_job_url, sizeof(s_cover_url));
        display_show_home_cover_image(s_cover_url, &s_cover_dsc);
        if (old_rgb || old_raw) {
            /* Display task đổi src qua hàng đợi; chờ 1 flush rồi mới free bìa cũ. */
            vTaskDelay(pdMS_TO_TICKS(BOARD_UI_IMAGE_UNHOOK_MS));
            free_image_buffer(old_rgb);
            free_image_buffer(old_raw);
        }
        return;
    }
    uint8_t *old_rgb = s_rgb_buf;
    uint8_t *old_raw = s_raw_buf;
    s_rgb_buf = new_rgb;
    s_raw_buf = new_raw;
    s_dsc = *new_dsc;
    show_decoded_image(&s_dsc);
    /* Không clear overlay trước khi ảnh mới sẵn sàng: tránh nháy xanh/đen giữa
     * ảnh bài học/banner. Giữ buffer cũ đủ lâu để panel không đọc vùng vừa free.
     * P4 HDMI flush DMA gần instant; SPI cần ~600ms. */
    vTaskDelay(pdMS_TO_TICKS(600));
    free_image_buffer(old_rgb);
    free_image_buffer(old_raw);
}

/* Client HTTPS BỀN (keep-alive) tái dùng cho ảnh: download_image chỉ chạy trong
 * img_worker task (tuần tự, ui_card_show đã chết) → không cần khóa. Ảnh thứ 2+
 * cùng host BỎ QUA bắt tay TLS (mbedTLS + ECC mềm ~tốn nhất mỗi ảnh). Lỗi bất kỳ
 * → cleanup + reset NULL để lần sau init sạch (tự lành). */
static esp_http_client_handle_t s_img_http;

static bool image_http_budget_ok(const char *url) {
    uint32_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (internal >= UI_IMAGE_HTTP_MIN_INTERNAL &&
        largest >= UI_IMAGE_HTTP_MIN_LARGEST) {
        return true;
    }
    ESP_LOGW(TAG_UI,
             "image HTTP deferred: internal=%u largest=%u min_internal=%u min_largest=%u url=%s",
             (unsigned)internal, (unsigned)largest,
             (unsigned)UI_IMAGE_HTTP_MIN_INTERNAL,
             (unsigned)UI_IMAGE_HTTP_MIN_LARGEST,
             url ? url : "(null)");
    return false;
}

static int download_image(const char *url, char **out_buf) {
    *out_buf = NULL;
    if (!s_img_http) {
        esp_http_client_config_t cfg = {
            .url = url,
            .timeout_ms = 15000,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .keep_alive_enable = false,
        };
        s_img_http = esp_http_client_init(&cfg);
        if (!s_img_http) return -1;
    } else {
        esp_http_client_set_url(s_img_http, url);
    }
    esp_http_client_handle_t cli = s_img_http;
    if (esp_http_client_open(cli, 0) != ESP_OK) {
        esp_http_client_cleanup(cli);
        s_img_http = NULL;
        return -1;
    }
    int total = esp_http_client_fetch_headers(cli);
    int status = esp_http_client_get_status_code(cli);
    if (status != 200 || total > UI_IMAGE_MAX_DOWNLOAD_BYTES) {
        ESP_LOGE(TAG_UI, "http status=%d len=%d max=%d (URL %s)",
                 status, total, UI_IMAGE_MAX_DOWNLOAD_BYTES, url);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        s_img_http = NULL;
        return -1;
    }
    int cap = (total > 0) ? total : (32 * 1024);
    char *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) buf = heap_caps_malloc(cap, MALLOC_CAP_8BIT);
    if (!buf) {
        esp_http_client_close(cli); // giữ client cho lần sau
        return -1;
    }
    int got = 0;
    while (total <= 0 || got < total) {
        vTaskDelay(pdMS_TO_TICKS(1)); // Yield to prevent TWDT if network is extremely fast
        if (got == cap) {
            int new_cap = cap + (32 * 1024);
            if (new_cap > UI_IMAGE_MAX_DOWNLOAD_BYTES) break;
            char *grown = heap_caps_realloc(buf, new_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!grown) grown = heap_caps_realloc(buf, new_cap, MALLOC_CAP_8BIT);
            if (!grown) break;
            buf = grown;
            cap = new_cap;
        }
        int to_read = (total > 0) ? (total - got) : (cap - got);
        int n = esp_http_client_read(cli, buf + got, to_read);
        if (n <= 0) break;
        got += n;
    }
    esp_http_client_close(cli);
    if ((total > 0 && got != total) || got <= 0) {
        esp_http_client_cleanup(cli); // đọc dở → kết nối lệch, bỏ client cho lần init sạch
        s_img_http = NULL;
        heap_caps_free(buf);
        return -1;
    }
    /* EDU S3 internal RAM rất sát. Không giữ HTTP/TLS client sống sau ảnh,
     * vì phần heap đó đủ làm WiFi/WS thiếu buffer và rớt 1006 khi idle. */
    esp_http_client_cleanup(cli);
    s_img_http = NULL;
    *out_buf = buf;
    return got;
}

static bool render_jpg_bytes(const uint8_t *jpg, int jpg_len) {
    uint8_t *rgb = NULL;
    size_t rgb_len = 0, w = 0, h = 0, stride = 0;
    jpeg_to_image_format_t fmt = JPEG_TO_IMAGE_RGB565;
    lv_color_format_t lv_cf = LV_COLOR_FORMAT_RGB565;
    esp_err_t r = jpeg_to_image_ex(jpg, jpg_len, fmt, &rgb, &rgb_len, &w, &h, &stride);
    if (r != ESP_OK || !rgb) {
        ESP_LOGE(TAG_UI, "jpeg_to_image fail %s", esp_err_to_name(r));
        return false;
    }
    lv_image_dsc_t dsc = {0};
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = lv_cf;
    dsc.header.w = w;
    dsc.header.h = h;
    dsc.header.stride = stride;
    dsc.data_size = rgb_len;
    dsc.data = rgb;
    decoded_cache_put(s_current_url, rgb, &dsc);
    /* Step 3: enqueue mới — display task drain sẽ set_src + start hide_timer. */
    swap_preview_image(rgb, NULL, &dsc);
    ESP_LOGI(TAG_UI, "ui_image: previewed %ux%u stride=%u",
             (unsigned)w, (unsigned)h, (unsigned)stride);
    return true;
}

static bool render_png_bytes_take(uint8_t *png, int png_len) {
    if (!png || png_len <= 0) return false;
    lv_image_dsc_t dsc = {0};
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    /* Để UNKNOWN để bin_decoder bỏ qua, LodePNG sẽ đọc magic PNG và set w/h/cf. */
    dsc.header.cf = LV_COLOR_FORMAT_UNKNOWN;
    dsc.data_size = png_len;
    dsc.data = png;
    swap_preview_image(NULL, png, &dsc);
    ESP_LOGI(TAG_UI, "ui_image: previewed PNG raw (%d bytes)", png_len);
    return true;
}

static bool render_image_bytes_take(uint8_t **buf, int len) {
    if (!buf || !*buf || len <= 0) return false;
    if (is_png_bytes(*buf, len)) {
        uint8_t *owned = *buf;
        *buf = NULL;
        return render_png_bytes_take(owned, len);
    }
    if (is_jpeg_bytes(*buf, len)) {
        return render_jpg_bytes(*buf, len);
    }
    ESP_LOGE(TAG_UI, "unsupported image format (%d bytes)", len);
    return false;
}

bool ui_image_show(const char *url) {
    ESP_LOGI(TAG_UI, "ui_image_show: %s", url ? url : "(null)");
    if (!url || !url[0]) return false;
    if (!lock_image_renderer()) {
        ESP_LOGW(TAG_UI, "ui_image: renderer busy, drop image");
        return false;
    }

    char cache_key[UI_IMAGE_JOB_URL_MAX];
    image_cache_key(url, cache_key, sizeof(cache_key));

    if (s_cur_as_home_cover) {
        if (s_cover_url[0] && strcmp(s_cover_url, cache_key) == 0 &&
            (s_cover_rgb || s_cover_raw) && s_cover_dsc.data) {
            display_show_home_cover_image(s_cover_url, &s_cover_dsc);
            ESP_LOGI(TAG_UI, "ui_image: home cover reuse (no decode)");
            xSemaphoreGive(s_image_lock);
            return true;
        }
    } else if (s_current_url[0] && strcmp(s_current_url, cache_key) == 0 &&
        (s_rgb_buf || s_raw_buf) && s_dsc.data) {
        hdmi_slideshow_bandwidth_pause();
        show_decoded_image(&s_dsc);
        ESP_LOGI(TAG_UI, "ui_image: same image already decoded, reusing preview");
        xSemaphoreGive(s_image_lock);
        return true;
    }

    /* s_current_url chỉ mô tả kho ảnh bài học (reuse + hide); job bìa ghi kho riêng. */
    if (s_cur_as_home_cover) strlcpy(s_cover_job_url, cache_key, sizeof(s_cover_job_url));
    else strlcpy(s_current_url, cache_key, sizeof(s_current_url));

    char fetch_url_buf[200];
    const char *fetch_url = url_rewrite_localhost_media(url, fetch_url_buf, sizeof(fetch_url_buf));

    char fs_path[192];
    uint8_t *img = NULL;
    int img_len = 0;
    bool ok = false;
    if (
#if UI_IMAGE_MEM_CACHE_ENABLE
        mem_cache_get_copy(url, &img, &img_len) ||
#endif
        course_media_cache_path(cache_key, fs_path, sizeof(fs_path)) ||
        course_media_cache_path(url, fs_path, sizeof(fs_path))
        || lesson_image_cache_path(url, fs_path, sizeof(fs_path)) /* EDU-only: SPIFFS lesson cache */
        ) {
        if (!img) {
            FILE *fp = fopen(fs_path, "rb");
            if (fp) {
                struct stat st;
                stat(fs_path, &st);
                img = heap_caps_malloc(st.st_size, MALLOC_CAP_SPIRAM);
                if (!img) img = heap_caps_malloc(st.st_size, MALLOC_CAP_8BIT);
                if (img) img_len = fread(img, 1, st.st_size, fp);
                fclose(fp);
            }
            if (img_len <= 0) {
                if (img) heap_caps_free(img);
                img = NULL;
            } else {
                ESP_LOGI(TAG_UI, "ui_image: cache hit %s (%d bytes)", fs_path, img_len);
#if UI_IMAGE_MEM_CACHE_ENABLE
                mem_cache_put(url, img, img_len);
#endif
            }
        }
    }
    if (!img) {
        if (!image_http_budget_ok(fetch_url)) {
            xSemaphoreGive(s_image_lock);
            return false;
        }
        ESP_LOGW(TAG_UI, "image cache miss — HTTP fallback: %s", fetch_url);
        char *downloaded = NULL;
        img_len = download_image(fetch_url, &downloaded);
        img = (uint8_t *)downloaded;
        if (img_len <= 0) {
            ESP_LOGE(TAG_UI, "ui_image: download failed");
            xSemaphoreGive(s_image_lock);
            return false;
        }
#if UI_IMAGE_MEM_CACHE_ENABLE
        mem_cache_put(url, img, img_len);
#endif
    }
    hdmi_slideshow_bandwidth_pause();
    ok = render_image_bytes_take(&img, img_len);
    if (!ok) {
        ESP_LOGE(TAG_UI, "ui_image: render failed");
    }
    if (img) heap_caps_free(img);
    xSemaphoreGive(s_image_lock);
    return ok;
}

void ui_image_show_jpg_buffer(const uint8_t *jpg_data, size_t jpg_len) {
    if (!jpg_data || jpg_len < 100) {
        ESP_LOGW(TAG_UI, "ui_image_show_jpg_buffer invalid (%u)", (unsigned)jpg_len);
        return;
    }
    s_cur_as_slideshow = false; /* ảnh WS-inline KHÔNG phải frame slideshow (không kế thừa cờ rác) */
    s_cur_as_home_cover = false;
    render_jpg_bytes(jpg_data, (int)jpg_len);
}

void ui_image_hide(void) {
    if (lock_image_renderer()) {
        clear_previous_image_buffers();
        xSemaphoreGive(s_image_lock);
        return;
    }
    s_current_url[0] = '\0';
    display_show_preview_image(NULL);
}

static void prefetch_task(void *arg) {
    char *url = (char *)arg;
    if (url) {
        char fetch_url_buf[200];
        const char *fetch_url = url_rewrite_localhost_media(url, fetch_url_buf, sizeof(fetch_url_buf));
        uint8_t *body = NULL;
        size_t len = 0;
        if (http_dl_get_to_buf(fetch_url, &body, &len) == ESP_OK) {
#if UI_IMAGE_MEM_CACHE_ENABLE
            mem_cache_put(url, body, (int)len);
#endif
        }
        if (body) {
            heap_caps_free(body);
        }
        free(url);
    }
    vTaskDelete(NULL);
}

void ui_image_prefetch(const char *url) {
    if (!url || !url[0]) return;
    
#if UI_IMAGE_MEM_CACHE_ENABLE
    uint8_t *temp = NULL;
    int temp_len = 0;
    if (mem_cache_get_copy(url, &temp, &temp_len)) {
        if (temp) heap_caps_free(temp);
        return; // Already in memory cache
    }
#endif
    
    char fs_path[192];
    if (course_media_cache_path(url, fs_path, sizeof(fs_path))) {
        return; // Already in disk cache
    }
    
    char *url_copy = strdup(url);
    if (url_copy) {
        xTaskCreatePinnedToCore(prefetch_task, "img_prefetch", 6144, url_copy,
                                tskIDLE_PRIORITY + 1, NULL, VIMATE_TASK_CORE_IO);
    }
}
