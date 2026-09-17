/**
 * dashboard.h — web dashboard phục vụ NGAY TỪ MÁY (giám sát + điều khiển từ điện thoại/PC
 * cùng mạng LAN), học theo firmware/rapidplus/src/webDashboard.cpp (docs/architecture/05).
 *
 * Khác Rapid+ có chủ đích (ESP-IDF, không AsyncWebServer):
 *   - esp_http_server cổng 80; client POLL `GET /api/state` mỗi 1 s thay vì SSE (không phải
 *     giữ socket mở, hợp đồng đơn giản; ≤ 3 client × ~1 KB/s không đáng kể).
 *   - Asset nhúng bằng EMBED_TXTFILES (web/dashboard.html, 1 file, không lib ngoài — Rapid4P
 *     không có đường cong CT nên không cần Highcharts).
 *   - Lazy start: chỉ chạy khi STA đã có IP (poll wifi_mgr_is_connected() mỗi 1 s); dừng khi
 *     mất mạng hoặc khi portal provisioning cần cổng 80 (dashboard_suspend()).
 *   - Máy là nguồn sự thật: mọi số liệu lấy từ measure_last()/calib_store/ui_reader_state(),
 *     client không giữ trạng thái riêng (mở muộn vẫn thấy đúng).
 *
 * Routes: GET /  ·  GET /api/state  ·  POST /api/control?btn=measure|back  ·
 *         POST /api/threshold?sick=<PC|EHP|EMS|WSSV|TPD>&value=<0..9999>
 * Nút web = nút vật lý (ui_reader_on_measure_button / on_boot_button → display_schedule),
 * KHÔNG gọi lv_* từ task httpd (luật 2 CLAUDE.md).
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t dashboard_init(void);       /* tạo task giám sát mạng; gọi sau ui_reader_init */
void dashboard_suspend(void);         /* dừng server, giữ dừng (portal cần cổng 80) */
void dashboard_resume(void);          /* cho phép chạy lại; dash_task tự bật khi STA có IP */
bool dashboard_running(void);

#ifdef __cplusplus
}
#endif
