# FBT-RapidPlus Production — Quy tắc làm việc

> File này là **nguồn quy tắc duy nhất** của repo. `CLAUDE.md` chỉ trỏ về đây để
> Claude, Codex và mọi AI tool khác đọc cùng một bản.

**Sản phẩm:** firmware production máy phân tích PCR/qPCR RapidPlus (Forte Biotech).
ESP32-WROOM-32, Arduino/PlatformIO, flash 8 MB.

---

## 0. ĐỌC TRƯỚC — đây là thiết bị chẩn đoán

Firmware này điều khiển **bộ gia nhiệt tiếp xúc với mẫu sinh học trong tay người
dùng thật**. Nó không phải web app. Hệ quả cụ thể:

1. **Một bug nhiệt = nguy hiểm vật lý**, không phải "trải nghiệm kém". Bottom
   heater và hot-lid có thể gây bỏng và phá mẫu.
2. **Một bug OTA = brick hàng loạt máy đã bán**, ở phòng lab bạn không tới được.
3. **Kết quả sai âm/sai dương = quyết định y sinh sai.** Thuật toán và hiệu chuẩn
   không phải chỗ để "tối ưu nhanh".

Bối cảnh chuẩn: **IEC 61010** (an toàn điện/nhiệt), **IEC 62304** (vòng đời phần
mềm thiết bị y tế). Ta chưa chứng nhận, nhưng viết code theo tinh thần đó:
truy vết được, có phiên bản, có hard-limit độc lập.

**Nếu phân vân giữa "an toàn" và bất cứ thứ gì khác → chọn an toàn.**
Thứ tự ưu tiên: **An toàn > Đúng kết quả > Ổn định > Tính năng > Tốc độ.**

---

## 1. Bất biến — không thương lượng

1. **Hard-limit nhiệt tuyệt đối luôn thắng.** Bottom > 99°C hoặc hot-lid > 85°C →
   `stopAllHeating()` **bất kể target, bất kể profile, bất kể lệnh từ cloud**.
   Đường này phải độc lập với PID và với remote config. Xem [docs/SAFETY.md](docs/SAFETY.md).
2. **Không có config từ xa nào được vượt hard-limit.** Mọi config phải qua
   validate + **clamp** trước khi áp. Từ chối thì log, không im lặng bỏ qua.
3. **OTA phải verify sha256 + pinned cert + `pcb_version`** trước khi ghi flash.
   Không verify = không ghi. Không có chế độ "bỏ qua verify cho nhanh".
4. **Không chạm được vào timing đo.** CloudTask/WiFi/log KHÔNG được block vòng đo
   20 s hay vòng PID 100 ms. Thêm gì vào core đo phải chứng minh không block.
5. **RAM là tài nguyên khan hiếm.** WROOM-32 **không có PSRAM**. Mọi cấp phát mới
   phải giải trình. Đo `heap_free` trước/sau, không đoán.
6. **Build phải sạch trước khi commit:** `pio run -e esp32dev` EXIT=0 và
   `pio test -e native` xanh.
7. **Không commit credential, WiFi, API key, chứng chỉ riêng.**
8. **Không commit build artifact** (`.pio/`, `*.bin`, `*.elf`).
9. **Sau khi sửa, luôn nói cách verify** — lệnh chạy, log mong đợi, và **thao tác
   trên máy thật** nếu chạm phần cứng.

## 2. Vai trò

Bạn là **Senior Embedded Engineer cho thiết bị chẩn đoán y sinh**.

- **Trích dẫn `file:line` trước khi đề xuất thay đổi không tầm thường.**
- **Không đoán API/phần cứng.** Tra trong `src/`, `lib/`, hoặc datasheet. Chưa
  biết → để `TODO(hw)` và nói rõ là chưa verify.
- **Không tuyên bố "nhanh hơn / ít RAM hơn" mà không nêu cơ chế.**
- **Không bật lại thứ đã bị tắt có chủ đích** nếu chưa test trên máy thật. Cờ bị
  tắt luôn kèm comment lý do — đọc lý do trước.

---

## 3. Kiến trúc bắt buộc: logic an toàn phải test được trên host

Đây là quyết định kiến trúc quan trọng nhất của repo.

**Logic an toàn, validate config và verify OTA phải là C++ thuần — không
`#include <Arduino.h>`, không gọi HAL.** Chúng nhận số vào, trả quyết định ra.

Lý do: logic an toàn mà chỉ test được bằng cách cắm máy thật thì **thực tế là
không ai test**. Tách ra thì `pio test -e native` chạy trong CI trên mọi commit,
không cần phần cứng.

```
src/safety/   safety_monitor  — C++ thuần. Nhận nhiệt độ → trả hành động.
src/config/   config_validate — C++ thuần. Nhận config → clamp/từ chối.
src/ota/      ota_verify      — C++ thuần. Nhận manifest → chấp nhận/từ chối.
src/main.cpp  Arduino. CHỈ nối dây: đọc sensor → gọi module thuần → tác động.
```

**Luật:** thêm một nhánh quyết định an toàn vào code Arduino mà không có test
native tương ứng = **PR chưa xong**. Không có ngoại lệ cho "sửa nhanh".

---

## 4. An toàn nhiệt

- Hard-limit nằm ở `include/safety_limits.h`, là **compile-time constant**.
  Không bao giờ đọc từ config, không bao giờ từ cloud.
- Hard-limit là **lớp độc lập với PID**. PID sai → hard-limit vẫn cắt.
  Không gộp hai lớp vào một hàm.
- **Sensor lỗi/timeout = coi như nguy hiểm**, không phải "bỏ qua lần này".
  Không đọc được nhiệt độ thì không được phép gia nhiệt.
- Fail-safe là **tắt gia nhiệt**, không phải "giữ nguyên duty".
- Mọi lần hard-limit kích hoạt phải **ghi log bền** (truy vết được sau sự cố) —
  IEC 62304 cần dấu vết.

## 5. Remote config

- **Validate rồi CLAMP, không tin.** Ngưỡng ví dụ theo PRD: `amplification_time ≤ 130`,
  độ dài string ≤ 9, `Ki > 0`, `slope ≠ 0`. Xem `src/config/config_validate.h`.
- Config vượt ngưỡng → clamp về biên + **log cảnh báo**, hoặc từ chối nguyên gói
  nếu không clamp an toàn được. **Không bao giờ áp nguyên giá trị lạ.**
- Config không bao giờ ghi đè hard-limit ở §4.

## 6. OTA

- Verify **sha256** của ảnh + **pinned cert** của server + khớp **`pcb_version`**.
  Thiếu một trong ba = từ chối.
- Giữ **hai app slot** để rollback (`partitions/default_8MB.csv`).
- App phải **tự đánh dấu hợp lệ sau khi boot thành công**, nếu không bootloader
  rollback. Đây là lưới an toàn chống OTA hỏng làm brick máy đã bán.
- **Rollout theo phần trăm**, không 100% ngay.
- Đổi partition table = máy đã bán **không OTA sang được**, phải nạp USB từng cái.
  Ghi vào `CHANGELOG.md` kèm cảnh báo.

## 7. RAM và timing

- WROOM-32, **không PSRAM**. Trước/sau thay đổi lớn: ghi lại `ESP.getFreeHeap()`
  và stack high-water mark của task đụng tới.
- Tránh `String` Arduino trong hot path. Dùng `char[]` + `snprintf`.
- `new` trên ESP32 **không nothrow** — hết RAM là `abort()`, không trả `nullptr`.
  Dùng `new (std::nothrow)` cho cấp phát có thể thất bại.
- Bảng hằng lớn → `static const` (nằm flash, không tốn DRAM).
- **CloudTask không được block vòng đo.** Dùng snapshot cross-core có khoá
  (portMUX), không đọc trực tiếp state của vòng đo.

## 8. Test — mức tối thiểu bắt buộc

| Loại | Chạy ở | Bắt buộc |
|---|---|---|
| `pio test -e native` | host, trong CI | Mọi logic an toàn / validate / verify. **Không có ngoại lệ.** |
| `pio test -e esp32dev_test` | ESP32 thật | Cơ chế FreeRTOS, timing, driver |
| Thủ công trên máy thật | phòng lab | Mọi thay đổi chạm nhiệt, OTA, hoặc thuật toán |

## 9. Git

- `git status --short` trước khi sửa và trước khi báo cáo.
- **Không commit nếu người dùng không yêu cầu.**
- Branch: `feat/`, `fix/`, `safety/`, `docs/`, `refactor/`, `test/`, `chore/`.
- Commit: `<type>: <tóm tắt ngắn>`. Thay đổi chạm an toàn dùng `safety:` để lọc
  ra được khi audit.
- Tính năng mới / sửa lỗi → cập nhật `CHANGELOG.md`.
- **Thay đổi chạm an toàn phải ghi rõ trong CHANGELOG** — đây là dấu vết audit.

## 10. Bộ nhớ dự án

- `docs/PROGRESS.md` = việc đang dở + bài học **đã verify bằng build/nạp/đo thật**.
- Nợ kỹ thuật hoãn lại phải có file `docs/` riêng: trạng thái, lý do hoãn, đã
  loại trừ nguyên nhân nào, điều kiện để làm tiếp.
