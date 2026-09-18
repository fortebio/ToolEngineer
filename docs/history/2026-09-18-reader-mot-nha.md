# 2026-09-18 — Firmware Reader về một nhà: `firmware/FBT-Reader`

Chạm 3 phần: `system/` (registry + schema), `firmware/`, `CLAUDE.md` gốc.
Nhật ký phía firmware: `firmware/FBT-Reader/docs/history/2026-09-18-to-chuc-lai-thu-muc.md`.

## Trước thế nào

Hai bản sao cùng một sản phẩm nằm cạnh nhau trong `firmware/`:

| | `firmware/reader/` | `firmware/FBT-Reader/` |
| --- | --- | --- |
| Phiên bản | v2.6.6 | **v2.6.7** |
| Git | được monorepo track | **repo riêng** `github.com/wuanpham/FBT-Reader`, monorepo thấy là `?? untracked` |
| Có gì thêm | — | `rtos.cpp/h` (5 task FreeRTOS), `webportal.cpp/h` + `webpage.h` (cổng web HTTP+WS), `otaDebug.cpp/h`, `updateOTA.json` |

`system/products.yaml` khai `reader.firmware.dir = firmware/reader`, tức **registry trỏ vào
bản cũ**. `registry_check.py` vẫn xanh vì nó chỉ kiểm thư mục có tồn tại và regex bắt được
phiên bản — nó không biết có bản mới hơn nằm ngay bên cạnh.

Đối chiếu từng file trước khi quyết: mọi file của `firmware/reader` đều có trong
`firmware/FBT-Reader` với nội dung mới hơn (Bluetooth.cpp 400→598 dòng, displayLCD.cpp
1211→1485, button.cpp 558→713, sensor.cpp 251→352). Hàm duy nhất "mất" là
`sensor::calib_SensorLight` — thực ra chỉ đổi kiểu trả về `uint16_t` → `float`. Không có
việc nào của `firmware/reader` chưa có ở bản mới.

## Nay thế nào

**`firmware/reader/` đã xoá.** Lấy lại được bằng lịch sử git của monorepo nếu cần.

**`firmware/FBT-Reader/` giữ nguyên là repo git riêng**, không chuyển thành submodule và
không gộp vào monorepo. Lý do: repo đó mang **9 nhánh phiên bản từ v1.6 tới v2.6.7** trên
GitHub; gộp vào là phải chọn giữa mất lịch sử đó hoặc kéo cả 9 nhánh vào monorepo, cả hai
đều đắt hơn lợi ích.

Vì vậy monorepo **không track** thư mục đó — đã thêm vào `.gitignore` gốc kèm lý do, để
`git status` không còn báo `?? firmware/FBT-Reader/` mãi mãi.

## Sửa theo

| File | Sửa gì |
| --- | --- |
| `system/products.yaml` | `reader.firmware.dir`: `firmware/reader` → `firmware/FBT-Reader`. `ota: false` → `true` (v2.6.7 có OTA qua repo `wuanpham/FBTRapidReaderOTA` **và** nạp `.bin` từ trình duyệt) |
| `system/contracts/products.schema.json` | `firmware.dir` pattern `^firmware/[a-z0-9-]+$` → `^firmware/[A-Za-z0-9-]+$` |
| `CLAUDE.md` (gốc) | Dòng bản đồ `firmware/reader/` → `firmware/FBT-Reader/` |
| `.gitignore` (gốc) | Thêm `firmware/FBT-Reader/`; tiện thể sửa dòng `.idea/` bị dính liền comment nên **không ignore được gì** |

### Vì sao nới schema thay vì đổi tên thư mục

Mọi thư mục firmware khác đều chữ thường (`rapidplus`, `rapid4p`, `rapidplus-prod`), nên
`FBT-Reader` trông lạc lõng và đổi tên nó thành `fbt-reader` là cách rẻ hơn về mặt quy ước.

Đã loại phương án đó: thư mục này là **clone của một repo ngoài**, tên do `git clone` đặt
theo tên repo. Ép chữ thường là làm tên thư mục lệch tên repo — người sau clone về sẽ có
`FBT-Reader` và tự hỏi tại sao registry đòi `fbt-reader`. Việc của schema là bắt lỗi gõ
trong registry, không phải đặt lại tên repo của người khác.

Nếu sau này chốt gộp hẳn vào monorepo thì đổi tên lúc đó và trả pattern về chữ thường.

## Kiểm

```
python tools/registry_check.py     →  ĐẠT: 0 lỗi, 5 cảnh báo, 6 sản phẩm
reader  production  RDR  *  v2.6.7  reader_v2.6.7.bin  firmware/FBT-Reader
```

Năm cảnh báo đều là TODO có từ trước (contract/channels chưa chốt), không phải do lần này.

## Còn nợ

`reader.channels` và `reader.payload.contract` vẫn `null` — chưa kiểm được payload thiết bị
theo schema. `hw: []` và `id_prefix: RDR` vẫn là TODO chờ đối chiếu `device_registry` của
ERP. Nợ phía firmware: `firmware/FBT-Reader/docs/plan/2026-09-18-no-ky-thuat.md`.
