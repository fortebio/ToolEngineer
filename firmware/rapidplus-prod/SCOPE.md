# Phạm vi — FBT-RapidPlus Production

> Mục đích: chặn scope creep. Phân vân "có nên làm X" → đọc đây trước. Không nằm
> trong "Trong phạm vi" → mặc định là **không**.

## Trong phạm vi

- Firmware production máy PCR/qPCR RapidPlus trên ESP32-WROOM-32.
- Điều khiển nhiệt (PID bottom + hot-lid) với hard-limit độc lập.
- Thu nhận quang học 10 kênh và thuật toán ra kết quả.
- Vận hành standalone offline-first (máy phải chạy được khi mất mạng).
- Lớp kết nối cloud: OTA có verify, remote config có validate, telemetry.
- UI tại máy, lưu trữ cục bộ, export.

## Ngoài phạm vi (cho tới khi có yêu cầu tường minh)

- **Đổi MCU.** WROOM-32 là ràng buộc, không phải lựa chọn mở.
- Chứng nhận IEC chính thức — ta viết *theo tinh thần* 61010/62304, chưa chứng nhận.
- Backend cloud, web admin — nằm ở `FBT/backend/`, `FBT/frontend/`.
- Multi-tenant, phân quyền phức tạp.
- Bất cứ thứ gì block vòng PID 100 ms hoặc vòng đo 20 s.

## Quyết định đã chốt

| Quyết định | Lý do |
|---|---|
| Logic an toàn là C++ thuần, test trên host | Logic chỉ test được bằng máy thật = thực tế không ai test |
| Hard-limit độc lập với PID | PID sai vẫn phải có lớp cắt |
| `slope` sai thì **từ chối**, không clamp | Kết quả đo sai âm thầm nguy hiểm hơn từ chối chạy |
| Cert ghim xét **trước** hash | Kênh không xác thực thì hash cũng do kẻ tấn công cung cấp |
| Hai app slot | Một slot = OTA hỏng là brick, không đường lùi |
| Toolchain ghim chính xác | Đổi codegen trên chip không PSRAM = đổi layout RAM |
| Giữ đường offline | Máy phải chạy được khi mất mạng |

## Chưa chốt — cần quyết

- [ ] **Ngưỡng hard-limit chính thức** (bottom/hot-lid) — PRD Q3, cần team nhiệt/sinh học.
- [ ] **Có cầu chì nhiệt phần cứng độc lập không?** Nếu không, firmware là lớp bảo vệ duy nhất.
- [ ] Port từ `instrument-wroom32` hay viết lại từng module?
- [ ] Hỗ trợ PCB V1.2 và V1.3 bằng một build hay hai build?
- [ ] Ngân sách RAM cho TLS + MQTT — chưa đo, là rủi ro NFR-01 lớn nhất.

## Thứ tự bắt buộc

Theo PRD §P1.5: **an toàn/bảo mật xong TRƯỚC khi mở remote config/OTA fleet.**

```
P1.5 (D6-01, D5-01, D1-01/02, D2-01, D5-02)  →  remote config / OTA fleet
```

Không đảo thứ tự. Remote config biến một bug an toàn thành **nguy hiểm vật lý
diện rộng** — đó là rủi ro cao nhất trong sổ rủi ro của PRD.
