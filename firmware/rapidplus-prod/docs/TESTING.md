# Test — FBT-RapidPlus

## Ba tầng, ba mục đích

| Tầng | Lệnh | Chạy ở | Dùng cho |
|---|---|---|---|
| **Native** | `pio test -e native` | host, **trong CI** | Logic an toàn, validate config, verify OTA, thuật toán |
| **On-device** | `pio test -e esp32dev_test -v` | ESP32 thật | FreeRTOS, timing, driver, phần không mô phỏng được |
| **Máy thật** | thủ công | phòng lab | Mọi thay đổi chạm nhiệt / OTA / thuật toán |

## Native — bắt buộc, không có ngoại lệ

```bash
pio test -e native                 # tất cả
pio test -e native -f test_safety  # chỉ nhóm an toàn
```

Chạy trong vài giây, không cần phần cứng, chạy trên **mọi commit** qua CI.

**Luật (AGENTS.md §8):** thêm một nhánh quyết định an toàn mà không có test
native tương ứng = **PR chưa xong**.

### Viết test cho lỗi

Khi sửa một bug an toàn, thứ tự bắt buộc:

1. Viết test tái hiện bug. **Chạy và thấy nó ĐỎ.**
2. Sửa code.
3. Test xanh.

Bỏ bước 1 thì không ai biết test đó có thật sự bắt được bug hay không.

### Những thứ dễ quên phải có test

Đây là các bẫy đã có test khoá lại trong [test/test_safety/](../test/test_safety/)
và [test/test_config/](../test/test_config/) — giữ nguyên, đừng xoá:

- **NaN** — mọi so sánh với NaN đều false, nên kiểm tra ngưỡng viết ẩu sẽ cho qua.
- **Tràn `millis()`** — sau ~49.7 ngày; máy chạy liên tục nhiều tuần là bình thường.
- **Giá trị đúng bằng ngưỡng** — khoá lại ranh giới `>` vs `>=`.
- **Chuỗi không null-terminate** — chính là finding D1-02.
- **`nullptr` trong manifest** — cloud có thể gửi trường thiếu.
- **Thứ tự kiểm tra** — sensor timeout phải thắng ngưỡng nhiệt; cert phải xét
  trước mọi trường khác của manifest.

## On-device

Dành cho thứ **không** mô phỏng được trên host: task FreeRTOS, semaphore, timing
thật, driver phần cứng.

```bash
pio test -e esp32dev_test -v
```

`test_build_src = no` → `src/` không được biên dịch, nên `setup()`/task của
firmware không chạy và không link driver. Chương trình test tự cung cấp
`setup()`/`loop()` riêng.

## Máy thật — checklist tối thiểu

Với **mọi** thay đổi chạm gia nhiệt:

- [ ] Rút sensor giữa lúc chạy → máy phải cắt gia nhiệt trong ≤ 2 s, log rõ lý do
- [ ] Ép target vượt hard-limit → máy phải cắt, không bám theo
- [ ] Đẩy config vượt ngưỡng từ cloud → clamp/từ chối, có log, không áp nguyên
- [ ] Chạy đủ một chu trình PCR hoàn chỉnh → kết quả khớp bản v2.4.2
- [ ] Ghi lại `ESP.getFreeHeap()` trước/sau
- [ ] Ghi kết quả vào [PROGRESS.md](PROGRESS.md)

Với mọi thay đổi chạm **OTA**:

- [ ] OTA thành công trên ≥ 1 máy thật
- [ ] OTA với ảnh sai sha256 → phải từ chối, máy vẫn chạy bản cũ
- [ ] OTA với `pcb_version` sai → phải từ chối
- [ ] Ngắt điện giữa lúc OTA → máy boot lại được bản cũ (rollback)

## Sai lầm phải tránh

| Sai lầm | Vì sao tệ |
|---|---|
| Sửa test cho xanh mà không sửa nguyên nhân | Xoá mất bằng chứng bug, giữ nguyên bug |
| "Sửa nhỏ, khỏi test" | Thay đổi nhỏ chạm an toàn vẫn là thay đổi an toàn |
| Chỉ test đường thành công | Bug an toàn nằm ở đường lỗi |
| Test cần cắm máy mới chạy được | Sẽ bị bỏ qua đúng lúc gấp — lúc dễ sai nhất |
