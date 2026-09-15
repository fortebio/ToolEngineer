# An toàn — FBT-RapidPlus

> **Đọc trước khi chạm bất cứ thứ gì liên quan gia nhiệt.**
> Nguồn yêu cầu: PRD FBT-DXD §FR-DEV-04 (đóng finding **D6-01**), NFR-06.

## Vì sao có file này

Firmware điều khiển bộ gia nhiệt tiếp xúc mẫu sinh học, đặt trong phòng lab của
khách. Chuỗi sự cố thực tế đáng sợ nhất không phải "code crash", mà là:

> config từ xa đặt target sai → PID bám theo trung thực → không có lớp nào cắt →
> máy nóng vượt ngưỡng vật lý.

Toàn bộ thiết kế dưới đây tồn tại để chuỗi đó **không thể xảy ra**, kể cả khi
PID sai, config sai, cloud bị chiếm quyền, hay firmware có bug.

## Mô hình phòng vệ nhiều lớp

```
Lớp 4  Cloud gửi config          ← không tin
Lớp 3  validateAndClamp()        ← clamp/từ chối trước khi áp
Lớp 2  PID bám target            ← có thể sai, chấp nhận
Lớp 1  safety::evaluate()        ← HARD-LIMIT, độc lập, luôn thắng
Lớp 0  Bảo vệ phần cứng          ← cầu chì nhiệt / thermal cutoff (TODO(hw))
```

**Lớp 1 không biết gì về lớp 2, 3, 4.** Nó chỉ biết nhiệt độ đo được và hằng số
compile-time. Đó là điều làm nó đáng tin.

## Hard-limit tuyệt đối

Định nghĩa tại [`include/safety_limits.h`](../include/safety_limits.h).

| Ngưỡng | Giá trị | Nguồn |
|---|---|---|
| Bottom heater | > 99.0 °C → cắt | PRD FR-DEV-04 |
| Hot-lid | > 85.0 °C → cắt | PRD FR-DEV-04 |
| Sensor hợp lệ | ngoài −40…200 °C → cắt | thiết kế |
| Sensor timeout | > 2000 ms không có số mới → cắt | thiết kế (20 chu kỳ PID) |

> ⚠️ **TODO(safety):** PRD Q3 ghi ngưỡng chính xác *"cần team nhiệt/sinh học chốt"*.
> Xác nhận bằng văn bản trước khi phát hành production.

### Bất biến

1. **Compile-time constant.** Không đọc từ config, không nhận từ cloud, không sửa
   lúc chạy. Đổi số = đổi firmware = qua quy trình phát hành.
2. **Độc lập với PID.** Không gộp hai lớp vào một hàm. PID sai thì hard-limit vẫn cắt.
3. **Không có tham số target.** `safety::evaluate()` cố ý không nhận target —
   nó không quan tâm profile đang chạy muốn gì.
4. **Không có đường vòng.** Không có cờ debug, không có "chế độ kỹ thuật viên",
   không có `#ifdef` nào tắt được lớp này.

## Quy tắc fail-safe

| Tình huống | Xử lý ĐÚNG | Xử lý SAI (đừng làm) |
|---|---|---|
| Sensor timeout | cắt gia nhiệt | dùng lại số đọc cũ |
| Sensor trả NaN | cắt gia nhiệt | bỏ qua mẫu này |
| Sensor ngoài dải | cắt gia nhiệt | clamp về dải hợp lệ |
| Nghi ngờ | cắt gia nhiệt | chạy tiếp rồi xem sao |

**Fail-safe là TẮT, không phải "giữ nguyên duty".** Giữ nguyên duty khi không đo
được nhiệt độ chính là kịch bản chạy trốn nhiệt.

### Bẫy NaN

Mọi phép so sánh với NaN đều cho `false`. Nên đoạn này **cho qua NaN**:

```cpp
if (t > kBottomHardLimitC) { stop(); }   // NaN → false → KHÔNG cắt
```

Vì vậy `isReadingSane()` kiểm tra **thuộc dải hợp lệ** (`>= min && <= max`) chứ
không kiểm tra "vượt ngưỡng". Có test khoá lại hành vi này:
`test_nan_thi_cat` trong [test/test_safety/](../test/test_safety/).

### Bẫy tràn millis()

`millis()` tràn sau ~49.7 ngày. Máy PCR chạy liên tục nhiều tuần là bình thường,
nên đây **không phải trường hợp lý thuyết**. Luôn so sánh bằng hiệu **unsigned**:

```cpp
if (nowMs - lastMs > timeout)      // ĐÚNG, đúng cả khi tràn
if (nowMs > lastMs + timeout)      // SAI, hỏng khi tràn
```

Test khoá lại: `test_millis_tran_khong_gay_cat_gia`.

## Remote config

Xem [`src/config/config_validate.h`](../src/config/config_validate.h).
Nguồn: PRD FR-DEV-03 (đóng D1-01, D1-02, D4-01, D4-02).

Quy tắc: **clamp được thì clamp, không clamp an toàn được thì từ chối cả gói.**

| Trường | Ngưỡng | Xử lý | Vì sao |
|---|---|---|---|
| `amplification_time` | 1…130 | clamp | Ngoài dải vẫn chạy được; hard-limit là lưới phía sau. |
| `ki` | > 0, ≤ 100 | clamp | PID vẫn hội tụ với Ki nhỏ. |
| `slope` | ≠ 0, hữu hạn | **TỪ CHỐI** | Là mẫu số trong thuật toán. Clamp sẽ cho kết quả đo *trông* hợp lệ mà vô nghĩa — **sai âm thầm nguy hiểm hơn từ chối chạy**. |
| `label` | ≤ 9 ký tự | cắt | Buffer tràn — chính là D1-02. |

Mọi lần clamp/từ chối **phải log** kèm cờ trường nào bị đụng. "Config sai" không
đủ để chẩn đoán.

## Điều kiện tiên quyết trước khi mở remote config/OTA fleet

Theo PRD §P1.5, **không thương lượng**:

- [ ] **D6-01** — hard-limit nhiệt tuyệt đối
- [ ] **D5-01** — OTA verify sha256 + cert
- [ ] **D1-01 / D1-02** — validate config
- [ ] **D2-01** — tách log khỏi SerialBT sau khi release BT
- [ ] **D5-02** — WiFi reconnect + backoff

## Khi sửa code chạm an toàn

1. Sửa **module thuần** trong `src/safety/` hoặc `src/config/`, không sửa `main.cpp`.
2. **Thêm test native** tái hiện tình huống. Test phải ĐỎ trước khi sửa.
3. `pio test -e native` xanh.
4. Test trên **máy thật**: tạo tình huống (rút sensor, ép target cao) và xác nhận
   máy cắt gia nhiệt.
5. Commit prefix `safety:`, ghi `CHANGELOG.md` mục Security/Safety.
6. Ghi lại kết quả đo vào [PROGRESS.md](PROGRESS.md).

**Cấm:** sửa test cho xanh mà không sửa nguyên nhân.
