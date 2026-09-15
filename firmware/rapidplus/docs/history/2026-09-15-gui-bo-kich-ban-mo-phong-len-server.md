# Gửi bộ kịch bản mô phỏng lên server — lệnh Serial `uploadResult` + `run_sim_cases.py --upload`

Ngày 2026-09-15, nhánh `v2.4.5at`, tiếp nối
[2026-09-15-kich-ban-mo-phong-danh-gia-ket-qua.md](2026-09-15-kich-ban-mo-phong-danh-gia-ket-qua.md).

## Vì sao

Sau khi bộ 11 kịch bản × 10 giếng đo xong trên RPL01015 (106 PASS / 0 FAIL), Kane hỏi: *"có thể gửi
lại toàn bộ dữ liệu testcase lên server được không"*. Ý là để cùng một bộ dữ liệu có mặt trên
Google Sheet / ingest / ERP — nơi kết quả thật được xem và duyệt — chứ không chỉ nằm trong
`docs/reports/`. Việc này còn kiểm được một thứ mà tài liệu 11/09 ghi là *"chưa chạy máy thật"*:
**payload v2.4.5AT với 7 trường hình dạng mới** (`shape_flag`, `rise_width`, `window_rate`,
`arm_width`, `suspect_score`, `climbs_fixed`, `climb_first_i`) và chữ **`F`** trong chuỗi `result` có
được cả ba đích chấp nhận không.

Hai quyết định Kane chốt trước khi làm: **giữ nguyên `type_Upload = "Manual"`** (không thêm giá trị
mới cho server phải chấp nhận, không đổi device ID tạm) và **gửi cả 11 kịch bản**.

## Trước

Runner cố ý không upload: `getResult` → `resultOutput()` → `escreenReview` → `screen_Result('r')`,
và trong `screen_Result` chỉ nhánh `'f'` mới gọi `postData_GoogleSheet`. Đường `'f'` tới được bằng
hai cách, cả hai đều **không** có từ UART:

- cuối run thật (`escreenFinished`, `type_Upload "Auto"`, WHITE sau đó **reboot**);
- màn "Up Data": giữ ĐỎ 3 s → `eSettingMenu` → ĐỎ → `eUpLoadData` → `screen_Result('f')`
  (`type_Upload "Manual"`, WHITE **về màn chính, không reboot**). Web `/control?btn=red` chỉ bắn
  short-press nên cũng không vào được menu này.

Tức là dữ liệu nạp bằng `{"Slot":[…]}n#` chấm được trên máy nhưng không có cách nào ra khỏi máy.

## Nay

### Firmware: lệnh Serial `uploadResult` (`src/ForteSetting.cpp`)

Giống `getResult` về hình thức, nhưng đặt `type_infor = eUpLoadData` — đúng cái mà ĐỎ trong menu
Setting làm. `screen_Result('f')` tự đọc lại record ở `RECORDPOS`
(`getDataAmplificationEEPROM()`), phân tích lại trên đường upload (`bResultPutToGoogleSheet`) và
gửi đủ 3 đích qua `postJsonToAllTargets`. Chọn `eUpLoadData` chứ không `escreenFinished` vì
payload mang **"Manual"** (đúng bản chất: gửi lại record đã lưu) và vì WHITE ở đó **không reboot**
→ lặp được 11 lần không cần chờ boot.

Hai chốt từ chối, có in lý do `[up] uploadResult refused: …` để runner báo:

- **`dashboardDeviceBusy()`** — cùng cổng với đường áp settings từ web. Upload đỗ DisplayTask trong
  mbedTLS tới ~90 s và tạm dừng dashboard, không được đè lên một run đang chạy. `eUpLoadData` tự
  nó nằm trong nhóm *busy*, nên lệnh thứ hai gửi tới khi lệnh đầu chưa xong cũng bị từ chối thay vì
  xếp thêm một lượt `screen_Result('f')` nữa (DisplayTask chỉ vào `switch` khi `changeScreen`
  bật, nên hai lượt gán liên tiếp là hai lần upload).
- **`dashboardIsAP() || WiFi.status() != WL_CONNECTED`** — `screen_Result('f')` không có STA thì
  **im lặng** rơi sang `bResultGet` và chỉ vẽ lưới; từ chối rõ ràng tốt hơn một lượt "đã gửi" mà
  không có `[up]` nào.

Thứ tự ghi: **state trước, `changeScreen` sau**. DisplayTask ở core khác đọc `changeScreen`;
thấy cờ trước khi state đổi thì nó vẽ lại màn **cũ** rồi xoá cờ, và màn mới không bao giờ lên.
(`resultOutput()` viết ngược thứ tự này từ đầu; chưa thấy cắn vì `escreenReview` vẽ lại cũng ra
đúng thứ đó — nhưng với `eUpLoadData` thì vẽ lại màn cũ nghĩa là **không upload**.)

### Runner: `python tools/run_sim_cases.py COM7 --upload`

Mỗi kịch bản vẫn nạp → `getResult` → chấm như cũ, **rồi** `uploadResult`. Runner chờ dòng
`ERP server feedback:` (dòng cuối cùng của `postJsonToAllTargets`) hoặc dòng từ chối; trần 300 s vì
mỗi đích có tới 4 lần thử × 60 s timeout đọc. Từ các dấu mốc `[up] <đích> POST OK/FAIL … code=…
(try k)` nó ghi **mã HTTP, thời gian, số lần thử của từng đích**, cỡ payload, và **thân phản hồi
của ingest và ERP** (mang `id`/`file` bên ingest, `result_id` bên ERP — đó là cách tìm lại từng
run mô phỏng trên server). Bị từ chối thì dừng cả catalogue: chạy tiếp mà không upload là vô nghĩa
với cờ này.

Kèm một phép đối chiếu miễn phí: đường upload in `Outcome check:` cho từng slot (không in JSON),
runner so **chữ của đường upload với chữ của `getResult`** — hai đường dùng chung
`analyseSlotCurve()` nhưng gọi từ hai chỗ khác nhau, lệch là có chuyện. Report ghi
`chữ đường upload khác getResult ở slot …` nếu xảy ra.

Record run thật được nạp trả lại như trước và **không** upload.

### Sửa kèm: `--regrade` tìm sai section khi log có cả backup lẫn restore

Chạy thử `--regrade` trên chính log 12:01 cho **110/110 STALE**. `offset` (section chứa injection
của kịch bản 0) được tính bằng *tổng số getResult − số kịch bản* = 2 khi log có backup read ở đầu
**và** restore read ở cuối, trong khi injection của kịch bản 0 nằm ngay sau backup read (section
1). Mọi giếng so với echo của kịch bản **kế tiếp** → STALE hết, và report chỉ nói "re-run on the
unit" nên không ai thấy. Nay nhận diện backup read trực tiếp (getResult đầu tiên **không** có
injection nào trước nó) và mỗi section getResult được cắt ở lệnh kế tiếp (`>>> `) để output của
`uploadResult` — cũng in `Slot N:` / `Outcome check` — không lẫn vào. Log 12:01 chấm lại đúng
106 / 0 / 1 / 3; log 11:23 ra 90 PASS + 16 STALE, đúng 16 giếng đã đổi recipe sau đó.

### Guard (`tools/test_sim_cases.py`, mục 6–7)

Lệnh còn trong firmware, còn được dispatch, còn từ chối khi busy / không STA, còn đặt
`eUpLoadData` (không phải `escreenFinished`), ghi state trước cờ; `Bluetooth.cpp` còn in đúng 4
chuỗi dấu mốc runner bám vào; `parse_upload` đọc đúng OK-sau-retry, FAIL-không-retry, từ chối,
chữ đường upload, phản hồi server. Và **chấm lại log bằng chứng 12:01 phải ≥ 90 PASS, 0 FAIL** —
toàn STALE là offset sai.

## Bằng chứng — RPL01015, 15/09/2026 16:11

Firmware nạp bằng dây sau khi build sạch 0 warning (`pio run -e esp32dev -t upload`, 2 443 984 B).
Report: [`docs/reports/simcases/2026-09-15-1611-RPL01015.md`](../reports/simcases/2026-09-15-1611-RPL01015.md)
· log serial cùng tên `.serial.log`.

**11/11 kịch bản tới đủ 3 đích, mọi đích trả 200 ở lần thử đầu; chấm trên máy vẫn 106 PASS · 0 FAIL ·
1 KNOWN-WEAK · 3 INFO** — cùng số với lần 12:01, tức firmware mới không đổi gì ở đường `getResult`.

| | GAS | ingest | ERP | payload | cả lượt |
| --- | --- | --- | --- | --- | --- |
| mã HTTP | 200 ×11 | 200 ×11 | 200 ×11 | 7 843 – 7 942 B | |
| thời gian | 3,6 – 7,3 s (lần đầu 7,3, sau đó ~4) | 2,2 – 2,6 s | 2,2 – 2,9 s | | 13 – 17 s |
| server trả về | — | `{"ok":true,"file":"RPL01015_20260915_<hash>.json","db":true,"id":21760…21770}` | `{"status":"ok","result_id":"<uuid>","device_matched":true}` | | |

- **Payload v2.4.5AT được cả ba đích nhận**: 7 trường hình dạng mới và chữ `F` trong `result`
  (dạng `N/A | <Ct> | F`: S04 slot 3–4 lý do 1, S05 slot 3 và 8 lý do 2, Ct 1,67 / 2,67) cùng chữ
  `E` (S05 slot 4, 7), 11 lần liên tiếp, không một retry. Ingest cấp id **liên tiếp 21760 → 21770**
  (11 run, không mất không trùng); ERP `device_matched: true` ở cả 11. (Log serial không in thân
  payload — chuỗi `result` suy từ format `"%s | %04.01f | %c"` và chữ đường upload đã in.)
- **Chữ của đường upload = chữ của `getResult` ở 110/110 giếng** (so `Outcome check` của
  `bResultPutToGoogleSheet` với JSON của `bResultGet`): P·S·F·E·B·N đều khớp, kể cả hai giếng `F`
  lý do 2 và hai giếng `E` của S05.
- Heap trước mỗi handshake (`intLargest` ở `POST begin`): GAS **69 620** ×11; ingest và ERP
  **63 476 / 61 428 / 49 140 / 47 092** (3 / 2 / 1 / 5 lượt mỗi đích) — tức **10 handshake thành
  công ở 47 092 B**, dưới mốc 49 140 mà `test_endrun_upload` ghi là "chết" (`-0x0010 BIGNUM`,
  27/07). Mốc đó là của một cấu hình heap khác (trước khi nhả BT sớm); con số hôm nay nói ngưỡng
  thật nằm dưới 47 KB với payload 7,9 KB. Không lượt nào chạm cổng chờ `TLS_MIN` 33 KB.
- Cả lượt 11 kịch bản kể cả backup/restore: **8 phút** (16:11 → 16:19). Máy đang ở màn review sau
  restore; bấm TRẮNG để rời (reboot).

Tìm lại trên server: ingest id 21760–21770 / file `RPL01015_20260915_*.json`; ERP 11 `result_id`
ghi trong report; Sheet: 11 dòng `RPL01015`, `type_Upload Manual`, 16:11–16:18 ngày 15/09.


## Điều phát hiện khi đọc đường upload

1. **"Up Data" trên máy gửi tên bệnh `N/A` dù run vừa xong có tên.** `dashboardLoop()` xoá nhãn
   slot (RAM + NVS) ở sườn lên của `isBusy()`; `eUpLoadData` nằm trong nhóm busy, nên vào màn Up
   Data từ menu là một "chu kỳ run mới" theo điều kiện đó — nhãn bị xoá trong ~10 ms, còn
   `postData_GoogleSheet` mới dựng payload sau `getDataAmplificationEEPROM()` + vòng in CSV
   90 × 10 × `delay(1)` (~1 s). Kết quả: bản gửi tay của cùng một run mang `N/A | Ct | P` trong
   khi bản tự động mang tên bệnh. Có từ 2026-08-20 (reset nhãn theo run), không phải do lệnh mới —
   lệnh mới chỉ đi đúng đường đó. **Chưa sửa**: cần quyết định `eUpLoadData` có phải "run mới"
   không (rõ ràng không), tức đưa nó ra khỏi sườn xoá nhãn mà vẫn giữ nó *busy* cho cổng settings.
2. `postJsonRetry` thử **4** lần (`ATTEMPTS = 4`), không phải 3 như comment cũ trong `tools/`.
3. **Mỗi thân phản hồi server kết thúc bằng một byte `0xFF`** (log serial hiện `�` sau `}` của
   ingest lẫn ERP, 22/22 lần). `readBodyDeadlined()` làm `while (s->available()) body +=
   (char)s->read();` — với `WiFiClientSecure`, `available()` báo 1 ở close_notify/EOF mà `read()`
   trả **-1**, `(char)-1` = `0xFF` được nối vào. Vô hại hôm nay (firmware chỉ `indexOf` trên thân
   đó), nhưng ai parse thân phản hồi bằng JSON sẽ vấp. Sửa = kiểm `read() < 0` rồi `break`. Chưa
   làm; runner cắt ký tự đó khi ghi report.

## Đã loại

- **`type_Upload = "Simulation <mã>"`** — lọc được trên server nhưng cần server chấp nhận giá trị
  ngoài `Manual/Auto/N/A`; không biết ba đích validate thế nào. Kane chọn giữ "Manual".
- **Đổi device ID tạm (`SIM01015`)** — sạch nhất cho dữ liệu RPL01015 nhưng thêm hai lần reboot và
  hai lần ghi `parameter` (400 B EEPROM) chỉ để gắn nhãn. Kane không chọn.
- **Gắn tên bệnh = mã kịch bản qua `/rename`** để chuỗi `result` tự mang `S06 | 04.7 | P` — không
  làm được vì chính điều 1 ở trên: nhãn bị xoá đúng lúc vào `eUpLoadData`.
- **Gửi thẳng từ PC** (đọc payload từ log rồi POST bằng Python) — nhanh hơn, nhưng thứ cần chứng
  minh là *máy* dựng và gửi được payload mới, không phải server nhận được JSON.

## Còn nợ

- Sửa điều 1: `eUpLoadData` không được kích sườn xoá nhãn.
- Sửa điều 3: `readBodyDeadlined()` dừng khi `read()` trả âm.
- `resultOutput()` (`getResult`) cũng nên ghi state trước cờ như `uploadResult()`.
- Các nợ cũ trong tài liệu 15/09 (dời `climbs_fixed` lên trước `toJSON()`, backup raw gốc qua
  `GET /curve`, biến thể `a`) chưa động.
