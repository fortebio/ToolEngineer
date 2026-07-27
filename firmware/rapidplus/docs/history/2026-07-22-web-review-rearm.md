# 2026-07-22 — Web Result tự hiện run cũ (không cần nhấn giữ nút trắng)

## Triệu chứng

Trên web app: **Result → chart KHÔNG hiện data run cũ**; chỉ khi **nhấn giữ nút trắng** (vật lý)
thì kết quả mới hiện.

## Gốc rễ (workflow 4-agent)

Web chỉ thử nạp run cũ **ĐÚNG MỘT LẦN** lúc bấm vào tab Result (`loadResultSlots` → `reviewStoredRun`,
data/script.js). Nếu ngay lúc đó máy **bận** (heating/preheat/calib — báo phase `"heater"`, KHÔNG
phải `"amplification"`, nên guard client `curPhase!=="amplification"` **không chặn** cú POST vô vọng),
firmware trả **409** (`handleReviewLast` → `dashboardDeviceBusy()`), `reviewStoredRun` **bỏ cuộc,
KHÔNG retry** khi máy rảnh sau đó. Không timer, không SSE re-arm.

**Nút trắng** đi đường khác: `handleLongPress_White` → `escreenReview` → `screen_Result('r')` chạy
thẳng trên DisplayTask, nạp EEPROM + `dashboardSetResults` (`gResultsReady=true`) **không qua guard
busy** → web thấy kết quả. (Lưu ý: nút trắng chỉ khôi phục **bảng**; nó KHÔNG set `lastRunLoops` nên
sau reboot **chart vẫn trống** — đường web `/reviewlast` mới set `lastRunLoops`, đúng hơn.)

## Fix (chỉ client `data/script.js`, không đổi firmware)

Tự thử lại review trên **cạnh bận→rảnh** (dùng SSE `home` tick đã mang sẵn `status.busy`):

```js
var prevBusy = false;                       // module flag
// trong renderHome(d), sau curPhase = phase:
if (prevBusy && !s.busy &&
    document.getElementById("screen-result").classList.contains("active"))
  loadResultSlots();
prevBusy = !!s.busy;
```

Edge-triggered (không level) → không spam `/slots`, không loop trên máy EEPROM rỗng. Khi máy rời
busy lúc đang mở tab Result → nạp lại run cũ (bảng + chart) tự động. Chart tự vẽ qua các caller
`loadCurve` sẵn có.

**Không resurrect run mới**: khi amplification `s.busy=true` chặn re-arm; và `loadResultSlots` chỉ
review khi `/slots ready===false` + guard `curPhase!=="amplification"`; firmware `/slots ready =
gResultsReady && type_infor != eoptoreading` cũng giấu cache cũ giữa run.

## Kiểm chứng

- `node tools/test_review_reboot.js` (Edge headless + mock `--reboot`) → **ALL PASSED** (đường cũ
  review-after-reboot còn nguyên; JS không lỗi). `sse_test_server.py selftest` OK.
- Nạp `uploadall` COM18, boot OK.

## Còn lại (track riêng, chưa làm)

Cho nút trắng cũng có chart sau reboot: route `escreenReview` qua length-scan + `setLastRunLoops` như
worker PEND_REVIEW (ForteSetting.cpp). Người dùng chưa chọn làm.

## Nạp

Đổi `data/` → cần `uploadfs`/`uploadall`.
