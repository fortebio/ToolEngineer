# Đồng bộ nội dung thông báo trên web với quy trình thật trên máy (2026-08-05)

## Vấn đề

`fillStatus()` (`src/webDashboard.cpp`) xử lý **12 trên 38** giá trị `e_statuslcd`. 26 state còn lại
rơi vào `default` → web báo **"Idle / Waiting for a run to start."** trong khi máy đang làm việc.

Ca tệ nhất là **`ewaitphase2`**: lysis vừa xong, máy đang chờ người **rút ống đang nóng** ra.

| | Nội dung |
| --- | --- |
| TFT | "Take the lysis tube / then close the lid" · "Press Green to preheat 67" |
| Web — banner | **"Idle / Waiting for a run to start."** |
| Web — chip xanh | "Amplification" |

Cùng một màn hình vừa nói "đang rảnh" vừa mời bấm một nút có nhãn. Đây **đúng là lỗi mà comment ở
`ewaitLysisTube` được viết ra để sửa** (2026-07-xx, "red lights up but lysis never starts") — bản
sửa đó không bao giờ được áp cho state anh em.

`fillActions` và `fillStatus` là **hai bảng tra trên cùng một enum**. Chúng lệch nhau: 6 state có
nhãn nút nhưng không có tên trạng thái.

## Đã làm

**34/38 state có case riêng.** 4 state còn lại nằm trong allowlist có ghi lý do:

| State | Vì sao để `default` |
| --- | --- |
| `escreenStart` | màn idle thật — "Idle" là sự thật |
| `ewaitingReadsensor` · `eheathotlid1` · `eprepare` | **không nơi nào gán** (`grep "type_infor = <tên>" src/` = 0). `eheathotlid1` còn không có case trong `switch` của `displayLCD` |

Ban đầu tôi có viết case cho `eheathotlid1` với nhiệt độ nắp — soát chéo bắt được: **mô tả một màn
hình không tồn tại**. Đã bỏ.

### Nội dung thêm vào

- **Nhiệt độ hiện tại / mục tiêu** ở mọi pha sấy. `"Warming to 80 C"` đứng yên suốt ~10 phút đọc
  như màn hình treo; nay là `"24.3 / 82.0 C"`. Số 80 trong chuỗi cũ cũng sai — mục tiêu thật là
  `parameter.lysisTemp` = **82.0**.
- **Đếm ngược làm tròn LÊN phút**, khớp đúng cách `waitLysis10min` in trên TFT (`timeleft/60 + 1`).
  Trước đó web nói "~9.4 min" trong khi máy nói "10 minute" — cùng một khoảng chờ, hai con số.
- **Số vòng** ở pha đo: `"~12 min left - round 47/120"`. Đồng hồ và bộ đếm vòng là **hai bộ đếm độc
  lập** — run kết thúc khi đủ vòng, nên đồng hồ có thể về 0 mà vòng chưa hết. Hết giờ thì nói
  `"Finishing the last rounds"`, **không bịa ra phút**.
- **`escreenFinished` tách hai giai đoạn** theo `gResultsReady`. Một enum nhưng là hai màn hình rất
  khác nhau: `screen_Result()` tính kết quả rồi **chặn 30-90 s trong mbedTLS** để upload. Báo
  "Results ready." suốt cửa sổ đó mời người ta reload vào một bảng trống.
- **`epreheat67` tách khỏi `eheating67`**: ở `eheating67` nhiệt còn đang leo, ở `epreheat67` đã tới
  nơi và cái đang chờ là giữ nhiệt + quang học. **Hai cổng riêng biệt nên không hứa một đếm ngược
  duy nhất** — hết giờ mà màn hình chưa chuyển thì câu trả lời trung thực là "còn thứ khác chưa
  xong", không phải một số 0 đứng im.
  `timeStartWait == 0` là **giá trị thật**, không phải "chưa đặt" (đường xanh sau lysis ghi 0 để coi
  như đã giữ đủ), nên có guard riêng.

## Hai luật về `phase`, học được từ chính bug này

1. **KHÔNG trả `"idle"` cho state mà nút ĐỎ mang nghĩa khác.** `script.js` viết lại cú bấm đỏ thành
   cổng đặt tên **khi và chỉ khi** `phase === "idle"`. `escreenStart` là **ca cơ sở** chứ không phải
   ngoại lệ — ở đó đỏ đúng nghĩa đó.
2. **`"finished"` không phải nhãn miễn phí** — `chartMode` của client chứa nó, phát ra là kéo chart
   lên Home. State chỉ *đi sau* một run thì cho phase riêng: `review`, `error`, `restart`.

Phase lạ thì an toàn: client coi mọi tên nó không biết là màn hình thường.

**Kéo theo**: CLAUDE.md từng ghi "chart mất khi `escreenRestart` (không có trong `fillStatus` →
`default` → `phase idle`)". Nay `escreenRestart` = `phase "restart"`, **vẫn không nằm trong
`chartMode`** nên hành vi giữ nguyên — nhưng câu văn đã sai và đã sửa.

## Guard

`python tools/test_status_coverage.py` — ba bất biến:

1. **Phủ**: mọi state có case, trừ allowlist có ghi lý do. Thêm state mới vào `displayCLD.h` là
   **buộc phải quyết định** web nói gì, không im lặng thừa hưởng "Idle".
2. **Hai bảng đồng thuận**: `fillActions` biết nút làm gì thì `fillStatus` phải biết đó là trạng
   thái gì. Chính xác cái đã lệch.
3. **Không state mang-nghĩa-đỏ nào báo `"idle"`** — cắn thật, đã ship một lần.

Guard **tự bắt hai lỗi của chính nó** ở lần chạy đầu, cả hai đều quanh `escreenStart`: nó vừa có
case (nhánh fall-through) vừa nằm trong allowlist, và nó *là* ca cơ sở của luật đỏ-ở-idle. Đã sửa
bằng cách so theo "state nào **tự đặt** phase" thay vì "state nào có nhãn case", và miễn trừ
`escreenStart` có ghi lý do.

Negative test 3/3 đỏ: mất case `eShowQR` · state có nhãn đỏ báo `"idle"` · allowlist liệt kê state
không tồn tại.

*(Seed đầu tiên của tôi **không** làm guard đỏ — nó chèn `phase = "idle"` **trước** dòng gán thật
trong cùng case, mà bộ phân tích lấy "lần gán cuối thắng". Seed sai, không phải guard hỏng. Và
anchor phải viết `\r\n`: `webDashboard.cpp` là CRLF.)*

## File đã sửa

| File | Việc |
| --- | --- |
| `src/webDashboard.cpp` | viết lại `fillStatus` (12 → 34 case), nhận thêm `bt`/`tp` thay vì đọc lại nhiệt độ; hoist `gResultsReady` lên trên nó |
| `tools/test_status_coverage.py` | guard mới |
| `CLAUDE.md` · `docs/GUI_SSE/GUI.md` | hai luật `phase`, sửa câu về `escreenRestart` |

## Kiểm chứng

- Build SUCCESS, flash 71.9% → **72.1%** (+5.4 KB chuỗi), 0 cảnh báo
- `test_status_coverage` + 5 guard cũ ✓, negative 3/3 ✓

**Còn phải xem trên máy thật**: đi hết một run và đối chiếu từng màn TFT với banner trên điện thoại
— nhất là `ewaitphase2` (phải hiện "Remove lysis tube"), `epreheat67` (đếm ngược rồi chuyển sang câu
chờ), và `escreenFinished` (phải nói "Finishing the run" trước, "Run complete" sau).
