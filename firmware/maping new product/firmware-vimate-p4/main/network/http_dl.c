/**
 * http_dl.c — HTTPS GET helpers (file + buffer).
 * Sử dụng esp_http_client với CA bundle.
 */
#include "http_dl.h"
#include "vimate.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_tls.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HTTP_DL_BUF_CHUNK 4096
#define HTTP_DL_MAX_BUF   (256 * 1024)

static esp_err_t mkdir_p(const char *path) {
    /* Tạo từng cấp; bỏ qua nếu đã tồn tại. */
    char tmp[160];
    strlcpy(tmp, path, sizeof(tmp));
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return ESP_OK;
}

esp_err_t http_dl_get_to_file(const char *url, const char *dest_path) {
    /* Tạo thư mục cha */
    char dir[160];
    strlcpy(dir, dest_path, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;
        mkdir_p(dir);
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(cli, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(cli);
        return err;
    }
    int total = esp_http_client_fetch_headers(cli);
    int status = esp_http_client_get_status_code(cli);
    if (status != 200) {
        ESP_LOGE(TAG_MAIN, "http_dl %s → %d", url, status);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_FAIL;
    }
    FILE *fp = fopen(dest_path, "wb");
    if (!fp) {
        ESP_LOGE(TAG_MAIN, "http_dl fopen %s fail", dest_path);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_FAIL;
    }
    char *buf = heap_caps_malloc(HTTP_DL_BUF_CHUNK,
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = heap_caps_malloc(HTTP_DL_BUF_CHUNK, MALLOC_CAP_8BIT);
    if (!buf) {
        fclose(fp);
        unlink(dest_path);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_ERR_NO_MEM;
    }
    int read_total = 0;
    while (1) {
        int n = esp_http_client_read(cli, buf, HTTP_DL_BUF_CHUNK);
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
    fclose(fp);
    heap_caps_free(buf);
    if (!esp_http_client_is_complete_data_received(cli)) err = ESP_FAIL;
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    if (err != ESP_OK || read_total <= 0) {
        unlink(dest_path);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG_MAIN, "http_dl %s → %s (%d bytes, content-len=%d)",
             url, dest_path, read_total, total);
    return ESP_OK;
}

esp_err_t http_dl_get_to_buf(const char *url, uint8_t **out, size_t *out_len) {
    return http_dl_get_to_buf_auth(url, NULL, out, out_len);
}

esp_err_t http_dl_get_to_buf_auth(const char *url, const char *auth_header,
                                  uint8_t **out, size_t *out_len) {
    if (!out || !out_len) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    *out_len = 0;
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return ESP_FAIL;
    if (auth_header && auth_header[0]) {
        esp_http_client_set_header(cli, "Authorization", auth_header);
    }
    esp_err_t err = esp_http_client_open(cli, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(cli);
        return err;
    }
    int total = esp_http_client_fetch_headers(cli);
    int status = esp_http_client_get_status_code(cli);
    if (status != 200) {
        ESP_LOGW(TAG_MAIN, "http_dl buffer GET status=%d", status);
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_FAIL;
    }
    int cap = (total > 0 && total < HTTP_DL_MAX_BUF) ? total : (32 * 1024);
    uint8_t *buf = heap_caps_malloc(cap + 1, MALLOC_CAP_SPIRAM);
    if (!buf) buf = malloc(cap + 1);
    if (!buf) {
        esp_http_client_close(cli);
        esp_http_client_cleanup(cli);
        return ESP_ERR_NO_MEM;
    }
    int read_total = 0;
    while (1) {
        if (read_total == cap) {
            int new_cap = cap + (32 * 1024);
            if (new_cap > HTTP_DL_MAX_BUF) {
                err = ESP_ERR_NO_MEM;
                break;
            }
            uint8_t *grown = heap_caps_realloc(buf, new_cap + 1, MALLOC_CAP_8BIT);
            if (!grown) {
                err = ESP_ERR_NO_MEM;
                break;
            }
            buf = grown;
            cap = new_cap;
        }
        int n = esp_http_client_read(cli, (char *)(buf + read_total), cap - read_total);
        if (n < 0) {
            err = ESP_FAIL;
            break;
        }
        if (n == 0) break;
        read_total += n;
        if (total > 0 && read_total >= total) break;
    }
    if (!esp_http_client_is_complete_data_received(cli)) err = ESP_FAIL;
    buf[read_total] = 0;
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    if (err != ESP_OK || read_total <= 0) {
        heap_caps_free(buf);
        return err != ESP_OK ? err : ESP_FAIL;
    }
    *out = buf;
    *out_len = read_total;
    return ESP_OK;
}
