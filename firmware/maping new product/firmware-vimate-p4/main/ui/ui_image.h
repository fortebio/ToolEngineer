/**
 * ui_image.h — Hiển thị 1 ảnh full-screen.
 *
 * 2 API:
 *  - ui_image_show(url): legacy, firmware tự download HTTPS (DEPRECATED — gây mbedtls TLS conflict với WS)
 *  - ui_image_show_jpg_buffer(data, len): server đẩy JPG bytes qua WS, firmware chỉ decode → display
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

bool ui_image_show(const char *url);
esp_err_t ui_image_worker_start(void);
bool ui_image_show_async(const char *url);
/* Tải vào cùng buffer preview nhưng đặt phía sau carousel Home. Chỉ giữ một bìa
 * tại một thời điểm để RAM không tăng theo số khóa học. */
bool ui_image_show_home_cover_async(const char *url);
/* true nếu bìa Home cho URL này đã decode và còn trong kho riêng (apply_home dùng để
 * giữ nguyên bìa đang hiện thay vì ẩn → placeholder → decode lại). */
bool ui_image_home_cover_ready(const char *url);
/* Như ui_image_show_async nhưng đánh dấu ảnh là frame màn chờ slideshow
 * (as_slideshow=true) → display giữ chế độ đồng hồ-nền-ảnh và KHÔNG tự diệt
 * slideshow. Ảnh nội dung thường (bài học) phải dùng as_slideshow=false. */
bool ui_image_show_async_ex(const char *url, bool as_slideshow);
/* Server pre-push JPG bytes inline qua WS — KHÔNG HTTP download.
 * Tránh conflict mbedtls TLS giữa WS task và HTTPS client. */
void ui_image_show_jpg_buffer(const uint8_t *jpg_data, size_t jpg_len);
void ui_image_hide(void);
void ui_image_prefetch(const char *url);

#ifdef __cplusplus
}
#endif
