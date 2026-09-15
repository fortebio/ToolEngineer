# 2026-07-17 — Test case: full quy trình amplification vs web

Máy **reboot/treo khi run xong** (sau ~40 phút). Yêu cầu: test case chạy full quy trình
amp đối chiếu với web.

## Con số của một run thật

`amplification_time = 120` vòng × `timePerLoop = 20s` (`src/define.h`) = **đúng 40 phút**.
Nên lỗi rơi vào **thời điểm kết thúc run**, không phải giữa chừng.

## Nghi phạm: `/curve` ở full scale va với TLS

Ở cuối run, `screen_Result()` (`displayLCD.cpp:1134`) chạy tuần tự:

```text
releaseBluetoothStack() -> getDataAmplificationEEPROM()  [Word tmp[10*130] = 2600B stack]
  -> dump 120x10 giá trị ra serial (delay(1) mỗi kênh ≈ 1.2s)
  -> WiFi retry (tới 5s) -> postData_GoogleSheet()  [TLS cần 32-40KB LIỀN MẠCH]
```

`postData_GoogleSheet` đã tối ưu kỹ (scope JsonDocument rồi hủy trước handshake, gọi
`dashboardSuspend()`). Nhưng có **khe hở ~1.2-6 giây** từ lúc `escreenFinished` được set
đến lúc `dashboardSuspend()` chạy — dashboard vẫn sống. Trong khe đó browser có thể gọi
`/curve`, mà **sau fix `lastRunLoops`** nó trả **cả run 120 vòng**:

- payload **5899 bytes** (đo trên máy; `--full` của mock ra 7741 vì giá trị giả khác)
- `JsonDocument` giữ 1200 float + `serializeJson` ra String + AsyncWebServer **copy** lại
- ⇒ đỉnh heap hàng chục KB **đúng lúc** TLS cần 32-40KB liền mạch ⇒ phân mảnh/OOM

Trước fix `lastRunLoops`, `/curve` ở `escreenFinished` trả `count:0` (~50 byte, vô hại).
**Tức là fix đó có thể đã tạo ra lỗi này.** Cần số liệu để khẳng định → viết test đo heap.

## Test 1 — `test/test_webcurve/` (trên máy thật)

```bash
pio test -e esp32dev_test -f test_webcurve -v
```

Self-contained như các test khác (`test_build_src = no`): tái tạo `sensor67Value[10][130]`
với **120 vòng** và chạy **đúng đường serialize của `handleCurve`**, rồi đo trên silicon:

- `test_curve_heap_cost_within_budget` — đo đỉnh heap của đường **streamed** (chunk 1460B,
  đường firmware dùng) và đường **materialised** (`JsonDocument→String→copy`). Assert
  streamed < 8KB, cộng assert đặc tả materialise **tệ hơn ≥4×** (lý do tồn tại của stream).
- `test_streamed_matches_document` — 2 đường phải ra **cùng giá trị** trên cả 1200 điểm
  (fix không đổi hợp đồng với client).
- `test_tls_block_still_available_after_curve` — **test yếu**, xem ghi chú cuối file.

Lưu ý: `pio test` **nạp firmware test đè lên máy** → phải nạp lại firmware thật sau đó
(`pio run -e esp32dev -t upload` **và** `-t uploadfs`).

## Test 2 — `tools/test_full_run.js` (không cần phần cứng)

```bash
python tools/sse_test_server.py --full     # terminal 1
node tools/test_full_run.js                # terminal 2
```

E2E qua Edge headless + DevTools Protocol (chỉ node stdlib, poll DOM nên không race SSE).
Đi hết quy trình và assert web ở từng chặng: heater → waitamp (**Start khoá**, đặt tên,
chart trống) → Confirm (mở Start, chart hiện, temps thu gọn) → **bấm Start** →
amplification **đủ 120 vòng** → finished (**chart ở lại**) → Result (đọc lại) →
**bấm White** (chart mới mất). **27/27 PASS.**

## Mock: 3 khiếm khuyết fidelity mà E2E lôi ra

1. **Mock tự vượt qua `waitamp`** — máy thật `ewaitampTube` **đứng chờ người dùng bấm
   Start**. Đã viết lại mock thành **state machine do web điều khiển**: `waitamp` giữ tới
   khi bấm đỏ, `finished` giữ tới khi bấm trắng (→ run mới). Giờ web thật sự lái máy.
2. **Mock bỏ sót round** khi vòng lặp SSE chậm hơn nhịp round → giả ra lỗ hổng máy thật
   không có. Sửa: stream **mọi** round đã xong, đúng thứ tự, mỗi round một lần; tách nhịp
   `home` (1s như thiết bị) khỏi nhịp readings.
3. **Mock nhảy sang `finished` tức thì** ở round cuối → mất điểm cuối. Máy thật giữ
   `eoptoreading` một lúc (COUNTER đã = 120) trong khi sensor ghi EEPROM, `dashboardLoop`
   (10ms) kịp gửi round 119. Đã mô hình cửa sổ đó.

`--full` giữ đúng khối lượng dữ liệu + trục X thật (120 vòng × 20s → 39.67 phút,
`/curve` 7.7KB) nhưng nén đồng hồ còn ~20s.

## KẾT QUẢ: giả thuyết đúng, đã đo trên máy thật

`pio test -e esp32dev_test -f test_webcurve -v`:

```text
[curve] full run: 120 rounds x 10 channels -> 5899 byte payload
[curve] peak heap cost: streamed (handleCurve) = 1488 B,
        materialised (JsonDocument+String+copy) = 29792 B  -> 20x worse
```

Và heap thật của firmware (Serial lúc dashboard chạy):

```text
[dash] dashboard on http://192.168.4.1/ (AP) | free=90508 maxAlloc=55284
```

**Phép tính chốt hạ:**

| | Bytes |
| --- | --- |
| Block liền mạch lớn nhất (thật, dashboard đang chạy, `clients=0`) | **55 284** |
| `/curve` cũ ngốn ở run đầy | **− 29 792** |
| Còn lại cho TLS | **25 492** |
| TLS cần | **32 768 – 40 960** ❌ |

⇒ **Đúng một request `/curve` ở cuối run là đủ đẩy block liền mạch xuống dưới ngưỡng TLS
→ OOM → reboot.** Có browser nối vào thì heap còn thấp hơn nữa.

## FIX: `handleCurve` chuyển sang chunked response

`webDashboard.cpp`: thêm `struct CurveWriter` — sinh JSON **từng token** qua buffer nhỏ
(`pend`), và `handleCurve` dùng `req->beginChunkedResponse(...)` với lambda giữ
`shared_ptr<CurveWriter>` (state sống qua các lần callback). Không bao giờ tồn tại cả
payload trong RAM.

- Đỉnh heap: **29 792 B → 1 488 B** (giảm **20×**) ⇒ còn 53 796 B liền mạch cho TLS.
- Hợp đồng **không đổi**: `test_streamed_matches_document` assert 2 đường ra **cùng giá
  trị** trên cả 1200 điểm.
- Build: SUCCESS (RAM 22.8%, Flash 68.3%). Test: **3/3 PASS**.

Test giờ **gác đúng thứ cần gác**: assert đường **streamed** (đường firmware thật dùng)
phải rẻ, cộng một assert đặc tả rằng materialise **tệ hơn ≥4×** — chính là lý do tồn tại
của việc stream. (Bản đầu assert đường JsonDocument nên fail vĩnh viễn dù firmware đã
sửa — test đỏ mãi là test bị bỏ qua.)

Lưu ý `test_tls_block_still_available_after_curve` là **test yếu**, đã ghi rõ trong code:
môi trường test không link `src/` nên không có WiFi/AsyncWebServer/BT → `free=227088`,
nhiều gấp bội firmware thật. Nó pass không chứng minh gì. Con số có ý nghĩa là **chi phí
heap**, vốn độc lập môi trường.
