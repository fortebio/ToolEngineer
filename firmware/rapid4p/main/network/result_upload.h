/**
 * result_upload.h — gửi kết quả đo lên Engineer Server, có hàng đợi offline trong NVS.
 *
 * Payload (thay Google Apps Script của ReaderPlus — MAPPING §3.6 / §5 mục 3). Server
 * catch-all POST chỉ đòi `id_device`; các mảng tên `result`/`CT_value`/`record_out`/
 * `amplification` PHẢI 10 phần tử (ARRAY_FIELDS của Rapid+) → Rapid4P KHÔNG dùng
 * các tên đó, dùng `slot_result`/`slot_value`/`slot_positive` (4 phần tử).
 *
 * {
 *   "id_device": "R4P00001", "version": "v0.1.0", "product": "rapid4p", "hw": "P4C5-43",
 *   "mac": "80:F1:B2:D1:53:C8", "method": "append", "type_Upload": "rapid4p_result",
 *   "sick": "EHP", "sample": "PRAWN Vannamei", "slots": 4,
 *   "slot_value": [v1,v2,v3,v4],        // lux×1000 vòng cuối
 *   "slot_result": [r1,r2,r3,r4],       // 0..3000 trung bình 3 vòng
 *   "slot_positive": [true,false,..],
 *   "value_sensor1..4": r1..r4,         // tên cũ của Sheet, giữ để tra cứu quen tay
 *   "threshold": 600, "calib_min": [...], "calib_max": [...],
 *   "time": "17-09-2026 10:00:00",      // giờ VN, server parse_ts
 *   "duration_ms": 34120
 * }
 * Chưa có hợp đồng JSON Schema trong system/contracts/ — việc của P2 (MAPPING §5.3).
 */
#pragma once
#include "esp_err.h"
#include "app/measure.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t result_upload_init(void);   /* nạp hàng đợi NVS, tạo task gửi */
/* Xếp kết quả vào hàng đợi (NVS) và đánh thức task gửi. Không chặn. */
esp_err_t result_upload_enqueue(const measure_result_t *r);
int result_upload_pending(void);
/* Thống kê: số đã gửi thành công từ lúc boot. */
int result_upload_sent(void);

#ifdef __cplusplus
}
#endif
