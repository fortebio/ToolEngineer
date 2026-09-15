# 2026-07-22 — `/reviewlast` chập chờn khi vừa boot: race EEPROM 4KB buffer

## Triệu chứng

`POST /reviewlast` **fail khi máy vừa boot** (preheat, count=0, không log `[review] reloaded`)
nhưng **pass khi đã chạy lâu** (maintain, count=120). POST trả 200 (không 409), nên là
**probe đọc thất bại thật** (`sensor67Value[0][0]` rơi ngoài `10..60000`), không phải bị guard từ chối.

## Điều tra — loại trừ các giả thuyết sai (workflow 4-agent)

- ❌ **Race trên `sensor67Value`**: `eSensorPreheat`/`eSensorMaintain` đọc code đầy đủ — **không hề
  ghi** `sensor67Value` (chỉ toggle LED + timer `sensor67ValueTime`). Chỉ `eSensor1stReadingFunc`
  ghi, mà nó chạy khi amplification (busy, bị guard chặn). Vậy không có race ghi-đè mảng.
- ❌ **preheat vs maintain**: cả hai không đụng `sensor67Value` → không giải thích được.
- ❌ **Busy guard 409**: `type_infor=escreenStart` ở idle → `isBusy=false` → review **chạy tới probe**.
- ❌ **Record rỗng**: bằng chứng runtime — up-a-while PASS (count=120) chứng minh RECORDPOS có run
  hợp lệ; nếu rỗng thì cả hai lần đều fail.

## Gốc rễ — race **EEPROM 4KB shared buffer** (double-free), thiên cold-boot

Đối tượng `EEPROM` toàn cục của Arduino chỉ có **một buffer 4096B**: `begin()` realloc, `end()`
free nó. `getDataAmplificationEEPROM` (`begin→get(RECORDPOS)→end`, SettingTask) chia sẻ buffer đó
với `error.saveErrorToEEPROM()` — gọi từ **~17 đường PID safety** trên **ControlTask** (core 1,
mỗi 100ms), **không mutex nào**. Nếu ControlTask `begin()/end()` xen giữa `begin()` và `get()` của
SettingTask → review đọc từ buffer đã free/realloc → `tmp` hỏng → probe ngoài `(10,60000)` → fail.

**Vì sao thiên cold-boot:** các đường PID safety (quá nhiệt, sensor no-data timeout) **dễ trip khi
máy vừa boot còn xa setpoint**; khi đã ổn định nhiệt chúng ngừng bắn → review đọc sạch. Khớp chính
xác pattern fresh-boot-fail / up-a-while-pass. (Đây là mối nguy GOTCHA #2 chưa được đóng.)

## Fix

**STEP 1 (chẩn đoán):** log `probe` ở nhánh else từng im lặng (`ForteSetting.cpp`) →
`[review] no stored run: probe=%u` (`0xFFFF`=virgin, `0`=erased/alloc-fail, garbage=torn read).

**STEP 2 (gốc rễ):** thêm **1 mutex `gEepromMutex`** + helper null-guarded `eepromLock()/eepromUnlock()`
(`define.h`), tạo cạnh `gI2CMutex` (`main.cpp`, **trước `xTaskCreate`**). Bọc quanh **mọi EEPROM
`begin..end` chạy runtime** để serialize hoàn toàn:
- `errorCheck.h`: `saveErrorToEEPROM` (attacker), `readErrorFromEEPROM`, `clearEEPROM`.
- `Bluetooth.cpp`: `getDataAmplificationEEPROM` (review), `saveSettingDevice`, `Wifi_Connect`.
- `sensor6035.cpp`: run-end RECORDPOS write.
- `ForteSetting.cpp`: JsonDataConfig write, review read/write, PEND_ID write.
- `displayLCD.cpp`: 2 calib writes. `PIDControl.cpp`: sensor-seq write.

Boot-only readers (`readEEPROM`/`loadParaFromEEPROM`/`loadSettingDevice`/`ForteSetting::begin`) chạy
trước khi task khởi động → không race → để nguyên (helper null-guard vẫn an toàn nếu gọi sớm).

**Không lồng:** đã kiểm mọi section — không section nào gọi section EEPROM khác khi đang giữ mutex
(run-end `end()`+unlock **trước** `saveErrorToEEPROM`; Wifi_Connect `end()`+unlock **trước**
`saveSettingDevice`; `readError`→`clear()` không đụng EEPROM). Plain mutex, `portMAX_DELAY` an toàn.

## Kiểm chứng

- `pio run -e esp32dev` → **SUCCESS** (RAM 22.9%, Flash 68.8%).
- Verify trên máy: fresh-boot, POST `/reviewlast` nhiều lần trong cửa sổ preheat (~5 phút) — với fix
  phải log `[review] reloaded` (hết double-free) thay vì im lặng/fail; probe ổn định ~150-260.

## Nạp

Chỉ đổi `src/` → `pio run -e esp32dev -t upload --upload-port COMxx`.
