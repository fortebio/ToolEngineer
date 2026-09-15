# 2026-07-17 — Sửa các lỗi từ code review (14 findings)

Review `xhigh` trên diff của session này ra 14 finding. #2 và #3 sửa ở lượt trước
([2026-07-17-...]); lượt này sửa phần còn lại.

## Correctness

- **#1 — `info_display*` gọi `SerialBT` không guard.** Dashboard giờ nhả BT ngay lúc khởi
  động (mọi boot có STA), nên macro `info_displayf/ln/display` (define.h) bắn vào stack đã
  giải phóng — vi phạm GOTCHA 1 toàn firmware. Fix: bọc `if (!gBtReleased)` **chỉ** quanh
  nhánh `SerialBT`; `DEBUG_COM` (USB) giữ nguyên. Xác nhận: khối CSV `<AmpStart>` (chạy
  qua `info_display` **sau** khi `screen_Result` nhả BT) vẫn ra USB serial.

- **#4 — cancel calib giật `type_infor` từ task AsyncTCP.** `/calib?action=cancel` set
  `type_infor = escreenStart` **vô điều kiện** → POST giữa lúc amplification sẽ **huỷ run**.
  Fix: chặn 409 khi `calibStep(s)` rỗng (không đang calib). Race sensor còn lại được ghi
  `ponytail:` ceiling — các write là scalar atomic, slope chỉ ghi ở `eSaveCalib` nên đo dở
  bị bỏ, không hỏng.

- **#13 — handoff post*/drain thiếu memory barrier.** `volatile` không đảm bảo thứ tự giữa
  2 core: payload (`pendingA`) có thể hiện sau cờ `pendingKind`. Thêm `__sync_synchronize()`
  release (3 poster, sau payload trước cờ) + acquire (drain, sau khi đọc cờ trước payload).

- **#9 — timer về Home không bị huỷ.** Save xong đặt timer 900ms về Home; user đổi tab
  trong cửa sổ đó bị giật ngược. Fix: lưu handle, `cancelBackToHome()` trong handler nav.

- **#14 (mock) — `/wifi` prefix nuốt `/wifiscan`.** `startswith("/wifi")` trong `do_POST`
  bắt cả `/wifiscan`. Firmware (AsyncWebServer) match URL chính xác → mock nói dối hợp
  đồng. Fix: match `urlparse(path).path == "/wifi"`.

## Cleanup

- **#8 — `.gz` tự sinh không gitignore.** `data/{index.html,script.js,style.css}.gz` là
  build output (tools/pio_gzip_data.py sinh lại mỗi lần) → thêm vào `.gitignore`.
  `highcharts.js.gz` **không** ignore (ship sẵn từ upstream, đã track).

- **#10 — `lastRunLoops = 0` trong clear() đọc như per-run.** `clear()` chỉ chạy lúc boot,
  nên dòng này KHÔNG invalidate per-run — chính vì thế `handleCurve` cần 2 guard. Thêm
  comment `ponytail:` nói rõ, tránh bẫy người đọc sau (bug này đã ship 2 lần vì hiểu nhầm).

## Không sửa (có lý do)

- **#5** — đã tự khỏi nhờ #2: `loadConfig()` giờ chạy **sau** khi thiết bị xác nhận applied,
  không còn đọc giá trị cũ.
- **#6** — `test_webcurve` test bản sao `CurveWriter`: inherent với `test_build_src=no`
  (không link src/). Đã có comment "mirrors the one in webDashboard.cpp".
- **#7** — POST endpoint chạy trên phần cứng: đã test `POST /config` thật ở lượt trước
  (queued→applied, giá trị đổi 82→81.5). Cùng đường body-handler với /wifi, /deviceid.
- **#11** (scan phá STA) và **#12** (`restartAt` 1.5s): inherent (1 radio) / ceiling hợp lý.
  Không thêm phức tạp.

## Kiểm chứng

- `pio run -e esp32dev` → SUCCESS (RAM 22.9%, Flash 68.9%).
- Mock (HTTP + CDP): #14 POST /wifiscan → 404 (không nuốt sang /wifi); #4 cancel khi không
  calib → 409; #9 đổi tab trong 900ms → **ở lại tab đã chọn**, save thường vẫn tự về Home.
- #1/#13 là firmware-only: build compile + logic; #1 xác nhận qua USB serial còn sống.

## Nạp

Đổi cả firmware + `data/` → `pio run -e esp32dev -t uploadall`.
