/**
 * course_media_cache.c — download active/owned course media to SD card.
 *
 * Hot path:
 *   server sends image URL -> course_media_cache_path(url) -> /sdcard/localPath.
 * If SD is unavailable or a file is missing, caller falls back to SPIFFS/HTTP.
 */
#include "course_media_cache.h"
#include "vimate.h"
#include "boards/board.h"
#include "core/task_profile.h"
#include "util/url_rewrite.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef CONFIG_VIMATE_SD_CACHE_ENABLE
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/sdmmc_host.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "boards/board.h"
#include "sdmmc_cmd.h"
#if defined(BOARD_SD_PWR_LDO_CHAN)
#include "esp_ldo_regulator.h"
#endif
#endif

#define COURSE_CACHE_BASE        "/sdcard"
#define COURSE_CACHE_MANIFEST    COURSE_CACHE_BASE "/vimate/course-cache.json"
#define COURSE_CACHE_MAX_JSON    (256 * 1024)
#define COURSE_CACHE_CHUNK       4096
#define COURSE_CACHE_SYNC_MIN_INTERNAL (48 * 1024)
#define COURSE_CACHE_SYNC_MIN_LARGEST  (24 * 1024)

#ifndef CONFIG_VIMATE_COURSE_CACHE_MAX_ASSETS
#define CONFIG_VIMATE_COURSE_CACHE_MAX_ASSETS 96
#endif

typedef struct {
    char *url;
    char *path;
} cache_item_t;

static bool s_ready = false;
static cache_item_t s_items[CONFIG_VIMATE_COURSE_CACHE_MAX_ASSETS];
static size_t s_item_count = 0;
static SemaphoreHandle_t s_lock;
static bool s_sync_running = false;

static void lock_items(void) {
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock_items(void) {
    if (s_lock) xSemaphoreGive(s_lock);
}

static void clear_items(void) {
    for (size_t i = 0; i < s_item_count; i++) {
        free(s_items[i].url);
        free(s_items[i].path);
        s_items[i].url = NULL;
        s_items[i].path = NULL;
    }
    s_item_count = 0;
}

static void add_item(const char *url, const char *path) {
    if (!url || !path || !url[0] || !path[0]) return;
    for (size_t i = 0; i < s_item_count; i++) {
        if (strcmp(s_items[i].url, url) == 0) return;
    }
    if (s_item_count >= CONFIG_VIMATE_COURSE_CACHE_MAX_ASSETS) {
        ESP_LOGW(TAG_CACHE, "course cache map full (%u)",
                 (unsigned)CONFIG_VIMATE_COURSE_CACHE_MAX_ASSETS);
        return;
    }
    s_items[s_item_count].url = strdup(url);
    s_items[s_item_count].path = strdup(path);
    if (s_items[s_item_count].url && s_items[s_item_count].path) {
        s_item_count++;
    } else {
        free(s_items[s_item_count].url);
        free(s_items[s_item_count].path);
        s_items[s_item_count].url = NULL;
        s_items[s_item_count].path = NULL;
    }
}

static void mkdir_p(const char *dir) {
    if (!dir || !dir[0]) return;
    char tmp[192];
    strlcpy(tmp, dir, sizeof(tmp));
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void mkdir_parent(const char *path) {
    char dir[192];
    strlcpy(dir, path, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (!slash) return;
    *slash = 0;
    mkdir_p(dir);
}

static bool file_exists_min(const char *path, size_t min_size) {
    struct stat st;
    return path && stat(path, &st) == 0 && st.st_size >= (off_t)min_size;
}

static bool looks_like_json(const uint8_t *body, size_t len) {
    if (!body || len == 0) return false;
    for (size_t i = 0; i < len; ++i) {
        if (isspace((unsigned char)body[i])) continue;
        return body[i] == '{' || body[i] == '[';
    }
    return false;
}

static void log_body_preview(const char *prefix, const uint8_t *body, size_t len) {
    char preview[81];
    size_t n = len < sizeof(preview) - 1 ? len : sizeof(preview) - 1;
    for (size_t i = 0; i < n; ++i) {
        uint8_t ch = body ? body[i] : 0;
        preview[i] = (ch >= 32 && ch <= 126) ? (char)ch : '.';
    }
    preview[n] = '\0';
    ESP_LOGW(TAG_CACHE, "%s len=%u head='%s'", prefix, (unsigned)len, preview);
}

static esp_err_t http_get_to_buf_auth(const char *url, const char *auth,
                                      uint8_t **out, size_t *out_len) {
    *out = NULL;
    *out_len = 0;
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 20000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return ESP_FAIL;
    if (auth && auth[0]) esp_http_client_set_header(cli, "Authorization", auth);
    esp_err_t err = esp_http_client_open(cli, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(cli);
        return err;
    }
    int total = esp_http_client_fetch_headers(cli);
    int status = esp_http_client_get_status_code(cli);
    if (status != 200) {
        ESP_LOGW(TAG_CACHE, "course manifest GET status=%d", status);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_FAIL;
    }

    int cap = (total > 0 && total < COURSE_CACHE_MAX_JSON) ? total : (16 * 1024);
    uint8_t *buf = heap_caps_malloc(cap + 1, MALLOC_CAP_SPIRAM);
    if (!buf) buf = malloc(cap + 1);
    if (!buf) {
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_ERR_NO_MEM;
    }

    int got = 0;
    while (1) {
        if (got == cap) {
            int new_cap = cap + (16 * 1024);
            if (new_cap > COURSE_CACHE_MAX_JSON) break;
            uint8_t *grown = heap_caps_realloc(buf, new_cap + 1, MALLOC_CAP_8BIT);
            if (!grown) break;
            buf = grown;
            cap = new_cap;
        }
        int n = esp_http_client_read(cli, (char *)buf + got, cap - got);
        if (n <= 0) break;
        got += n;
        if (total > 0 && got >= total) break;
    }
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    if (got <= 0) {
        free(buf);
        return ESP_FAIL;
    }
    buf[got] = 0;
    *out = buf;
    *out_len = got;
    return ESP_OK;
}

static esp_err_t http_get_to_file_auth(const char *url, const char *auth,
                                       const char *dest_path) {
    mkdir_parent(dest_path);

    char tmp_path[224];
    snprintf(tmp_path, sizeof(tmp_path), "%s.part", dest_path);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return ESP_FAIL;
    if (auth && auth[0]) esp_http_client_set_header(cli, "Authorization", auth);

    esp_err_t err = esp_http_client_open(cli, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(cli);
        return err;
    }
    (void)esp_http_client_fetch_headers(cli);
    int status = esp_http_client_get_status_code(cli);
    if (status != 200) {
        ESP_LOGW(TAG_CACHE, "asset GET status=%d url=%s", status, url);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_FAIL;
    }

    FILE *fp = fopen(tmp_path, "wb");
    if (!fp) {
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_FAIL;
    }
    uint8_t *buf = heap_caps_malloc(COURSE_CACHE_CHUNK,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = heap_caps_malloc(COURSE_CACHE_CHUNK, MALLOC_CAP_8BIT);
    if (!buf) {
        fclose(fp);
        unlink(tmp_path);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_ERR_NO_MEM;
    }
    int read_total = 0;
    while (1) {
        int n = esp_http_client_read(cli, (char *)buf, COURSE_CACHE_CHUNK);
        if (n < 0) {
            err = ESP_FAIL;
            break;
        }
        if (n == 0) break;
        if (fwrite(buf, 1, n, fp) != (size_t)n) {
            err = ESP_FAIL;
            break;
        }
        read_total += n;
    }
    if (!esp_http_client_is_complete_data_received(cli)) err = ESP_FAIL;
    heap_caps_free(buf);
    fclose(fp);
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    if (err != ESP_OK || read_total <= 0) {
        unlink(tmp_path);
        return ESP_FAIL;
    }
    unlink(dest_path);
    if (rename(tmp_path, dest_path) != 0) {
        unlink(tmp_path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static bool sha256_file_matches(const char *path, const char *expected_hex) {
    if (!expected_hex || !expected_hex[0]) return true;
    if (strlen(expected_hex) != 64) return true;
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    uint8_t buf[COURSE_CACHE_CHUNK];
    while (1) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (n > 0) mbedtls_sha256_update(&ctx, buf, n);
        if (n < sizeof(buf)) break;
    }
    fclose(fp);
    uint8_t sum[32];
    mbedtls_sha256_finish(&ctx, sum);
    mbedtls_sha256_free(&ctx);

    char hex[65];
    for (int i = 0; i < 32; i++) {
        snprintf(hex + i * 2, 3, "%02x", sum[i]);
    }
    return strcasecmp(hex, expected_hex) == 0;
}

static void local_path_for(const char *rel, char *out, size_t out_size) {
    if (!rel || !rel[0]) {
        out[0] = 0;
        return;
    }
    while (*rel == '/') rel++;
    snprintf(out, out_size, COURSE_CACHE_BASE "/%s", rel);
}

static void index_manifest(cJSON *root) {
    lock_items();
    clear_items();
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) data = root;
    cJSON *courses = cJSON_GetObjectItem(data, "courses");
    if (!cJSON_IsArray(courses)) {
        unlock_items();
        return;
    }

    cJSON *course;
    cJSON_ArrayForEach(course, courses) {
        cJSON *assets = cJSON_GetObjectItem(course, "assets");
        if (!cJSON_IsArray(assets)) continue;
        cJSON *asset;
        cJSON_ArrayForEach(asset, assets) {
            cJSON *url = cJSON_GetObjectItem(asset, "url");
            cJSON *local = cJSON_GetObjectItem(asset, "localPath");
            if (!cJSON_IsString(url) || !cJSON_IsString(local)) continue;
            char path[192];
            local_path_for(local->valuestring, path, sizeof(path));
            add_item(url->valuestring, path);
        }
    }
    ESP_LOGI(TAG_CACHE, "course cache indexed %u assets", (unsigned)s_item_count);
    unlock_items();
}

static void load_saved_manifest(void) {
    if (!file_exists_min(COURSE_CACHE_MANIFEST, 10)) return;
    FILE *fp = fopen(COURSE_CACHE_MANIFEST, "rb");
    if (!fp) return;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n <= 0 || n > COURSE_CACHE_MAX_JSON) {
        fclose(fp);
        return;
    }
    char *buf = heap_caps_malloc((size_t)n + 1,
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = heap_caps_malloc((size_t)n + 1, MALLOC_CAP_8BIT);
    if (!buf) {
        fclose(fp);
        return;
    }
    size_t got = fread(buf, 1, (size_t)n, fp);
    fclose(fp);
    buf[got] = 0;
    cJSON *root = cJSON_ParseWithLength(buf, got);
    heap_caps_free(buf);
    if (!root) return;
    index_manifest(root);
    cJSON_Delete(root);
}

#ifdef CONFIG_VIMATE_SD_CACHE_ENABLE
typedef struct {
    const char *name;
    int mosi;
    int miso;
    int sclk;
    int cs;
    bool unsafe_conflict;
} sd_spi_profile_t;

static void sd_reset_pin_if_valid(int pin) {
    if (pin >= 0 && pin < 49) {
        gpio_reset_pin((gpio_num_t)pin);
    }
}

uint64_t course_media_cache_free_bytes(void) {
    if (!s_ready) return 0;
    uint64_t tot = 0, freeb = 0;
    if (esp_vfs_fat_info(COURSE_CACHE_BASE, &tot, &freeb) != ESP_OK) return 0;
    return freeb;
}

#if defined(BOARD_SD_USE_SDMMC) && BOARD_SD_USE_SDMMC
/* ===== diskio bounce buffer cho FATFS trên SDMMC (P4, 15/09/2026) =====
 * Driver sdmmc chỉ DMA thẳng nhiều block khi buffer căn cache line (64 B trên P4) và
 * cỡ bội 64 (sdmmc_host_check_buffer_alignment); còn lại nó chép TỪNG SECTOR qua một
 * buffer tạm 512 B = mỗi sector một lệnh CMD17/24 → thẻ rẻ trễ ~4 ms/lệnh: đo được
 * 61 KB/s ghi, 126–298 KB/s đọc dù bus 20 MHz. Buffer đi qua FATFS phần lớn KHÔNG căn
 * (buffer stdio của newlib, FIL buf, mảng cục bộ, malloc PSRAM thường) nên gần như mọi
 * đường (mp4_player, cache khoá học, hash) đều rơi vào đường chậm.
 * Thay diskio của volume bằng bản này: buffer đã căn → gọi thẳng; chưa căn → gom tối
 * đa SD_BOUNCE_SECTORS sector qua một buffer PSRAM căn 64 (một lệnh nhiều block) rồi
 * memcpy. FF_FS_REENTRANT=1 (mutex theo volume) nên một bounce buffer/volume là đủ.
 * Cỡ 16 KB PSRAM, cấp một lần lúc mount; không đụng RAM nội. */
#define SD_BOUNCE_SECTORS 32
static sdmmc_card_t *s_sd_diskio_card = NULL;
static uint8_t      *s_sd_bounce = NULL;

static inline bool sd_buf_dma_ok(const void *buf, size_t bytes) {
    return (((uintptr_t)buf & 63u) == 0) && ((bytes & 63u) == 0);
}

static DSTATUS sd_diskio_init(BYTE pdrv)   { (void)pdrv; return 0; }
static DSTATUS sd_diskio_status(BYTE pdrv) { (void)pdrv; return 0; }

static DRESULT sd_diskio_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count) {
    (void)pdrv;
    sdmmc_card_t *card = s_sd_diskio_card;
    if (!card) return RES_NOTRDY;
    const size_t ss = card->csd.sector_size;
    if (!s_sd_bounce || sd_buf_dma_ok(buff, (size_t)count * ss)) {
        return sdmmc_read_sectors(card, buff, sector, count) == ESP_OK ? RES_OK : RES_ERROR;
    }
    while (count) {
        UINT n = count > SD_BOUNCE_SECTORS ? SD_BOUNCE_SECTORS : count;
        if (sdmmc_read_sectors(card, s_sd_bounce, sector, n) != ESP_OK) return RES_ERROR;
        memcpy(buff, s_sd_bounce, (size_t)n * ss);
        buff += (size_t)n * ss;
        sector += n;
        count -= n;
    }
    return RES_OK;
}

static DRESULT sd_diskio_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count) {
    (void)pdrv;
    sdmmc_card_t *card = s_sd_diskio_card;
    if (!card) return RES_NOTRDY;
    const size_t ss = card->csd.sector_size;
    if (!s_sd_bounce || sd_buf_dma_ok(buff, (size_t)count * ss)) {
        return sdmmc_write_sectors(card, buff, sector, count) == ESP_OK ? RES_OK : RES_ERROR;
    }
    while (count) {
        UINT n = count > SD_BOUNCE_SECTORS ? SD_BOUNCE_SECTORS : count;
        memcpy(s_sd_bounce, buff, (size_t)n * ss);
        if (sdmmc_write_sectors(card, s_sd_bounce, sector, n) != ESP_OK) return RES_ERROR;
        buff += (size_t)n * ss;
        sector += n;
        count -= n;
    }
    return RES_OK;
}

static DRESULT sd_diskio_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    (void)pdrv;
    sdmmc_card_t *card = s_sd_diskio_card;
    if (!card) return RES_NOTRDY;
    switch (cmd) {
        case CTRL_SYNC:        return RES_OK;
        case GET_SECTOR_COUNT: *((DWORD *)buff) = card->csd.capacity;    return RES_OK;
        case GET_SECTOR_SIZE:  *((WORD *)buff)  = card->csd.sector_size; return RES_OK;
        /* CTRL_TRIM: ff.c bỏ qua kết quả; không làm erase để đơn giản. */
        default:               return RES_ERROR;
    }
}

static void sd_diskio_install_bounce(sdmmc_card_t *card) {
    if (!card) return;
    if (!s_sd_bounce) {
        s_sd_bounce = (uint8_t *)heap_caps_aligned_alloc(
            64, (size_t)SD_BOUNCE_SECTORS * card->csd.sector_size,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_sd_bounce) {
            ESP_LOGW(TAG_CACHE, "SD bounce buffer: PSRAM het, giu diskio mac dinh (cham)");
            return;
        }
    }
    s_sd_diskio_card = card;
    BYTE pdrv = ff_diskio_get_pdrv_card(card);
    if (pdrv == 0xFF) {
        ESP_LOGW(TAG_CACHE, "SD bounce buffer: khong tim thay pdrv cua card");
        return;
    }
    const ff_diskio_impl_t impl = {
        .init   = sd_diskio_init,
        .status = sd_diskio_status,
        .read   = sd_diskio_read,
        .write  = sd_diskio_write,
        .ioctl  = sd_diskio_ioctl,
    };
    ff_diskio_register(pdrv, &impl);
    ESP_LOGI(TAG_CACHE, "SD diskio bounce %u sector (%u KB PSRAM can 64) tren pdrv %u",
             (unsigned)SD_BOUNCE_SECTORS,
             (unsigned)(SD_BOUNCE_SECTORS * card->csd.sector_size / 1024), (unsigned)pdrv);
}
#endif /* BOARD_SD_USE_SDMMC */

#if CONFIG_VIMATE_DIAG_ENABLE
/* Đo thông lượng thật sau mount (bản diag): ghi 256 KB theo khối 16 KB rồi đọc lại,
 * in KB/s. 256 KB/boot không đáng kể với thẻ 4 GB; buffer 16 KB ở PSRAM, free ngay.
 * Số để chọn BOARD_SD_MMC_FREQ_KHZ (README-P4 §7 thẻ nhớ). */
#define SD_BENCH_CHUNK  (16 * 1024)
#define SD_BENCH_TOTAL  (256 * 1024)
static void sd_benchmark(void) {
    const char *path = COURSE_CACHE_BASE "/sd_bench.bin";
    /* Căn 64 B + cỡ bội 64: điều kiện để sdmmc_host_check_buffer_alignment (P4, cache
     * line 64) cho DMA thẳng nhiều block; lệch là driver chép từng sector qua buffer
     * tạm (đo 15/09: 61 KB/s ghi / 298 KB/s đọc @ 20 MHz — không phải tốc độ bus). */
    uint8_t *buf = (uint8_t *)heap_caps_aligned_alloc(64, SD_BENCH_CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) return;
    for (int i = 0; i < SD_BENCH_CHUNK; i++) buf[i] = (uint8_t)(i * 7 + 3);
    int64_t t0 = esp_timer_get_time();
    FILE *fp = fopen(path, "wb");
    size_t wrote = 0;
    if (fp) {
        for (size_t off = 0; off < SD_BENCH_TOTAL; off += SD_BENCH_CHUNK) {
            wrote += fwrite(buf, 1, SD_BENCH_CHUNK, fp);
        }
        fclose(fp);
    }
    int64_t t1 = esp_timer_get_time();
    size_t rd = 0;
    bool ok = true;
    fp = fopen(path, "rb");
    if (fp) {
        size_t n;
        while ((n = fread(buf, 1, SD_BENCH_CHUNK, fp)) > 0) {
            if (n == SD_BENCH_CHUNK && buf[100] != (uint8_t)(100 * 7 + 3)) ok = false;
            rd += n;
        }
        fclose(fp);
    }
    int64_t t2 = esp_timer_get_time();
    unlink(path);
    heap_caps_free(buf);
    int64_t wus = t1 - t0 > 0 ? t1 - t0 : 1;
    int64_t rus = t2 - t1 > 0 ? t2 - t1 : 1;
    ESP_LOGI(TAG_CACHE, "SD bench (fwrite buffer can 64 / fread qua stdio+bounce, khoi 16 KB): write %u KB %lld ms = %lld KB/s, read %u KB %lld ms = %lld KB/s%s",
             (unsigned)(wrote / 1024), (long long)(wus / 1000),
             (long long)((int64_t)wrote * 1000000LL / 1024 / wus),
             (unsigned)(rd / 1024), (long long)(rus / 1000),
             (long long)((int64_t)rd * 1000000LL / 1024 / rus),
             ok ? "" : " (DATA MISMATCH)");
}
#endif

static esp_err_t sd_verify_rw(void) {
    const char *path = COURSE_CACHE_BASE "/sd_test.txt";
    const char *msg = "sd-ok";
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        ESP_LOGW(TAG_CACHE, "SD verify write open failed: errno=%d (%s)",
                 errno, strerror(errno));
        return ESP_FAIL;
    }
    size_t wn = fwrite(msg, 1, strlen(msg), fp);
    fclose(fp);
    if (wn != strlen(msg)) {
        ESP_LOGW(TAG_CACHE, "SD verify write failed");
        return ESP_FAIL;
    }

    char buf[32] = {0};
    fp = fopen(path, "rb");
    if (!fp) {
        ESP_LOGW(TAG_CACHE, "SD verify read open failed");
        return ESP_FAIL;
    }
    size_t rn = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    if (rn != strlen(msg) || strcmp(buf, msg) != 0) {
        ESP_LOGW(TAG_CACHE, "SD verify mismatch got='%s'", buf);
        return ESP_FAIL;
    }
    /* Log dung lượng trống — bản ghi MỚI cần cấp cluster mới; nếu thẻ ĐẦY thì
     * verify (ghi đè file cũ, dùng lại cluster) vẫn PASS nhưng fwrite bản ghi sẽ
     * trả 0 ("Đã lưu 0 giây"). Đây là cách phân biệt thẻ-đầy với lỗi-ghi. */
    uint64_t tot = 0, freeb = 0;
    esp_err_t fr = esp_vfs_fat_info(COURSE_CACHE_BASE, &tot, &freeb);
    if (fr == ESP_OK) {
        ESP_LOGW(TAG_CACHE, "SD free=%llu KB / total=%llu KB",
                 (unsigned long long)(freeb / 1024), (unsigned long long)(tot / 1024));
    } else {
        ESP_LOGW(TAG_CACHE, "SD esp_vfs_fat_info failed: %s", esp_err_to_name(fr));
    }
    ESP_LOGI(TAG_CACHE, "SD verify read/write OK: %s", path);
    return ESP_OK;
}

static esp_err_t sd_try_mount_profile(const sd_spi_profile_t *profile,
                                      sdmmc_card_t **out_card) {
    if (!profile || !out_card) return ESP_ERR_INVALID_ARG;
    *out_card = NULL;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 400; /* bring-up first; raise only after stable */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = profile->mosi,
        .miso_io_num = profile->miso,
        .sclk_io_num = profile->sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16 * 1024,
    };
    ESP_LOGI(TAG_CACHE, "SD probe profile=%s mosi=%d miso=%d sclk=%d cs=%d%s",
             profile->name, profile->mosi, profile->miso, profile->sclk,
             profile->cs, profile->unsafe_conflict ? " UNSAFE-CONFLICT" : "");
    esp_err_t r = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (r != ESP_OK) {
        ESP_LOGW(TAG_CACHE, "SD probe bus init fail profile=%s: %s",
                 profile->name, esp_err_to_name(r));
        return r;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = profile->cs;
    slot_config.host_id = host.slot;
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
    };
    r = esp_vfs_fat_sdspi_mount(COURSE_CACHE_BASE, &host, &slot_config,
                                &mount_config, out_card);
    if (r != ESP_OK) {
        ESP_LOGW(TAG_CACHE, "SD probe mount fail profile=%s: %s",
                 profile->name, esp_err_to_name(r));
        spi_bus_free(host.slot);
        sd_reset_pin_if_valid(profile->mosi);
        sd_reset_pin_if_valid(profile->miso);
        sd_reset_pin_if_valid(profile->sclk);
        sd_reset_pin_if_valid(profile->cs);
        return r;
    }

    r = sd_verify_rw();
    if (r != ESP_OK) {
        ESP_LOGW(TAG_CACHE, "SD probe rw fail profile=%s: %s",
                 profile->name, esp_err_to_name(r));
        esp_vfs_fat_sdcard_unmount(COURSE_CACHE_BASE, *out_card);
        *out_card = NULL;
        spi_bus_free(host.slot);
        sd_reset_pin_if_valid(profile->mosi);
        sd_reset_pin_if_valid(profile->miso);
        sd_reset_pin_if_valid(profile->sclk);
        sd_reset_pin_if_valid(profile->cs);
        return r;
    }

    ESP_LOGI(TAG_CACHE, "SD mounted profile=%s", profile->name);
    sdmmc_card_print_info(stdout, *out_card);
    return ESP_OK;
}

#if defined(BOARD_SD_USE_SDMMC) && BOARD_SD_USE_SDMMC
/* Native SDMMC mount (board 4.3" có khe thẻ chạy SDMMC). Chân từ board header
 * (BOARD_SD_MMC_*). Trả ESP_OK nếu mount + verify R/W OK; ngược lại WARN + lỗi
 * để caller fallback xuống SDSPI/HTTP. */
/* Thử mount SDMMC slot 0 với 1 độ rộng bus. send_op_cond timeout 0x107 = card
 * không đáp trên CMD (CMD thả nổi / sai chân / chưa cấp nguồn). Ép pull-up nội
 * tường minh trên CLK/CMD/D0(+D1-3) TRƯỚC mount để loại nguyên nhân điện. */
static esp_err_t sd_try_mount_sdmmc_width(sdmmc_card_t **out_card, int width, int freq_khz) {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = BOARD_SD_MMC_SLOT;
    /* 15/09/2026: thẻ đã mount ở 400 kHz (probing) → lần 1 dùng BOARD_SD_MMC_FREQ_KHZ
     * (P4: 20 MHz), lần sau hạ về probing (xem sd_try_mount_sdmmc). */
    host.max_freq_khz = freq_khz;
    /* Rail SD đã được cấp nguồn cố định 3.3V qua esp_ldo (xem caller) — KHÔNG
     * dùng host.pwr_ctrl_handle vì host dùng chung với C6 (init bị skip) khiến
     * driver không tự nâng áp → kẹt 0V → đọc được nhưng GHI fail. */

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = BOARD_SD_MMC_CLK;
    slot.cmd = BOARD_SD_MMC_CMD;
    slot.d0  = BOARD_SD_MMC_D0;

    /* Pull-up nội (~45k) tường minh — flag INTERNAL_PULLUP đôi khi chưa đủ; SD
     * cần CMD/D kéo cao để init. Set trước khi driver cấu hình lại chân. */
    gpio_set_pull_mode((gpio_num_t)BOARD_SD_MMC_CLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode((gpio_num_t)BOARD_SD_MMC_CMD, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode((gpio_num_t)BOARD_SD_MMC_D0,  GPIO_PULLUP_ONLY);
#if defined(BOARD_SD_MMC_WIDTH) && (BOARD_SD_MMC_WIDTH == 4)
    if (width == 4) {
        slot.d1 = BOARD_SD_MMC_D1;
        slot.d2 = BOARD_SD_MMC_D2;
        slot.d3 = BOARD_SD_MMC_D3;
        slot.width = 4;
        gpio_set_pull_mode((gpio_num_t)BOARD_SD_MMC_D1, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode((gpio_num_t)BOARD_SD_MMC_D2, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode((gpio_num_t)BOARD_SD_MMC_D3, GPIO_PULLUP_ONLY);
    } else
#endif
    {
        slot.width = 1;
    }
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
    };

    ESP_LOGI(TAG_CACHE,
             "SD SDMMC mount slot=%d CLK=%d CMD=%d D0=%d width=%d freq=%dkHz",
             (int)host.slot, (int)slot.clk, (int)slot.cmd, (int)slot.d0,
             (int)slot.width, (int)host.max_freq_khz);
    return esp_vfs_fat_sdmmc_mount(COURSE_CACHE_BASE, &host, &slot,
                                   &mount_config, out_card);
}

static esp_err_t sd_try_mount_sdmmc(sdmmc_card_t **out_card) {
    if (!out_card) return ESP_ERR_INVALID_ARG;
    *out_card = NULL;

    /* Cấp nguồn rail SD 3.3V qua LDO on-chip kênh 4 TRƯỚC mount. P4: yêu cầu
     * ĐÚNG 3300mV = chế độ "rail voltage" (full 3.3V) — vượt range khuyến nghị
     * [500,2700] nhưng hợp lệ (ldo_ll: use_rail_voltage khi ==3300). 2.7V hoặc
     * 0V → card đọc được nhưng GHI fail. KHÔNG ESP_ERROR_CHECK (tránh panic). */
#if defined(BOARD_SD_PWR_LDO_CHAN)
    static esp_ldo_channel_handle_t s_sd_ldo = NULL; /* giữ sống suốt đời thiết bị */
    if (!s_sd_ldo) {
        esp_ldo_channel_config_t ldo_cfg = {
            .chan_id = BOARD_SD_PWR_LDO_CHAN,
            .voltage_mv = 3300, /* rail voltage = 3.3V */
        };
        esp_err_t pr = esp_ldo_acquire_channel(&ldo_cfg, &s_sd_ldo);
        if (pr != ESP_OK) {
            ESP_LOGW(TAG_CACHE, "SD LDO 3.3V (chan=%d) acquire fail: %s",
                     (int)BOARD_SD_PWR_LDO_CHAN, esp_err_to_name(pr));
            s_sd_ldo = NULL;
        } else {
            ESP_LOGI(TAG_CACHE, "SD LDO power ON 3.3V (on-chip chan=%d)",
                     (int)BOARD_SD_PWR_LDO_CHAN);
            vTaskDelay(pdMS_TO_TICKS(20)); /* rail ổn định trước khi nói chuyện card */
        }
    }
#endif

    /* Card lạnh / tín hiệu marginal → init đôi khi fail lần đầu. Thang thử:
     * 4-bit @ BOARD_SD_MMC_FREQ_KHZ → 4-bit @ 400 kHz → 1-bit @ 400 kHz (loại trừ
     * lần lượt xung cao rồi đường D1-3). */
    esp_err_t r = ESP_FAIL;
    for (int attempt = 1; attempt <= 3; attempt++) {
        int width = (attempt < 3) ? 4 : 1;
        int freq  = (attempt == 1) ? BOARD_SD_MMC_FREQ_KHZ : SDMMC_FREQ_PROBING;
        r = sd_try_mount_sdmmc_width(out_card, width, freq);
        if (r == ESP_OK) break;
        ESP_LOGW(TAG_CACHE, "SD SDMMC mount #%d (width=%d, %dkHz) fail: %s",
                 attempt, width, freq, esp_err_to_name(r));
        *out_card = NULL;
        vTaskDelay(pdMS_TO_TICKS(150));
    }
#if defined(BOARD_SD_PWR_LDO_CHAN)
    if (r != ESP_OK && s_sd_ldo) {
        esp_ldo_release_channel(s_sd_ldo); /* mount fail → nhả LDO, tránh leak */
        s_sd_ldo = NULL;
    }
#endif
    if (r != ESP_OK) {
        ESP_LOGW(TAG_CACHE, "SD SDMMC mount fail sau 3 lần: %s — fallback "
                 "SDSPI/HTTP. Timeout 0x107 dai dẳng = card KHÔNG nằm trên chân "
                 "slot-0 IOMUX (43/44/39-42) hoặc chưa cấp nguồn → cần pinout "
                 "khe TF của board 4.3.",
                 esp_err_to_name(r));
        *out_card = NULL;
        return r;
    }

    /* Mount OK (đọc được) — in thông tin card thật để chẩn đoán trước khi test ghi. */
    if (*out_card) {
        ESP_LOGI(TAG_CACHE, "SD card mounted — thông tin:");
        sdmmc_card_print_info(stdout, *out_card);
    }

    r = sd_verify_rw();
    if (r != ESP_OK) {
        ESP_LOGW(TAG_CACHE, "SD SDMMC rw verify fail: %s", esp_err_to_name(r));
        esp_vfs_fat_sdcard_unmount(COURSE_CACHE_BASE, *out_card);
        *out_card = NULL;
        return r;
    }

    ESP_LOGI(TAG_CACHE, "SD mounted via SDMMC");
    sdmmc_card_print_info(stdout, *out_card);
    sd_diskio_install_bounce(*out_card);
#if CONFIG_VIMATE_DIAG_ENABLE
    sd_benchmark();
#endif
    return ESP_OK;
}
#endif /* BOARD_SD_USE_SDMMC */

static esp_err_t sd_mount_configured_or_probe(sdmmc_card_t **out_card) {
    if (!out_card) return ESP_ERR_INVALID_ARG;
    *out_card = NULL;

#if defined(BOARD_SD_USE_SDMMC) && BOARD_SD_USE_SDMMC
    /* Board khai rõ khe thẻ chạy SDMMC thật (slot + đủ 6 chân + kênh LDO) → đi
     * thẳng đường đầy đủ: 4-bit, có cấp nguồn LDO, tự hạ xuống 1-bit nếu D1-3 lỗi.
     *
     * PHẢI đặt trước guard "P4 ESP-Hosted owns SDIO" ngay bên dưới. Guard đó
     * đúng cho board KHÔNG khai báo khe thẻ, nhưng giả định của nó — hosted
     * chiếm hết SDIO của P4 — KHÔNG đúng với bo 4.3": ESP-Hosted đi SDIO
     * **slot 1** (CONFIG_ESP_HOSTED_SDIO_SLOT=1), khe TF đi **slot 0** IOMUX.
     * Kconfig của esp_hosted còn ghi thẳng "Slot 0 connects to the MicroSD Card
     * slot". Không có ngoại lệ này thì thẻ nhớ bị chặn ngay từ dòng đầu. */
    return sd_try_mount_sdmmc(out_card);
#endif

#if CONFIG_IDF_TARGET_ESP32P4 && CONFIG_ESP_WIFI_REMOTE_LIBRARY_HOSTED
    if (CONFIG_VIMATE_SD_SPI_MOSI < 0 || CONFIG_VIMATE_SD_SPI_MISO < 0 ||
        CONFIG_VIMATE_SD_SPI_SCLK < 0 || CONFIG_VIMATE_SD_SPI_CS < 0) {
        ESP_LOGW(TAG_CACHE,
                 "SD cache skip: P4 ESP-Hosted owns SDIO; configure SDSPI pins "
                 "before enabling external SD cache");
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif

#if CONFIG_IDF_TARGET_ESP32P4 && defined(BOARD_SD_CLK) && defined(BOARD_SD_CMD) && defined(BOARD_SD_D0)
    {
        gpio_set_pull_mode((gpio_num_t)BOARD_SD_CLK, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode((gpio_num_t)BOARD_SD_CMD, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode((gpio_num_t)BOARD_SD_D0, GPIO_PULLUP_ONLY);

        sdmmc_host_t mhost = SDMMC_HOST_DEFAULT();
        mhost.flags = SDMMC_HOST_FLAG_1BIT;
        mhost.max_freq_khz = SDMMC_FREQ_PROBING;
        sdmmc_slot_config_t mslot = SDMMC_SLOT_CONFIG_DEFAULT();
        mslot.width = 1;
        mslot.clk = BOARD_SD_CLK;
        mslot.cmd = BOARD_SD_CMD;
        mslot.d0 = BOARD_SD_D0;
        mslot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
        esp_vfs_fat_sdmmc_mount_config_t mcfg = {
            .format_if_mount_failed = false,
            .max_files = 8,
            .allocation_unit_size = 16 * 1024,
            .disk_status_check_enable = false,
        };
        esp_err_t mr = ESP_FAIL;
        for (int attempt = 1; attempt <= 3; attempt++) {
            ESP_LOGI(TAG_CACHE, "SD SDMMC P4 1-bit mount #%d clk=%d cmd=%d d0=%d",
                     attempt, (int)BOARD_SD_CLK, (int)BOARD_SD_CMD, (int)BOARD_SD_D0);
            mr = esp_vfs_fat_sdmmc_mount(COURSE_CACHE_BASE, &mhost, &mslot, &mcfg,
                                         out_card);
            if (mr == ESP_OK) {
                ESP_LOGI(TAG_CACHE, "SD SDMMC P4 mounted (lần %d)", attempt);
                return ESP_OK;
            }
            ESP_LOGW(TAG_CACHE, "SD SDMMC P4 mount #%d fail: %s", attempt,
                     esp_err_to_name(mr));
            vTaskDelay(pdMS_TO_TICKS(150));
        }
        return mr;
    }
#endif

#if defined(BOARD_SD_USE_SDMMC_1BIT) && BOARD_SD_USE_SDMMC_1BIT
    /* Board genu-v6: SD qua SDMMC 1-bit (pin từ board header), KHÔNG phải SDSPI.
     * Mount fail (không thẻ / sai pullup) → trả lỗi → cache off → fallback HTTP
     * (KHÔNG hồi quy, chỉ là không tăng tốc). */
    {
        /* GPIO45(CLK)/46(CMD) là strapping pin S3 + pullup nội SDMMC khá yếu →
         * ép GPIO_PULLUP_ONLY tường minh (lỗi 0x108 io_reset = CMD/D0 thả nổi).
         * Pull-up này set TRƯỚC mount; flag INTERNAL_PULLUP giữ pullup khi driver
         * cấu hình lại chân. */
        gpio_set_pull_mode((gpio_num_t)BOARD_SD_CLK, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode((gpio_num_t)BOARD_SD_CMD, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode((gpio_num_t)BOARD_SD_D0, GPIO_PULLUP_ONLY);

        sdmmc_host_t mhost = SDMMC_HOST_DEFAULT();
        mhost.flags = SDMMC_HOST_FLAG_1BIT;
        mhost.max_freq_khz = SDMMC_FREQ_PROBING; /* bring-up; nâng sau khi ổn */
        sdmmc_slot_config_t mslot = SDMMC_SLOT_CONFIG_DEFAULT();
        mslot.width = 1;
        mslot.clk = BOARD_SD_CLK;
        mslot.cmd = BOARD_SD_CMD;
        mslot.d0 = BOARD_SD_D0;
        mslot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
        esp_vfs_fat_sdmmc_mount_config_t mcfg = {
            .format_if_mount_failed = false,
            .max_files = 8,
            .allocation_unit_size = 16 * 1024,
            .disk_status_check_enable = false,
        };
        /* Thẻ lạnh / tín hiệu marginal → init đôi khi fail lần đầu rồi OK. Thử 3 lần. */
        esp_err_t mr = ESP_FAIL;
        for (int attempt = 1; attempt <= 3; attempt++) {
            ESP_LOGI(TAG_CACHE, "SD SDMMC 1-bit mount #%d clk=%d cmd=%d d0=%d",
                     attempt, (int)BOARD_SD_CLK, (int)BOARD_SD_CMD, (int)BOARD_SD_D0);
            mr = esp_vfs_fat_sdmmc_mount(COURSE_CACHE_BASE, &mhost, &mslot, &mcfg,
                                         out_card);
            if (mr == ESP_OK) {
                ESP_LOGI(TAG_CACHE, "SD SDMMC mounted (lần %d)", attempt);
                return ESP_OK;
            }
            ESP_LOGW(TAG_CACHE, "SD SDMMC mount #%d fail: %s", attempt,
                     esp_err_to_name(mr));
            vTaskDelay(pdMS_TO_TICKS(150));
        }
        ESP_LOGW(TAG_CACHE, "SD SDMMC mount fail sau 3 lần: %s — fallback HTTP. "
                 "Nếu vẫn lỗi 0x108: thiếu pull-up NGOÀI (10k) trên CMD/D0.",
                 esp_err_to_name(mr));
        return mr;
    }
#endif

    if (CONFIG_VIMATE_SD_SPI_MOSI >= 0 && CONFIG_VIMATE_SD_SPI_MISO >= 0 &&
        CONFIG_VIMATE_SD_SPI_SCLK >= 0 && CONFIG_VIMATE_SD_SPI_CS >= 0) {
        const sd_spi_profile_t cfg = {
            .name = "configured",
            .mosi = CONFIG_VIMATE_SD_SPI_MOSI,
            .miso = CONFIG_VIMATE_SD_SPI_MISO,
            .sclk = CONFIG_VIMATE_SD_SPI_SCLK,
            .cs = CONFIG_VIMATE_SD_SPI_CS,
            .unsafe_conflict = false,
        };
        return sd_try_mount_profile(&cfg, out_card);
    }

    return ESP_ERR_NOT_FOUND;
}
#endif

esp_err_t course_media_cache_init(void) {
    if (s_ready) return ESP_OK;
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) return ESP_ERR_NO_MEM;
    }

#ifdef CONFIG_VIMATE_SD_CACHE_ENABLE
#if !(defined(BOARD_SD_USE_SDMMC) && BOARD_SD_USE_SDMMC)
    ESP_LOGI(TAG_CACHE, "SD SPI mount try mosi=%d miso=%d sclk=%d cs=%d",
             CONFIG_VIMATE_SD_SPI_MOSI, CONFIG_VIMATE_SD_SPI_MISO,
             CONFIG_VIMATE_SD_SPI_SCLK, CONFIG_VIMATE_SD_SPI_CS);
#endif
    sdmmc_card_t *card = NULL;
    esp_err_t r = sd_mount_configured_or_probe(&card);
    if (r != ESP_OK) {
        ESP_LOGW(TAG_CACHE, "SD mount fail: %s", esp_err_to_name(r));
        return r;
    }
    s_ready = true;
    mkdir_p(COURSE_CACHE_BASE "/vimate");
    load_saved_manifest();
    ESP_LOGI(TAG_CACHE, "SD course cache mounted");
    return ESP_OK;
#else
    ESP_LOGI(TAG_CACHE, "SD course cache disabled");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool course_media_cache_ready(void) {
    return s_ready;
}

static void sync_blocking(void) {
    if (!s_ready) return;
    if (g_vimate_server.device_token[0] == '\0') {
        ESP_LOGI(TAG_CACHE, "course cache skip: device not activated");
        return;
    }

    char url[220];
    snprintf(url, sizeof(url), "%s/api/devices/%s/course-cache?scope=active",
             g_vimate_server.base_url, g_vimate_server.mac_id);
    char auth[120];
    snprintf(auth, sizeof(auth), "Bearer %s", g_vimate_server.device_token);

    uint8_t *body = NULL;
    size_t len = 0;
    if (http_get_to_buf_auth(url, auth, &body, &len) != ESP_OK) {
        ESP_LOGW(TAG_CACHE, "course-cache manifest GET fail");
        return;
    }

    if (!looks_like_json(body, len)) {
        log_body_preview("course-cache manifest is not JSON", body, len);
        free(body);
        return;
    }

    cJSON *root = cJSON_ParseWithLength((const char *)body, len);
    if (!root) {
        log_body_preview("course-cache JSON parse fail", body, len);
        free(body);
        return;
    }

    /* Ghi manifest atomic: .part + rename, kiểm fwrite đủ → đầy thẻ/cúp điện
     * giữa chừng không để lại manifest hỏng (giữ bản cũ còn dùng được). */
    mkdir_parent(COURSE_CACHE_MANIFEST);
    char mtmp[160];
    snprintf(mtmp, sizeof(mtmp), "%s.part", COURSE_CACHE_MANIFEST);
    FILE *mf = fopen(mtmp, "wb");
    if (mf) {
        size_t w = fwrite(body, 1, len, mf);
        int fc = fclose(mf);
        if (w == len && fc == 0) {
            if (rename(mtmp, COURSE_CACHE_MANIFEST) != 0) {
                ESP_LOGW(TAG_CACHE, "manifest rename fail — giữ bản cũ");
                remove(mtmp);
            }
        } else {
            ESP_LOGW(TAG_CACHE, "manifest ghi thiếu (%u/%u) — giữ bản cũ", (unsigned)w, (unsigned)len);
            remove(mtmp);
        }
    }
    free(body);
    index_manifest(root);

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) data = root;
    cJSON *courses = cJSON_GetObjectItem(data, "courses");
    int ok = 0, fail = 0, skip = 0;
    if (cJSON_IsArray(courses)) {
        cJSON *course;
        cJSON_ArrayForEach(course, courses) {
            cJSON *assets = cJSON_GetObjectItem(course, "assets");
            if (!cJSON_IsArray(assets)) continue;
            cJSON *asset;
            cJSON_ArrayForEach(asset, assets) {
                cJSON *urlj = cJSON_GetObjectItem(asset, "url");
                cJSON *localj = cJSON_GetObjectItem(asset, "localPath");
                cJSON *shaj = cJSON_GetObjectItem(asset, "sha256");
                if (!cJSON_IsString(urlj) || !cJSON_IsString(localj)) continue;

                char dest[192];
                local_path_for(localj->valuestring, dest, sizeof(dest));
                const char *sha = cJSON_IsString(shaj) ? shaj->valuestring : "";
                if (file_exists_min(dest, 100) && sha256_file_matches(dest, sha)) {
                    skip++;
                    continue;
                }

                char fetch_url[220];
                const char *dl = url_rewrite_localhost_media(urlj->valuestring,
                                                             fetch_url,
                                                             sizeof(fetch_url));
                if (http_get_to_file_auth(dl, auth, dest) == ESP_OK &&
                    sha256_file_matches(dest, sha)) {
                    ok++;
                } else {
                    unlink(dest);
                    fail++;
                    ESP_LOGW(TAG_CACHE, "course asset download fail: %s", dl);
                }
            }
        }
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG_CACHE, "course cache sync done: ok=%d skip=%d fail=%d map=%u",
             ok, skip, fail, (unsigned)s_item_count);
}

static void sync_task(void *arg) {
    (void)arg;
    sync_blocking();
    lock_items();
    s_sync_running = false;
    unlock_items();
    vTaskDelete(NULL);
}

void course_media_cache_sync_async(void) {
    if (!s_ready) return;
    uint32_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (internal < COURSE_CACHE_SYNC_MIN_INTERNAL ||
        largest < COURSE_CACHE_SYNC_MIN_LARGEST) {
        ESP_LOGW(TAG_CACHE,
                 "course cache sync deferred: internal=%u largest=%u min_internal=%u min_largest=%u",
                 (unsigned)internal, (unsigned)largest,
                 (unsigned)COURSE_CACHE_SYNC_MIN_INTERNAL,
                 (unsigned)COURSE_CACHE_SYNC_MIN_LARGEST);
        return;
    }

    lock_items();
    if (s_sync_running) {
        unlock_items();
        ESP_LOGI(TAG_CACHE, "course cache sync already running");
        return;
    }
    s_sync_running = true;
    unlock_items();

    BaseType_t ok = xTaskCreatePinnedToCore(sync_task, "course_cache", 12288, NULL,
                                            VIMATE_TASK_PRIO_BACKGROUND, NULL,
                                            VIMATE_TASK_CORE_IO);
    if (ok != pdPASS) {
        lock_items();
        s_sync_running = false;
        unlock_items();
        ESP_LOGW(TAG_CACHE, "course cache sync task create failed");
    }
}

bool course_media_cache_path(const char *url, char *out_path, size_t out_size) {
    if (!s_ready || !url || !out_path || out_size == 0) return false;
    char path[192] = {0};
    bool found = false;
    lock_items();
    for (size_t i = 0; i < s_item_count; i++) {
        if (s_items[i].url && strcmp(s_items[i].url, url) == 0) {
            strlcpy(path, s_items[i].path, sizeof(path));
            found = true;
            break;
        }
    }
    unlock_items();
    if (found && file_exists_min(path, 100)) {
        strlcpy(out_path, path, out_size);
        return true;
    }
    return false;
}
