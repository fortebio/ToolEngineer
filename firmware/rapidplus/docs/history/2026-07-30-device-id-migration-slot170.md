# 2026-07-30 — Nâng cấp lên v2.4.3 mất Device ID: migration từ EEPROM slot 170

## Triệu chứng

Nâng v2.4.2 → v2.4.3, máy **mất Device ID**. Không hiện `"UNSET"` mà hiện `"proto 0"` — một chuỗi
trông hợp lệ, nên không có dấu hiệu cảnh báo nào.

## Vì sao mất

| | v2.4.2 | v2.4.3 |
|---|---|---|
| ID **đang dùng thật** (header, QR, mDNS, upload) | `id_device` → `EEPROM.writeString(**170**)` | *(global đã xoá)* |
| `parameter.device_id` (EEPROM 512) | mặc định **`"proto 0"`**, chỉ Serial/BT ghi — hầu như không ai dùng | **store duy nhất** |
| `ADDR_CHECK_ID_DEVICE` (**210**) | chỉ là `bool` | ô dấu migration |

`sizeof(parastructure)` **không đổi**, nên khối parameter sống sót nguyên vẹn — kèm luôn
`device_id = "proto 0"`. Đó là 7 ký tự in được nên `sanitiseDeviceId()` **chấp nhận**, và ID thật
ở slot 170 không còn ai đọc.

### Bản vá đầu tiên sai ở đâu

```cpp
String tmp = parameter.device_id + String("\0");
if (tmp != EEPROM.readString(ADDR_CHECK_ID_DEVICE))   // 210 = ô BOOL, không phải ID
    parameter.device_id = EEPROM.readString(...).c_str();  // không compile: char[10]
```

1. **Sai địa chỉ** — ID ở `ADDR_ID_DEVICE_BASE` (170), 210 chỉ là cờ 1 byte.
2. `parameter.device_id = ...` không gán được cho `char[10]` → phải comment lại.
3. `+ String("\0")` vô nghĩa.
4. **So giá trị** thay vì đánh dấu một lần: sau migration, mỗi lần đổi ID qua web thì boot sau lại
   bị ghi đè ngược. Vòng lặp.
5. `EEPROM.readString()` **không dừng ở biên 40 B** của slot — quét NUL tới hết buffer 4096.

## Bẫy lớn nhất — và là lý do phải kiểm định đối kháng

Đề xuất ban đầu của tôi là: *"nhận ID cũ ở 170 nếu `parameter.device_id` đang là placeholder"*.
**Cả 3 hướng tấn công đều bác bỏ nó.** Cái chí mạng:

```cpp
// v2.4.2: src/Bluetooth.cpp — Wifi_Connect()
WiFiManagerParameter custom_id_device("id_device", "Enter ID Device", "RPL", 40);
//                                                  ^^^^^ tham số 3 = GIÁ TRỊ ĐIỀN SẴN
...
id_device = custom_id_device.getValue();   // lưu dù người dùng không đụng ô đó
saveSettingDevice();                        // -> EEPROM.writeString(170, "RPL")
```

Portal v2.4.2 **điền sẵn `"RPL"`** vào ô ID, mà ô đó nằm chung trang với ô WiFi. **Mọi operator chỉ
vào portal để nhập WiFi đều đã ghi `"RPL"` vào slot 170.**

Đề xuất của tôi coi `"RPL"` là placeholder khi thấy ở `parameter.device_id`, nhưng lại coi là ID
thật khi thấy ở 170 — **bất đối xứng**. Hậu quả nếu ship: một phần lớn fleet migrate về **cùng một
ID `"RPL"`**, nó qua được `sanitiseDeviceId()` nên **không phân biệt được với serial thật**,
migration không chạy lại được nữa, và tất cả upload lên Google Sheet/ERP dưới **một định danh
chung**, cùng trỏ về `rpl.local`.

Chính v2.4.2 cũng coi đó là "chưa cấu hình": `strncmp(id_device, "RPL", 3) == 0` → đặt tên AP khác.

## Thiết kế cuối

### 1. `idIsPlaceholder()` — áp cho **cả hai** phía

`{"RPL", "proto 0", "UNSET"}`, so bằng **`strcmp` chính xác, không phải prefix** — serial thật là
`"RPL03010"` phải sống sót. Đặt trong `sanitiseDeviceId()` nên **mọi đường ghi tự thừa hưởng**
(`JsonDataConfig()` và `PEND_ID` đều gọi nó trước khi persist) — không cần copy check ra chỗ khác.

### 2. Dấu một lần, không so giá trị

`ADDR_CHECK_ID_DEVICE` (210) stamp `0xA5` = "đã migrate". Ô này **không chứa định danh** — v2.4.2
dùng nó làm cờ bool.

- **Đọc bằng `EEPROM.read()`, KHÔNG `readBool()`**: EEPROM trắng là `0xFF` → `readBool` trả **true**
  → mọi máy mới sẽ bỏ qua migration.
- v2.4.2 ghi `0` vào ô này mỗi lần đổi ID → downgrade rồi sửa lại sẽ **tự động re-arm** migration.
- Dấu một lần là thứ khiến "operator đổi ID ở 2.4.3" **không bị revert** ở lần boot sau.

### 3. Đọc có chặn biên

40 byte vào `char legacy[41]`, ép NUL cuối. `(unsigned char)` khi kiểm ký tự in được — `char` trên
xtensa là **signed**, byte trinh nguyên `0xFF` sẽ so `< 0x20`.

### 4. Không nhận thì phải **ồn**

Legacy dài 10..40 ký tự (ô portal nhận tới 40, không ai clamp) → **từ chối, không cắt cụt**: một ID
bị cắt sẽ khớp *không* bản ghi nào trên cloud — tệ hơn là không có. Máy giữ `"UNSET"` và in
Serial để kỹ sư biết phải nhập lại.

### 5. Không đụng slot 170

Không ghi, không xoá — downgrade về v2.4.2 vẫn phải tìm thấy ID ở đó.

### 6. Persist chỉ khi khối parameter hợp lệ

`paraEEPROM.length == sizeof(parameter)`. EEPROM trắng thì **không** ghi mặc định đè lên, và
**không** đóng dấu → migration thử lại lần boot sau, prompt "please initialize" vẫn hiện.

### 7. Bỏ gate `FirmwareVer`

Đoạn hỏng nằm trong `if (FirmwareVer == "v2.4.3")`. Đó là global đổi mỗi bản phát hành, nên máy
nhảy 2.4.2 → 2.4.4 sẽ **bỏ qua migration**. Gate theo **dữ liệu**. Sửa kèm: **seed `kpid3` cũng
đang bị gate y hệt** — máy nhảy version sẽ chạy PID hotlid với `kpid3 == {0,0,0}`.

### 8. Mặc định biên dịch `"RPL"` → `""`

Một mặc định *trông giống serial* là một mặc định máy sẽ vô tư upload dưới tên đó. Rỗng →
`sanitiseDeviceId()` báo `"UNSET"`. `sizeof(parastructure)` không đổi nên vẫn an toàn khi nâng cấp.

## Kiểm

`tools/test_device_id.py` mục 2 viết lại — slot 170 **không còn "chết"**, nó là **nguồn đọc
read-only**:

- cấm `EEPROM.write*/put(ADDR_ID_DEVICE_BASE)` ở mọi file (ghi = dựng lại store thứ hai);
- cấm `EEPROM.readString()` trên hai slot legacy (không chặn biên);
- `begin()` **không được** nhắc `FirmwareVer`, **phải** có `ADDR_CHECK_ID_DEVICE`, **không** được
  dùng `readBool`;
- `idIsPlaceholder()` phải còn `"RPL"` + `"proto 0"` và phải dùng `strcmp`;
- `parastructure.device_id` không được có mặc định khác rỗng.

**Bẫy khi viết guard**: `code_only()` bóc **cả string literal**, nên check "còn literal `"RPL"`
không" phải chỉ bóc comment. Guard tự làm mù chính nó là guard vô dụng.

Negative test 5/5 đỏ đúng dòng:

| Gieo lỗi | Guard báo |
|---|---|
| bỏ `"RPL"` khỏi blacklist | `no longer rejects "RPL" - see the v2.4.2 portal default` |
| `strcmp` → prefix match | `a PREFIX match would reject real serials like "RPL03010"` |
| stamp đọc bằng `readBool` | `virgin EEPROM byte is 0xFF, which reads as TRUE` |
| trả mặc định về `"RPL"` | `a default that looks like a serial is one the machine will upload under` |
| xoá hẳn migration | `upgrading from v2.4.2 silently re-keys the machine in the Google Sheet / ERP` |

Build `pio run -e esp32dev` SUCCESS, **2 389 661 B (71.5%)**. 7/7 guard xanh.

## Xác nhận trên máy thật

Nạp `upload` rồi xem Serial lúc boot — đúng **một** trong ba dòng:

```
[id] migrated device ID from EEPROM slot 170 -> 'RPL03010'      <- migrate thành công
[id] legacy slot 170 holds 14 bytes, not a usable device ID     <- có gì đó nhưng không dùng được
[id] no usable device ID in EEPROM -> "UNSET"                   <- không có gì
```

**Đừng xác nhận bằng màn QR / tên SoftAP**: `dashboardApName()` đổi mọi ID không bắt đầu bằng `'R'`
thành `"RPL"`, nên SSID có thể là `FBT-RPL` trong khi store đã đúng. Xem dòng Serial hoặc header web.

Sau đó kiểm boot lần 2: **không** được có dòng `[id] migrated` nữa (dấu đã đóng), và đổi ID qua web
rồi reboot cũng **không** được quay về giá trị cũ.

## Cố ý KHÔNG làm

- **Không xoá/ghi slot 170** — giữ đường downgrade. Đánh đổi: dấu `0xA5` làm `readBool(210)` của
  v2.4.2 thành true, nên máy downgrade sẽ đặt tên AP portal là `FBT RAPIDPlus` thay vì `FBT <id>`
  cho tới lần lưu portal kế tiếp. Chỉ ảnh hưởng tên AP của portal, tự khỏi.
- **Không cắt cụt ID 10..40 ký tự**, không nới `char[10]` (guard ghim số 9 ở cả ba đường vào).
- **Không thêm check vào `handleDeviceId()`/`JsonDataConfig()`** — cả hai đã gọi
  `sanitiseDeviceId()` trước khi persist. Thêm bản sao thứ hai chính là bug hai-store thu nhỏ.
- **Không `eepromLock()`** — `begin()` chạy ở `main.cpp` trước `xTaskCreate` đầu tiên, và dùng
  chung phiên `EEPROM.begin()/end()` mà seed `kpid3` vẫn ghi qua. Thêm lock ở đây sẽ deadlock
  ngày ai đó (đúng đắn) bọc phiên đó lại — mutex không đệ quy.
- **Không reboot sau migration** — `begin()` chạy trước cả ba chỗ latch ID
  (`WiFi.setHostname`, `MDNS.begin`, `WiFi.softAP`) và trước mọi reader của payload upload.
