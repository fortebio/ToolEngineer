# Pha 0 của kế hoạch nâng fleet lên v2.4.3 (2026-07-29)

Thực hiện [docs/plan/2026-07-28-ota-fleet-upgrade-243.md](../plan/2026-07-28-ota-fleet-upgrade-243.md)
mục **Pha 0 — chặn cứng, làm trước mọi thứ**. Đây là các việc phải xong **trước** khi build
bất kỳ `.bin` nào định đẩy lên repo OTA public. Chưa đụng gì tới Pha 1 (nhúng asset) / Pha 2.

## 1. `currentVersion` 18 → 19 (`src/updateOTA.cpp:3`)

Không bump thì máy vừa lên 2.4.3 xong sẽ thấy `versionCode 19 > 18` (`updateOTA.cpp:45`) →
prompt OTA **mỗi lần boot** → tải lại 2.36 MB mỗi lần người dùng bấm ĐỎ. Màn OTA nằm trong
`isBusy()` nên `POST /config` và `/ota` bị **409** suốt thời gian đó. `OTA_DISMISSED` (nút XANH)
chỉ sống trong RAM → reboot là hỏi lại.

## 2. Secrets: `SECRET_*` từ code chết thành đường dùng thật

`src/secrets.h` (gitignored) có sẵn 5 macro nhưng **không ai dùng** — `Bluetooth.cpp:22-27`
hardcode literal, tức cả cơ chế `secrets.h` là trang trí. Nay 5 biến endpoint/token lấy thẳng
từ macro:

```cpp
const char *serverName          = SECRET_GAS_URL;
const char *serverName2         = SECRET_INGEST_URL;
const char *server_engineerToken = SECRET_INGEST_TOKEN;
const char *serverERP           = SECRET_ERP_URL;
const char *server_erpToken     = SECRET_ERP_TOKEN;
```

Đồng thời **scrub `src/secrets.example.h`** — file này **được commit** mà đang chứa GAS URL thật,
ingest Bearer thật và ERP X-API-Key thật (dòng 8, 10, 12). Nay là `PASTE_*` / `your-*-host`.

⚠ **Sửa code KHÔNG phải là rotate.** Token vẫn nằm trong git history và (theo kế hoạch) sẽ nằm
trong `.bin` — `strings` trên artifact thật cho ERP key @offset **3144**, ingest Bearer @**3251**,
đều trong 4 KB đầu. Rotate phía server + redeploy GAS vẫn là **gate của cả rollout**
(mục 9.1 của kế hoạch), chưa làm ở đây.

## 3. `"PCB version"` — lỗ ghi đè EEPROM có chủ đích

`validateConfig` (`webDashboard.cpp:680`) từng `continue` cho `"para version"`/`"PCB version"`
kèm comment *"checked below"* — **không có check nào**, hàm kết thúc ở :827. Đường đi đầy đủ:

`POST /config` → `drainPending` → `strcpy(parameter.PCB_version, ...)` (`ForteSetting.cpp:245`)
vào `char[10]` ở **offset 14** của `parastructure` → đè `slopes@24`, `origins@64`,
`amplification_time@212`, `lysisTemp@216`, `kpid@232`, `kpid2@256`, `kpid3@360`, chạy tiếp
~1 384 byte quá struct vào heap → rồi `EEPROM.commit()`. Tức: **hỏng calib quang + hằng số PID,
ghi vĩnh viễn**.

Vá **hai tầng**, vì hai tầng chặn hai lớp caller khác nhau:

- **Trust boundary (web)**: gộp `"PCB version"` + `"para version"` vào đúng nhánh `<= 9 chars`
  vốn đã có cho `"device ID"`/`"units"`.
- **Nơi copy (mọi caller)**: `strcpy` → **`strlcpy(dst, src, sizeof dst)`** trong `JsonDataConfig()`
  cho cả **4** field `char[10]`: `para_version`, `PCB_version` (`ForteSetting.cpp:241,248`) và
  `units`, `device_id` (`:348,355`). Hai field sau web có validate nhưng **Serial/BT thì không** —
  `JsonDataConfig()` là nơi duy nhất mọi đường đi qua, nên bound phải đặt ở đó. Kế hoạch chỉ nêu
  `PCB version`; ba field còn lại là **cùng một lỗi, cùng một hàm**, sửa luôn.

Không phải remote-exploit dễ: bounded bởi `CFG_BODY_MAX = 1800`, chỉ chạy khi máy **idle**
(`guardBusy`), UI shipped không bao giờ gửi key này. Nhưng nó **mới có trong 2.4.3** (2.4.2 chỉ
Serial/BT tới được) nên không mang nó ra fleet.

## 4. `.pio/libdeps` hỏng → xoá, build lại sạch

`.pio/libdeps/` có **57 file `* - Copy.cpp/.h`** (`AsyncEventSource - Copy.cpp`,
`WebHandlers - Copy.cpp`, …) đang **được biên dịch vào archive** của thư viện. Build cũ link được
là **may**, không phải đúng. Đã `rm -rf .pio/libdeps .pio/build/esp32dev` rồi build lại từ đầu.
Không publish `.bin` dựng từ cây libdeps đó.

## Guard: `tools/test_phase0_guards.py`

Host-side, không cần phần cứng, khoá **3 bất biến** của pha này:

```bash
python tools/test_phase0_guards.py     # exit 0 = pass
```

1. Không `strcpy(parameter.*` ở bất kỳ đâu trong `src/`.
2. `validateConfig()` phải có đủ 4 key `char[10]` trong nhánh `<= 9 chars`.
3. `Bluetooth.cpp` dùng `SECRET_*` (không literal), và `secrets.example.h` chỉ chứa `PASTE_*` /
   `your-*-host`.

Test **được kiểm ngược**: chép `src/` ra thư mục tạm, tái tạo cả 4 regression → guard báo đúng
4 dòng FAIL, exit 1. Bản thân guard **không chứa giá trị token nào** (nếu chứa thì lại commit
secret lần nữa) — nó khớp theo *hình dạng* (macro có mặt / placeholder).

## Chưa làm (vẫn thuộc Pha 0 nhưng không phải việc code)

- **Rotate ingest Bearer + ERP X-API-Key, redeploy GAS deployment mới** — việc phía server,
  là gate của toàn bộ rollout. Chừng nào chưa xong thì **không build `.bin` để publish**.
