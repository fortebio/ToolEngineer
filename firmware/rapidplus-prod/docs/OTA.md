# OTA — FBT-RapidPlus

> Nguồn: PRD FBT-DXD FR-DEV-05 (đóng finding **D5-01**).
> Logic quyết định: [`src/ota/ota_verify.h`](../src/ota/ota_verify.h).

## Vì sao khắt khe

OTA hỏng = **brick hàng loạt máy đã bán**, nằm ở phòng lab bạn không tới được.
Khác với server (rollback trong 30 giây), đây là cử người bay tới từng nơi cắm USB.

Nên: **thà từ chối OTA nhầm còn hơn ghi nhầm một byte.**

## Ba điều kiện — thiếu một là từ chối

| Điều kiện | Vì sao | Mã từ chối |
|---|---|---|
| **Pinned cert** | Kênh không xác thực thì mọi trường trong manifest đều có thể do kẻ tấn công viết, kể cả hash "hợp lệ" | `reject_no_pinned_cert` |
| **sha256** | Ảnh hỏng giữa đường = brick | `reject_bad_hash` |
| **pcb_version khớp** | Sai PCB = sai pin map = **hỏng phần cứng** | `reject_pcb_mismatch` |

Thứ tự kiểm tra **có chủ đích**: cert xét **đầu tiên**. Kiểm hash trước khi biết
kênh có đáng tin không là kiểm một con số do kẻ tấn công cung cấp.

Cộng thêm: kích thước phải nằm trong `[kMinImageBytes, kMaxImageBytes]` — ảnh lớn
hơn app slot sẽ **ghi tràn sang partition kế bên**, phải chặn trước byte đầu tiên.

## Hai pha verify

`ota::evaluate()` chỉ là **pha 1** — kiểm *định dạng* và *chính sách*, trước khi
tải. Nội dung ảnh chưa tồn tại lúc đó.

```
pha 1  evaluate(manifest)     → có được phép TẢI không
pha 2  tính sha256 ảnh đã tải → có được phép GHI không
```

**Đừng bỏ pha 2.** Pha 1 chỉ xác nhận chuỗi hash đúng 64 ký tự hex, không xác
nhận ảnh khớp hash.

## Rollback

- Hai app slot trong [`partitions/default_8MB.csv`](../partitions/default_8MB.csv).
- App **phải tự đánh dấu hợp lệ** sau khi boot thành công:
  `esp_ota_mark_app_valid_cancel_rollback()`. Không gọi → bootloader rollback.
- **Đánh dấu sau khi đã chứng minh boot tốt**, không phải ngay dòng đầu `setup()`.
  Gọi quá sớm là vô hiệu hoá chính lưới an toàn này.

## Rollout

- **Không bao giờ 100% ngay.** Bắt đầu nhỏ (5–10%), chờ máy đợt đầu báo về, rồi mở rộng.
- Rollout theo **PCB version**, không trộn.
- Có đường tạm dừng rollout giữa chừng.

## Đổi partition table

⚠️ Máy đã bán **không OTA sang được** — phải nạp USB từng cái ở chỗ khách.

Nếu buộc phải đổi:
1. Ghi `CHANGELOG.md` mục **Changed** kèm cảnh báo rõ.
2. Ghi rõ máy nào cần nạp tay.
3. Cân nhắc phát hành hai nhánh song song trong giai đoạn chuyển tiếp.

## Checklist trước khi bật OTA fleet

Theo PRD §P1.5 — **gate, không thương lượng**:

- [ ] D6-01 hard-limit nhiệt ([SAFETY.md](SAFETY.md))
- [ ] D5-01 OTA verify sha256 + cert
- [ ] D1-01/02 validate config
- [ ] D2-01 tách log khỏi SerialBT
- [ ] D5-02 WiFi reconnect + backoff
- [ ] Đã test rollback thật: ngắt điện giữa lúc OTA → máy boot lại bản cũ
