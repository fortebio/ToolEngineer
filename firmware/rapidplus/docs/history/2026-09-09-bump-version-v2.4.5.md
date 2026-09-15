# 2026-09-09 — Bump `FIRMWARE_VERSION` lên **v2.4.5AT / v2.4.5a**

Nhánh: `v2.4.5` (rẽ từ `cf6e66e` trên `v2.4.4A`).
File đụng tới: `src/define.h` (đúng một khối), `CLAUDE.md` (link tài liệu này).

## Trước thế nào

`src/define.h` khai `FIRMWARE_VERSION` là **`"v2.4.4a"` / `"v2.4.4AT"`** — trong khi mã nguồn
trên nhánh này **đã chứa toàn bộ** những gì CLAUDE.md gán cho v2.4.5 và v2.4.6:

- `otaMarkInstalled()` + cờ NVS "vừa nạp xong" — `src/updateOTA.cpp:56`, `:359`
- `?ver=` và `&updated=1` trong lượt `/ota/check` — `src/updateOTA.cpp:119-120`
- khớp **chính xác tên file** thay cho `indexOf()` — `src/updateOTA.cpp:181-182`

Tức là **chuỗi version đứng yên trong khi hành vi OTA đã đổi hai lần**. Đó chính là hình dạng
của cái bẫy mà `define.h` tự cảnh báo ("hai ảnh một version"), chỉ khác là lần này nó nằm ở
phía kho mã chứ chưa ra tới máy.

## Nay thế nào

```c
#ifndef FIRMWARE_VERSION
#ifdef SHAPE_RULE_NEGATIVE
#define FIRMWARE_VERSION "v2.4.5a"      // -DSHAPE_RULE_NEGATIVE: shape-flag -> Negative
#else
#define FIRMWARE_VERSION "v2.4.5AT"     // mặc định: shape-flag -> báo F, giữ nguyên kết luận
#endif
#endif
```

Hai trục **giữ nguyên độc lập** như v2.4.3AT/v2.4.4A: quy tắc hình dạng là `-D` switch, chuỗi
version override được theo từng env PlatformIO. Không đổi gì khác — `FirmwareVer` là nguồn duy
nhất, mọi nơi còn lại (`displayLCD.cpp:96,484`, `Bluetooth.cpp:585`, `errorCheck.cpp:35,80`,
`ForteSetting.cpp:1069`, `sensor6035.cpp:1536,2273`, `updateOTA.cpp:119`) đều đọc từ nó.

## Bằng chứng dẫn tới cách đặt tên này

**1. Chưa từng có bản v2.4.5 hay v2.4.6 nào rời khỏi kho này.** Quét mọi nhánh:

| nhánh | `FirmwareVer` |
| --- | --- |
| `main` / `origin/main` | `V1.49` |
| `v2.4.3` | `v2.4.3` |
| `v244-alg` | `v2.4.4.AT` |
| `v2.4.4A` (HEAD cũ) | `v2.4.4a` / `v2.4.4AT` |

`git log --all --grep="2\.4\.5\|2\.4\.6"` → **rỗng**. Không nhánh `v2.4.5`/`v2.4.6` nào tồn tại
trước hôm nay.

**2. `"v2.4.5"` TRẦN là chuỗi đã cháy.** CLAUDE.md ghi `fbt_v2.4.5.bin` **đang nằm trên
server** và là bản build **trước** khi có `?ver=`. Vì `checkFirmware()` so **khớp chính xác**
`name != "fbt_" + FirmwareVer + ".bin"`, một bản tự xưng `"v2.4.5"` sẽ khớp đúng file cũ đó →
máy đọc *"You have the lasted version"* **vĩnh viễn, im lặng**, và OTA là đường duy nhất với
tới máy ngoài hiện trường.

⚠ **Chưa kiểm chứng được từ trong repo.** Tài liệu mà CLAUDE.md dẫn cho khẳng định đó —
`docs/history/2026-08-20-bao-cap-nhat-thanh-cong.md` — **không tồn tại** (một trong 10/52 link
chết đã đo hôm 2026-09-07). Vì vậy trạng thái server ở đây là **lời kể của CLAUDE.md**, không
phải phép đo. Cách đặt tên bên dưới chọn sao cho **đúng dù khẳng định đó đúng hay sai**.

**3. Hậu tố `AT`/`a` gỡ đúng cái va chạm đó.** `"v2.4.5AT"` tìm `fbt_v2.4.5AT.bin`,
`"v2.4.5a"` tìm `fbt_v2.4.5a.bin` — **không chuỗi nào khớp** `fbt_v2.4.5.bin`. Hai chuỗi này
chưa từng tới máy nào nên vẫn tới được. Hậu tố ở bản này vì thế **gánh hai việc**: phân biệt
trục quy tắc hình dạng (như từ v2.4.3AT) **và** né chuỗi đã cháy. Đã ghi thẳng vào comment
`define.h` — `#ifndef` cho phép override từ env, nên luật phải được **phát biểu**, không chỉ
được **hiện thực**.

## Phương án đã loại bỏ

- **`"v2.4.5"` trần** — lý do ở mục 2. Cấm vĩnh viễn, không phải cấm lần này.
- **Nhảy thẳng `v2.4.6`** (CLAUDE.md đang khuyến nghị). Khuyến nghị đó viết cho một chuỗi
  version **trần**; với trục hậu tố thì va chạm không xảy ra, nên bỏ qua một số hiệu chỉ làm
  dãy số nói dối về những gì đã phát hành. Giữ v2.4.5 và **ghi rõ vì sao hậu tố là bắt buộc**
  đúng hơn là đổi số để né một vấn đề đã hiểu.
- **Gỡ trục `AT`/`a`, quay về một chuỗi duy nhất** — sẽ ép chọn một trong hai quy tắc hình dạng
  ngay bây giờ, trong khi `docs/history/2026-08-21-asf-va-quy-tac-hinh-dang.md` còn ghi
  "KHÔNG cài bản này lên máy chạy ASF". Chưa tới lúc.

## Còn hở — nằm ở SERVER, không nằm ở firmware

Đây là điểm 2 sẵn có trong comment `define.h`, nay áp cho v2.4.5:

Máy chạy `v2.4.5AT` **poll mỗi 6 h**. Nếu server vẫn phục vụ `fbt_v2.4.5.bin` (bản cũ) cho
device đó thì tên **không khớp** → máy báo *có bản mới* → người vận hành bấm **ĐỎ** là
**cài đè bản cũ tiền-`?ver=`** lên bản đang thử. Đây không phải lỗi mới của lần bump này (máy
`v2.4.4AT` hôm nay cũng vậy), nhưng phải xử lý trước khi bản v2.4.5 nào ra khỏi bàn:

1. đặt trên server file tên **đúng** `fbt_v2.4.5AT.bin` (và `fbt_v2.4.5a.bin` nếu phát hành
   trục kia), **hoặc**
2. giữ máy thử **ngoài mạng** cho tới khi server sẵn sàng.

Chưa làm được thì đừng cắm máy thử v2.4.5 vào WiFi có internet.

## Kiểm

- `python tools/check.py` — 10 passed / 0 failed / 5 skipped (thiếu toolchain g++), y hệt
  baseline trước khi sửa. Không guard nào ghim chuỗi version, nên bump không thể làm đỏ cái gì
  — cũng có nghĩa **không có guard nào chặn được việc đặt lại `"v2.4.5"` trần**. Comment trong
  `define.h` hiện là lớp bảo vệ duy nhất; một guard cho việc này là việc còn nợ.
- `pio run -e esp32dev` — build sạch.
