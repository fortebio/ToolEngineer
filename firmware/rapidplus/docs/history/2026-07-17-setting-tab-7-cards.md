# 2026-07-17 — Tab Setting: 7 card (WiFi, ID, Profile, LED, Calib, PID, Khác)

Setting thành **master-detail**: lưới 7 card → bấm mở panel form. Khoá toàn bộ khi máy
đang chạy (theo quyết định).

## Kiến trúc: 7 card nhưng chỉ 4 route

`ForteSetting::JsonDataConfig()` đã parse JSON parameter và áp dụng **chỉ các key có
mặt** (mọi field đều bọc `containsKey`), rồi ghi cả struct vào EEPROM. Nên 7 card chỉ là
**7 tập key** trên cùng một JSON — không cần 7 endpoint, và tái dùng parser đã kiểm chứng.

| Route | Việc |
| --- | --- |
| `GET /config` | toàn bộ parameter → đổ vào form |
| `POST /config` | validate → **xếp hàng** → SettingTask áp bằng `JsonDataConfig()` |
| `GET /wifiscan` | **async**: 202 khi đang quét, 200 + list khi xong |
| `POST /wifi` · `/deviceid` · `/calib` | credentials / id / wizard calib |

**Thread-safety**: AsyncTCP chỉ *validate + enqueue + trả lời*. Mọi ghi EEPROM dồn về
**SettingTask** (`drainPending()`), đúng khuôn `postShortPress` → InputTask. Nếu 2 task
cùng `EEPROM.begin/end` thì buffer 4096B bị giải phóng dưới chân nhau.

`paraToJson()` tách ra từ `paraDisplay()` (vốn dựng đúng JSON rồi vứt ra Serial); thêm
`top heater PWM` mà bản gốc bỏ sót nên GET không prefill được.

## Bảy cái bẫy bản đồ hoá code lôi ra (tránh được trước khi code)

1. **`"para version"` bắt buộc** — thiếu là `JsonDataConfig()` áp dụng **không gì cả**
   nhưng vẫn `return true`. Im lặng giả vờ thành công. Server tự chèn.
2. **Firmware không validate khoảng nào.** Setpoint/PID/overheat đi thẳng vào vòng điều
   khiển rồi ghi EEPROM; mảng ghi vào C array **không check bounds**. `handleConfigPost`
   là **trust boundary của heater**, không phải phép lịch sự UI:
   - `amplification time` ≤ **130** — lớn hơn là **tràn `sensor67Value[10][130]` giữa run**
   - mật khẩu WiFi ≤ **54** ký tự (khe EEPROM `ADDR_PASSWORD=75..129`), **không phải 63**
     của WPA2 — dài hơn sẽ **âm thầm đè field kế**
   - `device ID` / `units` ≤ **9** (`char[10]`, `strcpy` không check)
   - mảng phải **đúng độ dài** (LED 10, PID 3, Top overheat 2, temp calib 6...)
3. **Khoá client là vô nghĩa** — curl hay tab cũ vẫn POST được. Server chặn **409** *và*
   SettingTask **kiểm lại** `dashboardDeviceBusy()` ngay trước khi áp → đóng TOCTOU
   (user bấm nút chạy giữa lúc POST và lúc apply).
4. **`phase` không đủ để khoá** → thêm `status.busy` tính bằng **allowlist** (`isBusy()`):
   calib/OTA/chờ ống đều báo `phase "idle"`, denylist sẽ để lọt.
5. **Device ID ở HAI nơi**: global `id_device` (EEPROM 170, dashboard + upload dùng) và
   `parameter.device_id` (EEPROM 512). Sửa một cái không đổi cái kia → `PEND_ID` ghi cả hai.
6. **`Wifi_Connect()` không dùng được từ web** (blocking captive portal, tắt WDT, luôn
   restart). Và **không thể nối STA tại chỗ**: một radio, đổi kênh là **rớt hết client
   SoftAP** kể cả browser vừa POST. → lưu → **trả response** → SettingTask reboot sau 1.5s.
7. **`parastructure` = 400B, giới hạn 402B** (`sizeof(parameter) > 512-110`) → **không
   thêm field mới** vào struct.

## Card Calib: wizard, không phải nút bấm

Quy trình thật: BLUE **long-press** → RED sấy 55°C → chờ **5 phút** → chọn slot → **4 lần
đo, mỗi lần thay ống thật** (300/200/100/0) + bấm BLUE. Web chỉ lái nút, không tự đo.

- Thêm **`buttonManager::postLongPress()`** — `postShortPress` không tới được calib (bấm
  ngắn ở `escreenStart` chạy `epreheating80`, tức sấy 80°C cho run thường).
- Firmware **không có cancel sạch** (WHITE ở `eSelectSlot` gọi `ESP.restart()`) →
  `/calib?action=cancel` tự gỡ `flag_calib_done` + `type_calib` + về `escreenStart`.
  Thiếu bước này: `flag_calib_done` latch lại → lần calib **sau chết cứng** (handler bị
  guard bởi `!flag_calib_done`).
- Chọn slot **ghi thẳng `_displayCLD.slot`**, không bấm RED nhiều lần: `pendingEvent` chỉ
  có **1 ô**, các press trong cùng 5ms **gộp làm một** → slot sai.
- Client theo `status.calib` (**tên bước**, không phải số enum thô).
- Calib **chỉ ghi `slopes`**, không ghi `origins` — UI nói đúng như vậy.

## Client: bảng khai báo + 1 renderer

7 form sinh từ **bảng `CARDS`** (path chấm cho key lồng: `parameters.sg order`), một
renderer chung dựng số/mảng/ma trận/select. Viết tay 7 form là cách nhanh nhất để các
shape lệch nhau.

## Bug CSS bắt được bằng screenshot

`.hide{display:none}` và `.set-grid{display:grid}` **cùng specificity** (1 class) → rule
định nghĩa **sau** thắng ⇒ panel render **dưới** menu thay vì thay thế nó. DOM query vẫn
thấy field nên test logic **pass**, chỉ ảnh chụp mới lộ. Đúng lớp bug từng cắn dự án
(`#screen-home` thắng `.screen{display:none}`). Sửa: `.hide{display:none !important}` —
utility phải luôn thắng.

## Mock: 1 khiếm khuyết fidelity nữa

Mock **tự sấy ngay khi boot** → luôn `busy` → Setting không test được. Máy thật boot vào
`escreenStart` (**idle**, sửa cài đặt được). Sửa: mock boot idle, **bấm GREEN (Lysis)**
mới bắt đầu — trùng với `test_full_run.js` (đã cập nhật để bấm green trước).
Thêm `/config`, `/wifiscan` (mô phỏng async), `/deviceid`, `/wifi`, `/calib` vào mock.

## Kiểm chứng

- `pio run -e esp32dev` → **SUCCESS** (RAM 22.9%, Flash 68.6%).
- Endpoint (mock): boot idle → unlocked; merge một phần (lysis 82→79.5, key khác nguyên);
  id 10 ký tự **bị từ chối**; pass 60 ký tự **bị từ chối**; async scan 202→200;
  **POST config khi đang calib → 409 VÀ không được áp dụng**.
- UI (Edge headless + CDP): 7 card; Profile 6 field **prefill từ config thật**; LED 10 ô;
  WiFi 4 mạng sắp theo tín hiệu; wizard calib 8 bước; đang calib → card **xám + banner khoá**.

## Nạp

Đổi cả firmware + `data/` → `pio run -e esp32dev -t upload` **và** `-t uploadfs`.
