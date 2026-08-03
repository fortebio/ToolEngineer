# 2026-07-30 — Reset cuối run: `printf("%s", i + 1)` + bật `-Wformat`

## Triệu chứng

```
[up] result computed
[len] set(120) was=0 APPLIED
Guru Meditation Error: Core  0 panic'ed (LoadProhibited). Exception was unhandled.
```

Máy reset ở **cuối run 40 phút**, ngay khâu upload.

## Nguyên nhân

`src/Bluetooth.cpp`, dòng debug mới thêm trong `postData_GoogleSheet()`:

```cpp
Serial.printf("[up] Sick Name %s: CT=%04.01f result=%c\n", i + 1, CT_value[i], result[i]);
//                             ^^                          ^^^^^
```

`%s` nhận `i + 1` — một **`int`**. `printf` coi số đó là **`char*`** rồi deref địa chỉ
`0x00000001` → **LoadProhibited**. Crash ở **vòng lặp đầu tiên** (`i = 0`), nên **không có dòng
`[up] Sick Name` nào kịp in** — khớp chính xác với log.

### Đã loại trừ trên đường điều tra

- **Heap**: `intLargest = 86 004 B` — cao hơn mốc chạy được đã đo (63 476) và xa mốc chết
  (49 140). Không liên quan.
- **Thứ tự boot đổi hôm nay** (chuyển `_displayCLD.begin()` + `_ForteSetting.begin()` lên trước
  khối WiFi, GOTCHA 1): nghi ngờ ban đầu là nó phá bố cục heap — **sai**, số đo ở trên minh oan.
- **`slotNames[i]` tràn**: `OPTOCHANNELS` = 10, mảng `String[10]`. Không tràn.
- **`outcome[]`/`peak_features[]` chưa khởi tạo**: constructor có gọi `clear()`. Không phải.
- **Stack**: Display dùng 1568/10240 B. Rộng.

## Vì sao nó lọt: `-Wformat` đang TẮT

`Print::printf` **có sẵn** `__attribute__((format(printf, 2, 3)))`
(`cores/esp32/Print.h:75`) — compiler **hoàn toàn có thể** bắt được. Nhưng build này không bật
`-Wformat`.

**Đã kiểm bằng thực nghiệm**: build lại với dòng lỗi nguyên vẹn → **không một cảnh báo nào**.
Bug đi thẳng vào firmware.

Đây là điểm đáng giá hơn cả bản vá: một *lớp* lỗi có thể biến số nguyên thành con trỏ được deref
— trên thiết bị y tế — đang được ship im lặng.

## Sửa

### 1. Dòng gây crash

```cpp
Serial.printf("[up] slot %d: name=%s CT=%04.01f result=%c\n",
              i + 1, slotNames[i].c_str(), CT_value[i], result[i]);
```

### 2. Bật `-Wformat` (`platformio.ini`)

Scope là **`-Wformat`, không phải `-Wall`**: nó nhắm đúng lớp lỗi nguy hiểm mà không chôn tín hiệu
dưới hàng loạt cảnh báo style từ header bên thứ ba. Bật lên lộ ngay **14 lỗi format có sẵn**:

| Chỗ | Lỗi | Mức |
|---|---|---|
| `sensor6035.cpp:870,886` | `%s` nhận **`const String`** (không `.c_str()`) | **Cùng lớp với crash** — truyền object non-POD qua varargs là UB |
| `PIDControl.cpp:1205,1315` | `%d` nhận **`double`** (`CURRENT_TEMP_PID`) | **Nặng** — in rác trong **thông báo quá nhiệt** |
| `displayLCD.cpp:923,1124` | `%d` nhận `unsigned long` | Nhẹ (32-bit trùng cỡ) |
| `displayLCD.cpp:1336,1359,1364` | `printf` không có conversion mà vẫn truyền `CT_value[i]` | Vô hại, nhưng lộ ý đồ đã rơi |
| `Bluetooth.cpp:604` | thừa tham số `result[i]` | Vô hại |
| `wifiStore.cpp:68,70` | `-Wformat-truncation` trên `char key[8]` | Lý thuyết (`snprintf` không tràn) |

**Đã sửa hết** để build sạch. Lý do không để lại cái nào: **cảnh báo lúc nào cũng hiện là cảnh báo
bị bỏ qua** — đúng cơ chế đã để bug này lọt.

### 3. `sprintf` → `snprintf` (cùng vòng lặp)

`char resultConfig[15]` với `"%04.01f | %c"`: một `CT_value` lớn hoặc rác (slot không có đường
cong dùng được) in ra **quá 15 byte**, đè thẳng phần còn lại của stack frame. `snprintf` cắt thay
vì phá.

## Kiểm

`tools/test_phase0_guards.py` thêm assertion #4: **`-Wformat` phải còn trong `build_flags`**.

Guard ở tầng *cấu hình build*, không phải quét source — vì bộ dò thật sự là compiler; cái cần bảo
vệ là đừng ai tắt nó. Bỏ cờ đi thì mất im lặng, không có triệu chứng nào cho tới lần crash sau.

Negative test: gỡ `-Wformat` → guard đỏ; gắn lại → xanh.

Build `pio run -e esp32dev` SUCCESS, **2 356 229 B (70.5%)**, **0 cảnh báo format**. 7/7 guard xanh.

## Sửa kèm: `paraList[index]` không chặn biên (`sensor6035.cpp`)

Soát tiếp `ALS_IT_Process()` thì **cảnh báo ban đầu về `strParaList[readValue - 1]` là SAI** — chỗ
đó có guard `readValue < 1 || readValue > 6` + `return` ngay phía trên, index luôn 0..5.

Nhưng **dòng ngay bên cạnh thì hở thật**:

```cpp
int index = strPara.toInt();          // text tuỳ ý từ Serial/BT
VEML6035_SET_ALS_IT(paraList[index]); // paraList là const Word[6] - KHÔNG chặn biên
```

`ALS_IT 99` → đọc ~200 byte ngoài mảng trên stack rồi **ghi giá trị nhặt được vào thanh ghi
ALS_IT của VEML6035** — tức âm thầm cấu hình sai chính cảm biến đo khuếch đại. `ALS_IT -5` đọc
ngược về trước mảng. Và `toInt()` trả `0` cho chuỗi không phải số, nên gõ nhầm sẽ **lặng lẽ chọn
25 ms** thay vì báo lỗi.

Đường vào là Serial/BT — đúng đường CLAUDE.md Setting #4 ghi là "vào thẳng, không qua validate
nào". Đã thêm guard `0..5` cùng kiểu với guard read-back ngay dưới nó.

Đối chiếu: hai chỗ còn lại dùng `toInt()` làm chỉ số (`webDashboard.cpp:1423` `?n=`,
`:1507` `?slot=`) **đều đã có** guard `0..9` và trả 400. Đây là chỗ duy nhất sót.

## Còn tồn (chưa sửa — cần quyết định sản phẩm, không phải dọn dẹp)

- **`bool flag` ở `Bluetooth.cpp:507` không bao giờ được dùng.** Nó là giá trị trả về của
  `bResultPutToGoogleSheet()` — chỉ báo thuật toán phát hiện **thành công hay thất bại** — và đang
  bị vứt: máy vẫn upload kết quả lên Google Sheet/ERP kể cả khi thuật toán báo hỏng. Xử lý thế nào
  khi nó `false` (bỏ upload? gắn cờ vào bản ghi? báo màn hình?) là quyết định sản phẩm.
