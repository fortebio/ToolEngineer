/**
 * dev_console.h — console dev trên UART0 (115200) để lái UI từ máy dev mà không chạm màn.
 *
 * Chỉ bật khi CONFIG_RAPID4P_DEV_CONSOLE=y (mặc định y khi còn bring-up; tắt ở bản phát
 * hành). Lệnh: `help`, `ui <0..12|start|settings|...>`, `btn do|boot`, `heap`. esp_hosted
 * đăng ký thêm crash/reboot/mem-dump… vào cùng console (chỉ khi console này chạy).
 * Gửi lệnh từ máy dev: `python scripts\uicmd.py COMxx "ui settings"` (mở cổng KHÔNG kéo
 * DTR/RTS — xem readlog.py).
 */
#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Gọi SAU ui_reader_init (lệnh ui/btn cần UI đã dựng). Không có console → ESP_OK, không làm gì. */
esp_err_t dev_console_start(void);

#ifdef __cplusplus
}
#endif
