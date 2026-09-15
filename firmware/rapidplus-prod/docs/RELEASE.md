# Phát hành — FBT-RapidPlus

> Thiết bị chẩn đoán: mỗi bản phát hành phải **truy vết được** (IEC 62304).

## Bất biến

1. **Tag `vX.Y.Z` phải khớp `FBT_FW_VERSION`** trong [`include/version.h`](../include/version.h).
   CI chặn nếu lệch — cloud đọc version từ binary làm nguồn canonical, lệch nghĩa
   là bản phát hành tự nhận sai phiên bản và mọi thứ dựa trên version (rollout,
   rollback, hỗ trợ khách) sai theo.
2. **Không phát hành khi test đỏ.** Không có "test này đỏ nhưng không liên quan".
3. **Thay đổi chạm an toàn phải test trên máy thật** trước khi phát hành.
4. **Rollout theo phần trăm**, không 100% ngay.

## Checklist

### Trước khi tag

- [ ] `pio run -e esp32dev` EXIT=0
- [ ] `pio test -e native` xanh **toàn bộ**
- [ ] `pio test -e esp32dev_test` xanh (nếu chạm FreeRTOS/timing)
- [ ] `FBT_FW_VERSION` đã tăng và khớp tag sắp gắn
- [ ] `FBT_PCB_VERSION` đúng với PCB đích
- [ ] `CHANGELOG.md` đã cập nhật, mô tả theo góc nhìn người dùng
- [ ] Thay đổi an toàn (nếu có) đã ghi mục **Security/Safety** trong CHANGELOG
- [ ] Đã ghi `ESP.getFreeHeap()` trước/sau vào [PROGRESS.md](PROGRESS.md)
- [ ] Không có credential/cert nào lọt vào commit

### Test máy thật (bắt buộc nếu chạm nhiệt / OTA / thuật toán)

Xem checklist đầy đủ ở [TESTING.md](TESTING.md).

- [ ] Rút sensor → cắt gia nhiệt ≤ 2 s
- [ ] Ép target vượt hard-limit → cắt
- [ ] Một chu trình PCR hoàn chỉnh, kết quả khớp bản trước
- [ ] OTA thành công trên ≥ 1 máy thật
- [ ] Ngắt điện giữa OTA → rollback về bản cũ được

### Phát hành

- [ ] Gắn tag `vX.Y.Z` → CI build + verify version
- [ ] Rollout **5–10%** trước
- [ ] Theo dõi máy đợt đầu báo về
- [ ] Mở rộng dần
- [ ] Có đường tạm dừng rollout

### Nếu đổi partition table

- [ ] CHANGELOG ghi cảnh báo: máy đã bán **không OTA sang được**
- [ ] Danh sách máy cần nạp USB tay
- [ ] Kế hoạch chuyển tiếp

## Đánh số phiên bản

`MAJOR.MINOR.PATCH`

| Tăng | Khi |
|---|---|
| MAJOR | Phá tương thích: protocol cloud, partition, định dạng dữ liệu |
| MINOR | Tính năng mới, tương thích ngược |
| PATCH | Sửa lỗi |

Thay đổi **chạm an toàn** luôn ít nhất là PATCH và **luôn** phải ghi CHANGELOG,
kể cả khi "chỉ đổi một hằng số".

## Sau sự cố

Nếu một bản phát hành gây sự cố ở hiện trường:

1. Tạm dừng rollout **ngay**.
2. Ghi lại: bản nào, bao nhiêu máy, triệu chứng gì.
3. Truy nguyên gốc rễ — **không đoán**.
4. Thêm test native tái hiện. Test phải đỏ trước khi sửa.
5. Ghi vào [PROGRESS.md](PROGRESS.md) mục *Bài học đã verify* — đây là kiến thức
   không tái tạo được từ git log.
