# 2026-07-16 — Fix: không kết nối được WiFi SoftAP (RAPID-<id>)

## Triệu chứng
AP `RAPID-RPL03010` phát bình thường (thấy được, Open, signal mạnh) nhưng điện thoại/PC
**không kết nối được** ("không lấy được IP" / connect fail).

## Nguyên nhân
Ở chế độ AP fallback, **Bluetooth vẫn resident (~60-80KB)** + AsyncWebServer + AP →
free heap còn rất thấp. Khi client xin **DHCP** (cấp IP) thì thiếu bộ nhớ → client
associate được nhưng không nhận được IP → báo không kết nối.

## Fix
`webDashboard.cpp dashboardStartAP()`: gọi **`releaseBluetoothStack()` TRƯỚC** khi
`WiFi.softAP()` → giải phóng ~60KB cho DHCP + phục vụ trang.

- An toàn: `releaseBluetoothStack()` gọi được cả khi BT chưa `begin()` (SerialBT.end()
  no-op, các deinit guard theo status, `esp_bt_mem_release` giải phóng vùng reserved).
- Chỉ release ở **AP fallback** (STA fail). Chế độ STA bình thường không đụng → BT config
  vẫn dùng được. Release là 1 chiều → reboot khôi phục BT/STA.

Compile SUCCESS. Serial sau khi nạp sẽ in `[dash] SoftAP 'RAPID-...' ... free=<cao hơn nhiều>`.

## Nạp
`pio run -e esp32dev -t upload` (chỉ firmware). Rồi để STA fail (không có WiFi đã lưu) →
nối phone vào `RAPID-<id>` → mở `http://192.168.4.1/`.
