# 2026-07-29 — Mở QR bị reset: tràn stack trong encoder QR + SSID lệch hai nơi

## Triệu chứng

Sau khi sửa `dashboardStartAP()` (webDashboard.cpp:1534) đổi tên SoftAP `RAPID-<id>` → `FBT-<id>`
kèm fallback `if (tmpID[0] != 'R') tmpID = "RPL"`, **mở màn QR thì máy reset**.

## Ba lỗi, cùng một gốc: SSID được dựng ở BA chỗ độc lập

> **Cập nhật cùng ngày.** Bản đầu của tài liệu này viết "hai chỗ" — sai. Sau khi sửa, người dùng
> báo tiếp *"phần ID trên web SoftAP bị lỗi"*: còn **chỗ thứ ba**, `buildHomeJson()`
> (`webDashboard.cpp:328`), cũng nối `"RAPID-" + id_device` để trả `net.ssid` cho trang web →
> ở chế độ SoftAP **trang web hiển thị tên mạng không tồn tại**. Cộng thêm một chuỗi
> **người dùng đọc được** trong `data/script.js:1850` ("its RAPID-... hotspot"). Bài học nằm ở
> mục [Guard đã quá hẹp](#guard-đã-quá-hẹp) dưới cùng.

```cpp
// 1. webDashboard.cpp dashboardStartAP()  -> AP máy THẬT SỰ phát
String ap = "FBT-" + tmpID;                // đã sửa, có clamp

// 2. displayLCD.cpp screen_QR()           -> nội dung QR người vận hành quét
String ap = "RAPID-" + id_device;          // KHÔNG sửa, KHÔNG clamp  -> RESET

// 3. webDashboard.cpp buildHomeJson()     -> net.ssid trang web đọc
net["ssid"] = ap ? ("RAPID-" + id_device)  // KHÔNG sửa  -> web báo tên mạng không tồn tại
                 : ...;
```

### Lỗi 1 (chắc chắn): QR chỉ vào một AP không tồn tại

Máy phát `FBT-RPL03010`, QR bảo điện thoại join `RAPID-RPL03010`. Quét xong không vào được gì.
Đây là hệ quả trực tiếp của việc cùng một chuỗi được viết tay ở nhiều file.

### Lỗi 1b (cùng loại, báo sau): web ở SoftAP hiện tên mạng sai

`buildHomeJson()` đẩy `net.ssid` qua SSE `home` mỗi giây; ở chế độ AP nó cũng tự nối
`"RAPID-" + id_device`. Người vận hành mở dashboard **qua chính cái hotspot đó** và đọc được một
tên mạng máy chưa từng phát. Kèm theo `data/script.js:1850` — chuỗi hint trong panel WiFi —
cũng hardcode `"its RAPID-... hotspot"`, tức **văn bản người dùng đọc** cũng sai. Cả hai nay
lấy tên từ `dashboardApName()` / `net.ssid` thay vì tự viết.

### Lỗi 2 (nguyên nhân reset): payload QR không có chặn độ dài

`screen_QR()` mã hoá bằng **version 3 / ECC_LOW**, dung lượng byte-mode = **53 byte**.
`ricmoo/QRCode` **không tự kiểm** con số đó:

- `encodeDataCodewords()` không hề so `length` với `dataCapacity`.
- `bb_appendBits()` **không có bounds check** nào.
- đích ghi là `uint8_t codewordBytes[bm_bytes_len(moduleCount)]` = **71 byte VLA trên stack
  của DisplayTask**.

Payload dài hơn 53 byte → ghi thẳng ra ngoài mảng, đè stack DisplayTask → panic → **reset ngay
lúc màn QR mở**. Giá trị trả về của `qrcode_initText()` cũng đang bị bỏ qua.

### `id_device` dài tới đâu?

```cpp
id_device = EEPROM.readString(ADDR_ID_DEVICE_BASE);  // Bluetooth.cpp loadSettingDevice()
```

`EEPROM.readString(addr)` quét tìm byte NUL **từ `addr` tới hết buffer 4096 B**, *không* dừng ở
biên 40 B mà layout dành cho slot này (`170..210`, define.h). Máy nào có byte rác từ layout cũ ở
offset 170 sẽ nhận về chuỗi vài trăm ký tự.

Đó chính là lý do cái fallback `tmpID[0] != 'R'` được thêm vào — nó **cứu được AP**
(`WiFi.softAP()` chỉ từ chối khởi động khi SSID > 32 B, không crash) nhưng **không cứu QR**, vì
`screen_QR()` vẫn nối `id_device` thô. Khớp đúng triệu chứng: AP lên bình thường, mở QR thì reset.

## Sửa

Gộp về **một nguồn sự thật** thay vì vá từng chỗ:

| File | Việc |
|---|---|
| `webDashboard.cpp` | thêm **`dashboardApName()`** — `"FBT-" + id`, clamp `id` (rỗng / >24 / không bắt đầu `'R'` → `"RPL"`) |
| `webDashboard.h` | export `dashboardApName()` + ghi rõ **mọi** consumer phải gọi nó |
| `webDashboard.cpp` | `dashboardStartAP()` dùng `dashboardApName()` |
| `displayLCD.cpp` | `screen_QR()` dùng `dashboardApName()` — hết lệch prefix |
| `webDashboard.cpp` | `buildHomeJson()` → `net.ssid` dùng `dashboardApName()` (chỗ thứ ba) |
| `data/script.js` | hint panel WiFi bỏ hardcode `"RAPID-..."` → "its own hotspot" |
| 4 comment (`displayLCD.cpp` ×2, `displayCLD.h`, `Bluetooth.cpp`) | bỏ `"RAPID-<id>"` → trỏ về `dashboardApName()`; comment cũ chính là chỗ gieo bug |
| `displayLCD.cpp` | `screen_QR()` **chặn `payload.length() <= 53` VÀ kiểm return của `qrcode_initText()`**; quá dài → chỉ vẽ caption + "QR too long - type the address" (địa chỉ vẫn hiện dạng chữ nên người dùng gõ tay vào được) |
| `Bluetooth.cpp` | `loadSettingDevice()` clamp `id_device` về **24 ký tự** ngay lúc load |
| `displayLCD.cpp` | caption `.local` dùng `dashboardHostname()` thay `id_device` thô |

### Vì sao clamp ở `loadSettingDevice()` chứ không ở từng call site

`id_device` chảy vào nhiều thứ đều có giới hạn cứng: SSID SoftAP (802.11 cap 32 B), nhãn mDNS,
JSON upload, payload QR. Clamp **một lần ở cửa vào** là diff nhỏ hơn clamp ở 4 nơi, và không để
sót consumer nào thêm sau này. 24 là thoáng: phần còn lại của hệ thống lưu ID trong `char[10]`
(`parameter.device_id`) và `/deviceid` validate ≤ 9.

### Sổ tính worst case

```
"WIFI:T:nopass;S:" (16) + ";;" (2)   = 18
"FBT-"                               =  4
id_device clamp                      = 24
                                     ----
                                       46  ≤  53   ✔
```

## Kiểm

```bash
python tools/test_qr_payload.py   # MỚI
```

Guard khoá **cả bốn** mắt xích, vì payload chỉ nằm trong 53 B nhờ 3 clamp rải 2 file:

1. `dashboardApName()` còn tồn tại và còn clamp → tự **tính lại worst case** từ prefix + clamp
   đọc ra từ source, fail nếu > 53.
2. **Không chỗ nào trong `webDashboard.cpp` / `displayLCD.cpp` / `data/script.js`** inline lại
   literal `"RAPID-"`/`"FBT-"`, trừ chính `dashboardApName()` (bỏ comment trước khi soi — guard
   trip vì đọc phải văn xuôi là guard sẽ bị xoá).
3. `screen_QR()` còn chặn `payload.length()` **và** còn kiểm return của `qrcode_initText()`.
4. `loadSettingDevice()` còn clamp `id_device`.

Nếu `screen_QR()` đổi sang version QR khác, guard **báo lỗi** thay vì im lặng dùng sai hằng số
dung lượng.

Negative test (guard không fail được là guard vô dụng):

- bỏ dòng chặn độ dài → đỏ đúng 2 dòng ("REBOOT PATH" + "ignores return value") → gắn lại → xanh.
- trả `buildHomeJson()` về `"RAPID-" + id_device` → đỏ, chỉ đúng file + số dòng → sửa lại → xanh.

Build `pio run -e esp32dev` SUCCESS, 2 364 893 B (70.8%). `test_web_assets.py` đỏ một nhịp vì
`script.js.gz` cũ hơn `script.js` — **đó là guard làm đúng việc**; build regenerate `.gz` +
`webAssets.h` rồi xanh lại.

### Guard đã quá hẹp

Bản guard đầu chỉ soi **hai hàm có tên** (`dashboardStartAP`, `screen_QR`). Chính vì thế
`buildHomeJson()` — chỗ thứ ba, và là chỗ người vận hành *đọc* — lọt qua và phải chờ người dùng
báo mới thấy. Nay guard **quét cả file** và chỉ cho phép literal tồn tại trong đúng
`dashboardApName()`. Rút ra: khi bất biến là *"chuỗi này chỉ được sinh ở một nơi"*, đừng liệt kê
các nơi được phép gọi — hãy **cấm literal ở mọi nơi trừ chủ sở hữu**, vì danh sách call site luôn
thiếu đúng cái chưa ai biết.

## Cần xác nhận trên máy thật

Cơ chế suy ra từ đọc code + hành vi thư viện, **chưa đọc được panic trace**. Để chốt:

```bash
pio device monitor -b 115200     # rồi mở màn QR
```

- Nếu thấy `Guru Meditation ... / Stack canary watchpoint triggered (DisplayTask)` → đúng chẩn đoán.
- Nếu thấy nguyên nhân khác (LoadProhibited ở địa chỉ lạ, `task_wdt`) → còn lỗi khác, gửi trace.

In `id_device.length()` lúc boot cũng đủ để biết máy này có đúng là nạn nhân của
`EEPROM.readString` hay không.
