# 2026-07-16 — Tài liệu kiến trúc + flowchart

Viết mô tả kiến trúc đầy đủ của firmware vào `docs/architecture/` (tiếng Việt, sơ đồ
mermaid render trực tiếp trên GitHub/VS Code).

## File tạo

| File | Nội dung |
|---|---|
| `README.md` | Mục lục + sơ đồ tổng luồng xét nghiệm + nguyên tắc kiến trúc |
| `01-tong-quan.md` | Thiết bị, phần cứng, ngăn xếp phần mềm, bản đồ module |
| `02-rtos-tasks.md` | 6 FreeRTOS task, phân bố core, mutex, giao tiếp |
| `03-quy-trinh-xet-nghiem.md` | Máy trạng thái `type_infor` (state diagram) + bảng chuyển + nhánh lỗi/abort |
| `04-nhiet-va-sensor.md` | PID nhiệt (e_pidstep), đọc opto (e_sensorStep), pipeline tính kết quả |
| `05-web-dashboard.md` | AsyncWebServer + SSE, điều khiển, SoftAP, heap (5 flowchart) |
| `06-mang-va-upload.md` | Boot, WiFi (3 đường), upload TLS, OTA, ràng buộc heap |

15 sơ đồ mermaid (flowchart / stateDiagram / sequenceDiagram).

## Cách làm

Dùng workflow 4 agent song song map các subsystem từ code thật (state machine, nhiệt/PID,
sensor/kết quả, boot/upload) — mỗi phát hiện có `file:line` — rồi tổng hợp thành tài liệu +
sơ đồ. Phần RTOS tasks + dashboard tự viết (đã nắm chắc từ khi build).

Đã ghi lại các **caveat độ chính xác** từ code (tin code hơn tên/comment):
- "30 phút" amplification không phải timer transition (do vòng đọc sensor đạt MEASUREMENTLOOPS).
- "67°C" thực là 65.8°C; enum `B_BLUE` = nút vật lý green; comment `epreheating67` sai.
- Giá trị sensor lưu là TỔNG 8 mẫu (slope/origin hấp thụ scale); `postData_GoogleSheet` luôn
  return 200 (không phản ánh trạng thái upload thật).

## Liên quan

- README.md + CLAUDE.md thêm link tới `docs/architecture/`.
- Chỉ là tài liệu — không đụng code, không cần nạp lại.
