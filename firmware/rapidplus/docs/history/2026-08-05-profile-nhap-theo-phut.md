# Profile Configuration nhập theo phút + đồng bộ thời gian preheat (2026-08-05)

## Yêu cầu

- **Lysis**: `Lysis duration` (giây) → **Lysis time**, nhập theo **phút**
- **Amplification**: gộp `Amplification rounds` + `Time per round` (cố định 20000) thành **một ô
  Amplification time**, nhập theo **phút**
- **Opto preheat**: đồng bộ lại thời gian preheat của dự án, và cho nhập theo **phút**

## Đơn vị đổi ở FORM, không đổi ở máy

`parastructure` là **400 B trên trần 402 B** (CLAUDE.md Setting #5) → **không có chỗ** cho một bản
sao ở đơn vị khác. Và validate của firmware (`handleConfigPost`) viết theo **đơn vị lưu**:

```cpp
else if (k == "lysis duration")      // 0..65535 s
else if (k == "opto preheat time")   // 0..3600 s
else if (k == "amplification time")  // 1..130 rounds  <- COUNTER indexes sensor67Value[10][130]
```

Nên chuyển đổi phải nằm ở **rìa form**, không được đụng trust boundary. Thêm hai móc tuỳ chọn cho
mỗi field số — tổng cộng **2 dòng** plumbing:

| Nơi | Sửa |
| --- | --- |
| `renderFields` | `numInput(f, f.toUi && cur !== undefined ? f.toUi(cur) : cur)` |
| `collectFields` | `setPath(body, f.p, f.toDev ? f.toDev(num) : num)` |

| Ô trên web | Key gửi đi | Máy lưu |
| --- | --- | --- |
| Lysis time (min) | `lysis duration` | giây |
| Amplification time (min) | `amplification time` | **vòng** |
| Opto preheat (min) | `opto preheat time` | giây |

## Gộp hai ô: `time per loop` biến mất khỏi form nhưng KHÔNG bị ghi

`collectFields` chỉ gửi **key của card đang mở**, nên bỏ field ra khỏi `fields[]` là đủ để nó không
bao giờ bị ghi — máy giữ nguyên giá trị của mình.

Và chính giá trị đó được dùng để quy đổi:

```js
function perLoopMs() {
  var v = Number(getPath(cfgCache, "time per loop"));
  return v > 0 ? v : 20000;
}
```

**Đọc từ máy chứ không hardcode 20000.** Yêu cầu nói "cố định 20000" nhưng đó là giá trị *mặc định*
của một tham số có thật; máy nào đặt khác sẽ đọc ra số phút sai nếu ta ghim hằng số. Đọc từ config
tốn đúng bằng ấy code và không bao giờ nói dối. `v > 0` cũng chặn luôn chia cho 0.

**Clamp 130 không phải sở thích**: `COUNTER` index vào `sensor67Value[10][130]`, quá là tràn buffer
**giữa run**. Firmware cũng từ chối — hai cổng chứ không phải một; form không được post một giá trị
nó biết chắc sẽ bị từ chối, vì như thế là báo "đã lưu" mà chẳng đổi gì. Trần: 130 × 20 s = **43 phút**.

## Preheat: hai timer, một con số "15"

Đây là thứ cần "đồng bộ". Trong dự án có **hai** khoảng thời gian và cả hai đều dính số 15:

| | Giá trị | Là gì |
| --- | --- | --- |
| `PIDControl::hotlidWaitMs` | `15 * 60000` = **15 phút** | giữ nắp trước khi vào amplification |
| `parameter.optopreheatduration` | `15 * 20` = 300 **giây** = **5 phút** | preheat LED + opto |

*(cột giá trị là trạng thái **trước** thay đổi hôm nay — xem "Chốt" bên dưới)*

`PREHEATLOOPS = optopreheatduration * 1000 / OPTO_INTERVAL` = `300000 / 20000` = **15 vòng** × 20 s.

Tức là "15" ở một bên là **phút**, bên kia là **vòng**. Cách viết `15 * 20` đọc lên như "15 phút",
và `sensor6035.h` nói thẳng ra thế:

```cpp
eSensorpreheat,    // preheat for 15 mins during heater preheating to amp temperature
```

**Sai.** Máy preheat opto **5 phút** — comment nói 15, máy chạy 5.

### Chốt: nâng lên 15 phút thật

Bước đầu tôi chỉ viết lại `15 * 20` → `5 * 60` (cùng ra 300) và sửa comment, vì đổi giá trị là đổi
**hành vi máy** chứ không phải dọn tài liệu. Hỏi lại thì ý định là **đồng bộ với 15 phút preheat
Amp** → `optopreheatduration = 15 * 60` = **900 giây**, `PREHEATLOOPS` = 45 vòng.

Hai đồng hồ nay **cố ý bằng nhau**, và `button.cpp:437-442` khởi động cả hai trên **hai dòng liền
nhau** — nên quang học ấm đúng bằng khoảng thời gian nhiệt ổn định, thay vì "chắc là đủ ấm" ở 1/3
quãng đường.

Giá trị này còn nằm ở **hai file provisioning** — `JsonPara/pass_file_initial_Full.json` và
`pass_file_processTest.json` — đều đã lên 900. **Bỏ sót là nạp lại máy sẽ âm thầm quay về 5 phút.**

### Hệ quả: đường calib → amplification

`button.cpp:463-468` cố ý hạ `hotlidWaitMs` xuống **5 phút** cho luồng sau calib. `optopreheatduration`
là **tham số lưu**, không có override runtime tương ứng → đường đó nay **chờ 15 phút thay vì 5**.

Chậm, không hỏng. Muốn giữ 5 phút thì phải thêm một override runtime cho `PREHEATLOOPS` đúng kiểu
`hotlidWaitMs` — tôi **không tự thêm**, vì đó là state mới cho một luồng hiện đang ẩn khỏi web
(card Calib comment từ 2026-08-02) và chưa ai yêu cầu.

## Guard

`node tools/test_profile_minutes.js` — thuần toán, không cần browser lẫn mock: nó **bóc bảng
`fields` thật** ra khỏi `data/script.js` (khớp ngoặc, không regex) rồi chạy.

Ghim: round-trip ở mặc định (600 s↔10′, **900 s↔15′**, 120 vòng↔40′) · **mặc định của firmware đọc
thẳng từ `define.h`** nên đổi một bên mà quên bên kia là đỏ · clamp 60′→130 vòng, 0′→1 vòng ·
`time per loop` không còn là field · mọi field thời gian phải có **cả hai** chiều `toUi`/`toDev` ·
biên phút quy ra không vượt giới hạn firmware · `perLoopMs()` hỏi thiết bị.

Lý do phải có guard: sai hệ số 60 vẫn ra một con số **trông hợp lý**, và chỉ lộ ra dưới dạng một run
kéo dài 40 giây hoặc 40 tiếng.

### Negative test tìm ra một lỗ trong chính guard

Bản đầu của check "perLoopMs hỏi thiết bị" chỉ **regex thân hàm** xem có `getPath(cfgCache,
"time per loop")` không. Gieo lỗi `return 20000;` — để lại lệnh đọc **chết** ngay phía trên — thì
guard **vẫn xanh**.

Sửa: guard **gọi hàm thật** (bóc thân hàm ra, inject `cfgCache` giả). Không pattern-match được nữa.

4/4 seed đỏ đúng check: nghịch đảo hệ số 60 · bỏ clamp 130 · hardcode 20000 · trả lại field
`time per loop`.

*(Anchor của seed phải viết bằng `\r\n` — `data/script.js` là CRLF, 2776 dòng, 0 LF đơn. Đây là lần
thứ tư trong repo này một seed `\n` khớp 0 lần và suýt cấp chứng nhận cho một guard không chạy.)*

## File đã sửa

| File | Việc |
| --- | --- |
| `data/script.js` | móc `toUi`/`toDev`; `perLoopMs()`; viết lại `fields[]` của card profile |
| `src/define.h` | `optopreheatduration`: `15 * 20` (=300 s) → `15 * 60` (=**900 s = 15 phút**) + comment |
| `src/sensor6035.h` | comment `eSensorpreheat`: "15 mins" nay là **đúng** |
| `JsonPara/pass_file_initial_Full.json` · `pass_file_processTest.json` | `"opto preheat time"` 300 → **900** |
| `tools/test_profile_minutes.js` | guard mới |
| `CLAUDE.md` · `docs/GUI_SSE/GUI.md` · `docs/architecture/04-nhiet-va-sensor.md` | theo quy ước repo |

## Kiểm chứng

- `node tools/test_profile_minutes.js` ✓ (26 check) · negative 4/4 đỏ
- `test_device_id` · `test_web_assets` · `test_phase0_guards` · `test_qr_payload` ✓
- Build SUCCESS, flash **71.9%**, 0 cảnh báo

**Còn phải thử trên máy thật**: mở Setting → Profile Configuration, xác nhận 5 ô hiện
10 / 40 / 5 phút ở máy mặc định, Save rồi `GET /config` phải trả về `600` / `120` / `300`.
Nạp bằng `pio run -e esp32dev -t upload` — UI nhúng trong firmware, **không cần `uploadfs`**.
