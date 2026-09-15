# 2026-07-29 — ID thiết bị lệch giữa header và Setting + iOS zoom khi chạm ô nhập

Hai vấn đề độc lập, báo cùng lúc.

## A. ID thiết bị: một giá trị, HAI store, không ai đồng bộ

### Hiện trạng

| Chỗ hiển thị | Đọc từ | Mặc định biên dịch | Có kiểm tính hợp lệ? |
|---|---|---|---|
| Header web + `setDevice` | `id_device` (EEPROM **170**, `EEPROM.readString`) | `"RAPIDPlus"` | **KHÔNG** |
| Setting → Device ID | `parameter.device_id` (EEPROM **512**, key `"device ID"`) | `"RPL"` (was `"proto 0"`, đổi 2026-07-30) | có, qua `parameter.length` |

Hai mặc định là **hai chuỗi khác nhau**, nên trên máy chưa từng bấm Save ở thẻ đó, header và
Setting hiện hai giá trị khác nhau **theo thiết kế**. Không phải hiển thị sai — là hai store thật.

`id_device` là bản **đang được dùng thật**: header, QR, nhãn mDNS, và
`dataPostGoogleSheet["id_device"]` (Bluetooth.cpp:543 — tức mọi bản ghi lên Google Sheet/ERP).

### Ba lỗi cụ thể

**1. Thẻ Setting GHI một store, ĐỌC store khác.**
`renderDeviceId()` đổ giá trị đầu từ `cfgCache["device ID"]` (= `parameter.device_id`) nhưng nút
Save lại `POST /deviceid` → `PEND_ID` → ghi **cả hai**. Nghĩa là người vận hành nhìn thấy
giá trị của store kia, tưởng ID sai, rồi "sửa" một giá trị chưa bao giờ được dùng.

**2. Serial/BT và `POST /config` ghi `parameter.device_id` MỘT MÌNH.**
`JsonDataConfig()` (ForteSetting.cpp:352-357) áp key `"device ID"` bằng `strlcpy` vào
`parameter.device_id` rồi commit — **không chạm `id_device`**. Đây là đường máy hiện một ID mà
upload gửi một ID khác.

**3. `id_device` không hề được validate lúc load.**
`EEPROM.readString(addr)` quét NUL **tới hết buffer 4096 B**, không dừng ở biên 40 B của slot
(`170..210`, define.h). Máy có byte rác ở 170 → chuỗi vài trăm ký tự, hoặc rác ngắn trông như ID.
Hậu quả không chỉ là hiển thị xấu: SSID > 32 B làm `WiFi.softAP()` **không khởi động**, và trước
bản vá QR nó **reboot máy** (xem [2026-07-29-qr-reset-ssid-drift.md](2026-07-29-qr-reset-ssid-drift.md)).

### Sửa

| File | Việc |
|---|---|
| `data/script.js` | thêm `deviceIdNow` (từ SSE `home.device`); `renderDeviceId()` đổ giá trị từ đó — **đọc đúng store mình ghi** |
| `src/Bluetooth.cpp` | thêm `plausibleDeviceId()` (1..9 ký tự in được); `loadSettingDevice()` validate → không hợp lệ thì lấy `parameter.device_id` (có `probe.length` gác) → vẫn không được thì `"UNSET"` |
| `src/ForteSetting.cpp` | `JsonDataConfig()` **mirror** ID sang `id_device` + `saveSettingDevice()` — chỉ khi key `"device ID"` **có mặt**, và **sau `eepromUnlock()`** |

#### Vì sao `1..9 ký tự in được` không phải con số tuỳ ý

**Mọi** đường ghi đều cap ở 9: `POST /deviceid` validate `1..9`, `handleConfigPost` từ chối `> 9`,
và `parameter.device_id` là `char[10]`. Nên "1..9 in được" chính là **hình dạng duy nhất một lần
ghi thật có thể tạo ra** — cái gì khác là rác **chứng minh được**, không phải chỉ "trông đáng ngờ".
Guard `test_device_id.py` khoá việc **cả bốn giới hạn phải khớp**: lệch một cái là loader bắt đầu
từ chối ID mà writer vẫn nhận.

#### `"UNSET"` thay vì đoán

Không có store nào dùng được → hiện `"UNSET"` trên **cả** TFT start screen và header web. Thà nói
thẳng là chưa cấu hình, hơn là để người vận hành tin vào một chuỗi trông hợp lý do tai nạn.
Đường sửa: Setting → Device ID → Save (ghi cả hai store).

#### Hai bẫy khi mirror ID trong `JsonDataConfig()`

1. **Chỉ mirror khi key có mặt.** Mirror vô điều kiện nghĩa là Save thẻ **LED/PID** cũng âm thầm
   thay ID upload bằng giá trị store kia đang giữ (mà theo báo cáo thì giá trị đó đang sai).
2. **Phải nằm SAU `eepromUnlock()`.** `saveSettingDevice()` lấy lại **cùng** mutex
   `gEepromMutex` — plain, không đệ quy (CLAUDE.md Setting #2). Lồng hai section = **deadlock**.
   Guard kiểm luôn vị trí này, không chỉ sự tồn tại của lời gọi.

`char[10]` từ EEPROM **không đảm bảo có NUL** — cả hai chỗ đọc nó đều copy sang buffer
`[sizeof + 1] = {0}` trước khi cho `String` đọc.

## B. iOS: chạm ô nhập là trang tự zoom

### Gốc rễ

iOS Safari **zoom cả viewport** khi focus một text input có `font-size` **< 16px**. Toàn bộ control
trên trang đều dưới ngưỡng:

| Control | Cũ | = px |
|---|---|---|
| `.sample-name`, `.slot-name` | `0.85rem` | 13.6 |
| `.f-in`, `.f-sel` | `0.9rem` | 14.4 |

Nên không riêng ô sample — mật khẩu WiFi, form config, dropdown bệnh đều nhảy. Ô sample chỉ là
cái phải chạm **mỗi lần chạy**.

### Sửa: một rule cho tất cả

```css
@media (hover: none) and (pointer: coarse) {
  .sample-name, .slot-name, .disease-sel, .f-in, .f-sel { font-size: 16px; }
}
```

Scope theo `pointer: coarse` để desktop giữ bảng gọn 13.6px.

### KHÔNG dùng `maximum-scale=1`

Đây là cách "fix" phổ biến trên mạng và nó **sai ở đây**: nó chặn zoom bằng cách **tước pinch-zoom
của tất cả mọi người**, kể cả người thị lực kém, trên một **thiết bị y tế**. 16px **bỏ đi lý do
zoom** chứ không bỏ đi khả năng zoom. Guard fail nếu `maximum-scale`/`user-scalable=no` xuất hiện
trong `index.html`.

## Kiểm

```bash
python tools/test_device_id.py    # MỚI
```

Khoá 5 bất biến: (1) bốn giới hạn độ dài khớp nhau, (2) loader validate + fallback có
`probe.length` gác + từ chối byte không in được, (3) mirror có guard `containsKey` **và** nằm
ngoài `eepromLock`, (4) thẻ Setting đọc `deviceIdNow` chứ không phải `cfgCache["device ID"]`,
(5) rule 16px touch còn đủ 5 selector + viewport không chặn zoom.

Negative test từng cái (guard không fail được là guard vô dụng):

| Gieo lỗi | Guard báo |
|---|---|
| `/deviceid` cho 12 ký tự | `limits disagree: ... POST /deviceid=12, plausibleDeviceId=9` |
| thẻ Setting đọc lại `cfgCache` | 2 dòng: mất `deviceIdNow` + đọc `cfgCache["device ID"]` |
| hạ rule touch về 14px | `no coarse-pointer rule setting form controls to 16px` |

`test_qr_payload.py` check #4 phải sửa kèm: nó tìm clamp `id_device.length() > 24` cũ, nay đã bị
`plausibleDeviceId()` (chặt hơn) thay. Nay nhận cả hai dạng; giới hạn chính xác do
`test_device_id.py` khoá.

Build `pio run -e esp32dev` SUCCESS, 2 366 969 B (70.8%). 7/7 guard xanh.

## Cần làm trên máy thật

1. Nạp `upload` → xem Serial lúc boot: `[id] no usable device ID in EEPROM -> "UNSET"` hoặc
   `[id] EEPROM 170 unusable -> using parameter.device_id "..."` cho biết store nào đang có gì.
2. Setting → Device ID → nhập ID thật → Save. Sau đó **header, thẻ Setting, QR và upload phải
   cùng một giá trị**.
3. Trên iPhone: chạm ô sample trong tab Result → trang **không** được zoom/nhảy; pinch-zoom thủ
   công vẫn phải hoạt động.
