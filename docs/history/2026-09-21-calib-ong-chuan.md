# 2026-09-21 — Tool ống chuẩn hiệu chuẩn quang (server `/calib/*` + app tab "Hiệu chuẩn")

Chạm 2 phần: `server/` và `apps/fbt_rapid/`. Kế hoạch + hợp đồng + đối chiếu tài liệu:
[`server/docs/plan/calib-ong-chuan.md`](../../server/docs/plan/calib-ong-chuan.md).

## Đầu vào
- `KHAI'S HANDOVER.md` + `RAPIDPlus calibration tubes.xlsx` (bàn giao 2026-09): quy trình pha FAM, chia ống, đọc raw
  một khe, Apps Script xếp hạng tổ hợp.
- `Optical Calibration WI` (DxD Hub 18/07/2024): protocol gốc, `Fluorescein dilution calculator`, `LOD_template`
  — tiêu chí R² > 0,95, **LOD = 3,3·SD(blank)/slope < 20 nM**, bảo quản parafilm/4–10 °C/tối, đo qua serial
  115200 gửi `0` → `raw,calibrated`.

## Server (`server/`)
`app/calib.py` + `config.CALIB_DIR` + 13 route `/calib/*` + `tests/test_calib.py` (13 test; cả bộ 130/130).
Chi tiết trong `server/docs/history/2026-09-21.md`.

## App (`apps/fbt_rapid/`)
- `services/calib_api.dart` — client `/calib/*` (PUT/GET/DELETE), model `CalibBatchMeta`/`CalibSet`/`CalibCombo`/`CalibRank`.
- `screens/calib_screen.dart` — tab Hiệu chuẩn: **Lô pha** (lọc, tạo lô với lot/stock nM/số ống/µL) · **Bộ ống**
  (lọc trạng thái + số máy; Cấp máy(SN) / Thu hồi / Dùng hết / Huỷ; cảnh HẾT HẠN) · **Ngưỡng** (version, R² min,
  LOD max, slope, hạn dùng — chỉ `canEditLimits`).
- `screens/calib_batch_screen.dart` — màn lô 5 thẻ: nguyên liệu + máy tham chiếu → checklist bước pha (tick = PUT
  ngay `done_at`/`by`, ô thể tích thực tế) → bảng số đo ống × nồng độ (Lưu số đo) → Xếp hạng (blank n/mean/SD/SNR,
  ngưỡng đang áp, gợi ý bộ tick sẵn, hạn dùng, **Đóng gói**, bảng top 30 với LOD/PASS) → bộ ống của lô.
  Menu ⋮: đóng/mở lô, xoá lô tạo nhầm.
- `models/user_session.dart` + `services/session_store.dart`: `canSeeCalib`/`canWriteCalib` (root/admin/manager/
  operator), `SessionStore.username` (ghi `by=`). `home_shell.dart`: tab sau Sản xuất. `i18n.dart`: `nav.calib`, `calib.*`.

## ⚠️ Chưa kiểm được phía app
Máy `Admin` không có Flutter (chỉ box ADM có) → **chưa `flutter analyze`, chưa mở tab**. Việc đầu tiên trên box ADM:
```powershell
Set-Location apps\fbt_rapid; & C:\Users\ADM\fvm\versions\3.44.1\bin\flutter.bat analyze lib/ 2>&1 | Select-String "error|warning|No issues"
```
rồi `localtest.ps1` + build web, mở tab Hiệu chuẩn tạo một lô, nhập 3 ống × 4 nồng độ theo sheet mẫu, Xếp hạng →
tổ hợp #1 phải là 300/3 · 200/2 · 100/3 · 0/3 (slope 3,033, R² 0,99998, LOD 8,3 nM).

## Việc sau (P1)
Đọc raw tự động qua COM/Web Serial (gửi `0`, parse `raw,calibrated` điền ô đang chọn); in nhãn túi zip; nối bộ ống
với `/devices` để cảnh báo bộ hết hạn còn `issued`.

## Bổ sung cùng ngày — đọc số thô tự động (P1 xong)
- `apps/fbt_rapid/lib/services/calib_reader.dart` (thuần Dart, 11 test): `parseCalibRaw` nhận `{Green: N}` (fleet
  Rapid+, đối chiếu `firmware/rapidplus/src/sensor6035.cpp::testShot`) và `raw,calibrated` (Beta WI);
  `calibSlotCommand(slot)` = **1 byte** `'0'..'9'` (firmware chỉ nhận lệnh opto khi `recvLen == 1`); `CalibReader`
  gom dòng, một lần đọc một lúc, timeout 6 s, bắt "Opto sensor error"; `nextCalibCell` thứ tự ống.
- `calib_batch_screen.dart` thẻ 3: panel "Đọc từ máy" (COM desktop / Web Serial), ô đang chọn tô viền, bấm ô để
  đổi, ĐỌC/Enter/Space → điền + `PUT` ngay từng ô + nhảy ống kế; phím bắt bằng `HardwareKeyboard` (bỏ qua khi
  đang gõ ô nhập/dialog).
- Kiểm: Flutter 3.44.1 đã có trên máy Admin → `flutter analyze lib/` sạch (10 info có sẵn), test 24/24; build web
  local + server Docker, cổng Web Serial giả: 5 ống đọc liên tiếp, byte gửi `[48]`, DTR/RTS off, server lưu đủ
  `300/1..5`. Chưa thử máy thật (cần máy Rapid+ ở màn hình chính, cắm USB).
- Thêm ô **Khe đọc 1–10** trong panel (yêu cầu cùng ngày): chọn = cập nhật ô thẻ 1 + `PUT reader.slot` ngay; nút ĐỌC
  ghi rõ khe; viền đỏ khi lô chưa ghi khe (đang mặc định khe 1). Thử: chọn khe 3 → byte gửi `[50]` = `'2'`, server lưu `reader.slot: 3`.
- Bẫy: PowerShell `-replace` với `$_` trong chuỗi thay thế nhét cả file vào giữa file (ghi ở CLAUDE.md gốc);
  browser tool `key` không tới canvas Flutter → dispatch KeyboardEvent vào `flutter-view` (CLAUDE.md app).

## Bổ sung cùng ngày — firmware máy tham chiếu v2.4.6 + app nhận số từ nút máy
- Máy thật báo "Opto sensor error" ngay lệnh đọc đầu → rà mã: `testShot` chạy trên SettingTask **không giữ
  `gI2CMutex`** trong khi SensorTask idle ở `eSensormaintain` quay LED qua I2C. Sửa trong clone riêng
  `firmware/FBT-RapidPlus/` (nhánh `v2.4.5at-calib-solution`, v2.4.6; monorepo không track — `.gitignore`):
  `I2CLock` (define.h), `sensor6035::readSlotRaw` (chờ LED, AcquisitionControl cục bộ, luôn đóng mux/tắt LED,
  in đúng `slot+1`), `'0'..'9'`/`'R'` bọc mutex; **chế độ `eCalibTube`**: màn LCD `screen_CalibTube` (khe ·
  nhãn · số cỡ 5 · #n READY/READING/ERROR), lệnh `CalibStart[,n]`/`CalibSlot,n`/`CalibLabel,<ascii>`/
  `CalibShot`/`CalibEnd` (ForteSetting, trước `restart()`), nút ĐỎ = đọc (cờ → SettingTask), XANH = khe +1
  (`{CalibSlot: n}`), TRẮNG = thoát; web phase `calibtube` (busy). Guard `tools/test_status_coverage.py` xanh.
  Chi tiết + kịch bản thử máy: `firmware/FBT-RapidPlus/docs/history/2026-09-21-che-do-doc-ong-chuan-calib-tube.md`.
- App: `calib_reader.dart` thêm `sendCommand`/`startMode`/`setSlot`/`setLabel`/`endMode`, stream `readings`
  (số máy tự in khi bấm ĐỎ — chỉ phát khi không có `readSlot` đang chờ), `slotChanges`, `calibErrors`,
  `modeOn`; `readSlot` vẫn 1 byte (firmware cũ). Màn lô: Kết nối → `CalibStart,<khe>` + nhãn ô đang chọn;
  mỗi lần đọc (app hay máy) → `_applyReading` (điền, PUT, nhảy ống, gửi nhãn mới); đổi khe hai chiều không
  vòng lặp (`fromDevice`); Ngắt/rời màn → `CalibEnd`. 17 test reader, analyze sạch.
