# Ống chuẩn hiệu chuẩn quang (Fluorescein) — `/calib/*` + tab "Hiệu chuẩn"

Thay quy trình **Google Sheet + Apps Script** bàn giao 2026-09 (`KHAI'S HANDOVER.md`, sheet
"RAPIDPlus calibration tubes") bằng công cụ trên Engineer Server + app FBT_RAPID, đối chiếu với
tài liệu gốc **DxD Hub "Optical Calibration for Beta Prototype with Fluorescein"** (18/07/2024,
kèm `Fluorescein dilution calculator` và `LOD_template_Fluo_240717`).

## Quy trình thật (đã đối chiếu 2 nguồn)

| Bước | Bàn giao (Khai, 2026-09) | WI gốc (DxD Hub, 07/2024) | Tool làm gì |
|---|---|---|---|
| Thuốc thử | FAM "52 mM" trong tủ lạnh | Fluorescein **NIST-traceable** ThermoFisher **F36915** | Lô mới ghi lot/HSD; mặc định catalog F36915; stock mặc định **52 000 nM** (xem §Lệch đơn vị) |
| Pha | stock → 1000 nM (10 + 510 µL) → 300/200/100 nM (mỗi 300 µL) từ 1000 nM, EDTA pH 8; ly tâm mỗi bước, tránh nắng | Tris pH 8, pha **nối tiếp** 5000 → 1000 → 150 → 100 → 50 nM, thể tích = nhu cầu × 120 % | Bảng pha tính C1·V1 = C2·V2 theo mẫu bàn giao (nồng độ 300/200/100/0 là 4 điểm firmware Rapid+ dùng); mỗi bước có DỰ KIẾN + ô THỰC TẾ + giờ/người tick |
| Chia ống | 25 µL × 10 ống/nồng độ (0,5 mL) | 25 µL; 3 ống/nồng độ + 1 blank | `tubes_per_conc`/`aliquot_ul` cấu hình theo lô, mặc định 10 × 25 µL; kiểm 10 × 25 ≤ 300 µL |
| Bảo quản | túi zip, tủ lạnh | parafilm, hộp kín tối, 4–10 °C; tín hiệu +20 %/12 tháng do bay hơi | bước `seal` trong checklist; `shelf_days` (mặc định 90) → `expires_at` của bộ |
| Đo | USB → Serial Debug Assistant, đọc **một khe**, gõ vào sheet | 115200, gửi `0` → `raw,calibrated`; cùng hướng mũi ống; đóng nắp nếu sáng | bảng ống × nồng độ trong app, `reader{device, slot, fw}`; **đọc tự động**: app gửi 1 byte khe qua COM/Web Serial, nhận `{Green: N}`, lưu ngay từng ô, Enter = ống kế (`calib_reader.dart`) |
| Chọn | Apps Script thử mọi tổ hợp 1 ống/nồng độ, xếp R² & slope, PASS/FAIL, mỗi tổ hợp = 1 túi | R² > 0,95 mỗi khe; **LOD = 3,3·SD(10 blank)/slope < 20 nM** | `rank_combinations`: mọi tổ hợp, R² giảm dần rồi slope; **thêm LOD** từ SD các ống 0 nM; ngưỡng có version; **gợi ý bộ không trùng ống** |
| Cấp phát | (không có) | (không có) | bộ `CB<yymmdd>-<nn>-S<nn>`: stored → issued(SN máy) → used / discarded, `history[]` |

### Lệch đơn vị stock — PHẢI KIỂM
Bàn giao ghi stock **52 mM** nhưng bảng pha 10 µL + 510 µL (52×) ra 1000 nM chỉ đúng khi stock là
**52 µM** (52 000 nM); 52 mM/52 = 1 mM. Tool lấy 52 000 nM làm mặc định (khớp bảng pha) và app cảnh báo
đỏ khi `stock.conc_nM > 1 000 000`. Ai pha lô kế tiếp đọc nhãn ống rồi sửa số trong lô.

### Vì sao ngưỡng chọn ống siết hơn WI
WI chấp nhận **máy** ở R² > 0,95 (3 ống trung bình). Bộ ống là **vật chuẩn** đem hiệu chuẩn cả fleet, nên
mặc định `r2_min = 0,995` (sheet bàn giao PASS ở 0,998), `lod_max = 20 nM` như WI, slope không giới hạn
(phụ thuộc máy tham chiếu — đặt sau khi có thống kê). Đổi = `PUT /calib/limits` (token admin OTA), một
version = một nội dung, bộ ống ghi `limits_ver` + `verdict` lúc đóng.

## Hợp đồng server (`app/calib.py`, route trong `app/main.py`; KHÔNG POST)

| Method | Path | Việc |
|---|---|---|
| `GET` | `/calib/template?stock_nM&working_nM&working_total_ul&target_total_ul&tubes_per_conc&aliquot_ul` | Khung lô: nguyên liệu + bảng pha (0 = mặc định) |
| `GET` `PUT` | `/calib/limits?by=` | Ngưỡng `{version, r2_min, lod_max, slope_min, slope_max, shelf_days}` (PUT: `ota_admin`) |
| `GET` | `/calib/batches?status=` | Danh sách lô (tóm tắt: bước xong, số đo, số bộ) |
| `PUT` | `/calib/batches?by=` | Tạo lô — body = khung đã sửa (`stock`, `buffer`, `concentrations`, `tubes_per_conc`, `aliquot_ul`, `note`) hoặc rỗng |
| `GET` `PUT` `DELETE` | `/calib/batches/{id}` | Lô đầy đủ (kèm `sets`) / cập nhật MỘT PHẦN (`stock/buffer/reader/note/status`, `steps[{code, done_at, by, actual_*}]` gộp theo code, `readings{nM:{ống: raw\|null}}` gộp từng ô) / xoá khi chưa có bộ |
| `GET` | `/calib/batches/{id}/rank?top=` | `{combos[:top], total, pass, suggested_sets, blank{n, mean, sd, snr33}, limits, tubes_in_sets, missing}` |
| `PUT` | `/calib/batches/{id}/sets?by=` | `{combos:[{tubes, rank?}], expires_days?}` → bộ `<lô>-S<nn>`; server TÍNH LẠI hồi quy + LOD, chặn ống trùng bộ (409) |
| `GET` | `/calib/sets?status&device&batch` · `/calib/sets/{id}` | Bộ ống |
| `PUT` | `/calib/sets/{id}?status&device&by&note` | stored → issued (device bắt buộc) \| discarded; issued → used \| discarded \| stored (thu hồi); used/discarded là cuối |

Kho file `FBT_CALIB_DIR` (mặc định `~/fbt_server/calib`): `batches/<id>.json`, `sets/<id>.json`,
`limits.json`, `history.jsonl` (append-only mọi thao tác ghi). Không bảng DB — vài chục lô/năm.

Trạng thái lô: `prep` (tạo) → `measure` (tự chuyển khi có số đo) → `ranked` (khi đóng bộ) → `closed`
(khoá sửa, mở lại được). Mã lô `CB<yymmdd>-<nn>` theo ngày địa phương của box (nhãn túi).

Quyền: mọi route ghi lô/số đo/bộ gác `auth` (token thường) — app tự gác vai trò
(`canWriteCalib` = root/admin/manager/operator; `user` không thấy tab); ngưỡng gác `ota_admin`
(app: `canEditLimits` = root/admin), giống `/ate/limits`.

## Phía app (`apps/fbt_rapid`)

- `services/calib_api.dart` — client + model `CalibBatchMeta`, `CalibSet`, `CalibCombo`, `CalibRank`.
- `screens/calib_screen.dart` — tab **Hiệu chuẩn** (`AppTabScaffold`, lazy): **Lô pha** (lọc trạng thái, tạo
  lô) · **Bộ ống** (lọc trạng thái/số máy; Cấp máy / Thu hồi / Dùng hết / Huỷ) · **Ngưỡng**.
- `screens/calib_batch_screen.dart` — một lô: 1 nguyên liệu + máy tham chiếu · 2 checklist bước pha (tick =
  ghi giờ/người ngay, ô thể tích thực tế) · 3 bảng số đo ống × nồng độ · 4 xếp hạng (blank, ngưỡng, gợi ý
  bộ tick sẵn, hạn dùng, **Đóng gói**, bảng top 30) · 5 bộ ống của lô. Menu ⋮: đóng/mở lô, xoá lô tạo nhầm.
- `home_shell.dart`: tab sau Sản xuất, gác `canSeeCalib`; i18n `nav.calib`, `calib.*`.

## Trạng thái

| Pha | Nội dung | Trạng thái |
|---|---|---|
| P0 | Server `/calib/*` + `app/calib.py` + `tests/test_calib.py` (13 test, số liệu sheet bàn giao + template LOD) | **Xong 2026-09-21**, ĐÃ DEPLOY 2026-09-23 |
| P0 | App: tab Hiệu chuẩn 3 mục + màn lô | **Xong 2026-09-21** — `flutter analyze` sạch, chạy thử bản web local (server Docker) |
| P1 | **Đọc raw tự động** (`services/calib_reader.dart`, test 11 ca): panel "Đọc từ máy" ở thẻ số đo — COM (desktop) / Web Serial (web), gửi ĐÚNG 1 byte `'0'..'9'` = khe 1..10 (firmware `OptoCommandProcess`, `recvLen == 1`), nhận `{Green: N}` (fleet) hoặc `raw,calibrated` (Beta WI), điền ô đang chọn, LƯU NGAY từng ô, tự nhảy ống kế; Enter/Space = ĐỌC (HardwareKeyboard, bỏ qua khi đang gõ ô nhập/dialog) | **Xong 2026-09-21**, thử bằng cổng Web Serial giả: 5 ống đọc liên tiếp, byte gửi đúng, server nhận đủ. Chưa thử máy thật |
| P1 | **Firmware máy tham chiếu** (`firmware/FBT-RapidPlus/`, clone riêng FBT-DXD, nhánh `v2.4.5at-calib-solution`, v2.4.6): fix `testShot` không giữ `gI2CMutex` (máy thật báo "Opto sensor error" ngay lệnh đầu — SensorTask quay LED qua I2C khi rảnh); chế độ **`eCalibTube`**: LCD hiện khe/nhãn app gửi/số thô cỡ lớn/#lần, lệnh `CalibStart/Slot/Label/Shot/End`, nút ĐỎ trên máy = đọc (app tự điền qua stream `readings`), XANH = khe +1, TRẮNG = thoát | **Code xong 2026-09-21** (build + guard web xanh; app 17 test), CHƯA nạp máy thật — kịch bản thử trong `firmware/FBT-RapidPlus/docs/history/2026-09-21-che-do-doc-ong-chuan-calib-tube.md` |
| P1 | In nhãn túi zip (mã bộ + 4 ống + R²/LOD + hạn) — PDF/ảnh từ app | Chưa |
| P2 | Nối với hồ sơ ATE/`/devices`: máy nào đang giữ bộ nào, cảnh báo bộ hết hạn còn `issued` | Chưa |
| P2 | Vật chuẩn rắn (Starna) khi fleet lớn — WI gợi ý; tool chỉ cần thêm `kind` cho bộ | Chưa |

## Kiểm nhanh
```powershell
Set-Location server; & C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe -m pytest tests/test_calib.py -q
```
