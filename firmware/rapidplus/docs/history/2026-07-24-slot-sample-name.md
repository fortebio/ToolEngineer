# 2026-07-24 — Thêm ô "tên mẫu" cạnh dropdown bệnh

## Yêu cầu

> "Thêm cột đặt tên mẫu vào cạnh cột tên bệnh."

Mỗi slot trước đây chỉ có **một** nhãn = **bệnh** (chọn từ danh sách cố định
`{PC,EHP,EMS,WSSV,TPD}` qua `<select>`). Giờ thêm một trường **free-text** để người dùng gõ
**định danh mẫu** riêng (ví dụ ao/bể/ống: `Pond-3`, `Tank-A`), nằm **ngay cạnh** dropdown bệnh
trong cả bảng **đặt tên** (Home) lẫn bảng **Result**.

## Lưu ở đâu: file riêng `/slotsamples.json`

`slotNames[10]` (bệnh) ↔ `/slotnames.json` **giữ nguyên**. Tên mẫu đi vào **mảng + file
riêng**:

- `slotSamples[10]` ↔ `/slotsamples.json` (cùng khuôn `loadSlotNames`/`saveSlotNames`).
- **Không** nhét chung một file: giữ format string-array của `slotnames.json` **nguyên vẹn** →
  firmware cũ đọc `/slotnames.json` vẫn đúng, chỉ đơn giản không thấy file sample. Không migration,
  không rủi ro đọc rác.
- Cả hai là **label thuần web** (mảng `static` trong `webDashboard.cpp`) — **không** chảy vào
  upload (GAS/ingest/ERP) hay màn TFT. Thêm cột này không đụng đường dữ liệu kết quả.

## `/rename` — hai trường độc lập

`GET/POST /rename?slot=<0-9>&name=<disease>&sample=<label>`:

- Chỉ **`slot`** là bắt buộc; gửi **`name` và/hoặc `sample`**, cập nhật cái nào có mặt (giống
  cách `JsonDataConfig` chỉ áp key có mặt). Client cũ chỉ gửi `name=` vẫn chạy y như trước —
  **tương thích ngược**.
- Mỗi trường **cap 32 ký tự** phía firmware (`substring(0,32)`): dashboard không xác thực trên
  LAN, chặn caller (curl/tab lạ) bơm label khổng lồ làm phình LittleFS.
- `GET /slots` thêm field `sample` mỗi slot: `{name, sample, ct, result}`.

## Client (`script.js`)

- `slotSamples[10]`; `ingestSlots` đọc thêm `slots[i].sample`.
- `makeSampleInput(i, value)` → `<input type=text class="sample-name" maxlength=32
  placeholder="Sample">`, `change` → `onRenameSample` → `POST /rename?slot=i&sample=…`, đồng bộ
  ô sinh đôi ở bảng kia (`.sample-name[data-slot=i]`).
- `buildTable` chèn ô sample **sau** dropdown bệnh trong `.sample-cell`.
- **Ô sample dùng class riêng `.sample-name`, KHÔNG phải `.slot-name`** — để `fitNameColumn`
  vẫn đo độ rộng cột **chỉ theo chữ bệnh**; ô sample lấy phần flex còn lại. `fitNameColumn` cộng
  thêm **budget cố định (132px)** cho ô sample nên cột đủ rộng cho hai trường cạnh nhau; hàng hẹp
  thì ô sample wrap xuống dưới.
- **Chart legend giữ nguyên = tên bệnh** (`applyNamesTo` vẫn dùng `slotNames`): tên mẫu là định
  danh per-slot, không phải nhãn đường cong.

## CSS (`style.css`)

- `.sample-cell { flex-wrap: wrap }` — bệnh + sample **cạnh nhau khi cột rộng**, ô sample **rơi
  xuống dòng dưới khi hàng hẹp** (bảng Result trên mobile: CT/Result đã ăn chỗ) thay vì bóp cả hai
  thành sợi chỉ không đọc được.
- `.disease-sel` thu `max-width: 11rem → 8rem` để chừa chỗ; `.sample-name` `flex: 2 1 6rem;
  max-width: 13rem` + chrome input (viền/nền/focus) riêng.
- Media wide của `#namingCard`: block căn giữa nới `30rem → 42rem`, bệnh `20rem → 12rem`, sample
  `18rem`.
- `.sample-name:focus-visible` thêm vào rule ring cuối file (a11y).

## Kiểm chứng

- `pio run -e esp32dev` → **SUCCESS**, Flash **70.4%** (+~0.8%), không lỗi/cảnh báo.
- `node --check data/script.js` OK; mock parse OK.
- Mock roundtrip: `/slots` slot có key `sample`; `POST /rename?slot=0&sample=Tank-A` →
  `/slots` slot0 `sample:"Tank-A"`.
- **CDP screenshot** (`ui_screenshot.js`, mock 8080):
  - **Naming card** (Home, pha `waitamp`) mobile 390px **và** desktop 1280px: `[● #1] [WSSV ▾]
    [Pond-3]` **cạnh nhau một dòng**, căn giữa cân đối.
  - **Result table** desktop: cạnh nhau; mobile: ô sample **wrap xuống dòng** (đúng thiết kế
    responsive — cột Sample nhường CT/Result).

## Ghi chú

- Tên mẫu **web-only**, không lên upload/TFT — nếu sau này cần đẩy tên mẫu vào Google Sheet/ERP
  thì phải nối vào đường `postData_GoogleSheet`, không tự động có.
- Mobile bảng Result wrap là **cố ý**; naming card (1 cột full-width) đủ rộng nên cạnh nhau kể cả
  trên điện thoại.
