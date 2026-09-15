# 2026-07-27 — Nút đỏ trên web không start được lysis: `phase` gánh hai nghĩa

## Triệu chứng

Điều khiển lysis từ web: bấm nút đỏ ở màn "Insert lysis tube" thì **nút sáng lên** nhưng máy
đứng nguyên, không vào 10 phút lysis. Nút vật lý thì chạy.

## Chẩn đoán (đo, không đoán)

Bấm thẳng bằng HTTP vào máy thật:

```
before:  Insert lysis tube | red = "Start lysis"
POST /control?btn=red  ->  {"ok":true,"btn":"red"}
after :  Lysis heating | busy True
```

**Firmware hoàn toàn đúng.** Nghi phạm ban đầu — `handleShortPress_Red()` trả về sớm vì
`ErrorStatus()` — **sai**. Lỗi nằm ở client.

## Gốc rễ

[data/script.js:67](../../data/script.js):

```js
if (btn === "red" && curPhase === "idle") btn = "ampname";
```

Ở màn chính, nút đỏ trên web nghĩa là "Amplification" nên phải mở cửa đặt tên thay vì nhấn RED
thật. Nhưng điều kiện chỉ xét `phase === "idle"` — mà **`ewaitLysisTube` cũng mang
`phase = "idle"`**. Kết quả: ở màn chờ ống lysis, nút đỏ vẫn sáng, vẫn gửi request, nhưng gửi
**`btn=ampname`** — một lệnh chẳng liên quan gì tới lysis.

| `type_infor` | phase (cũ) | RED thật sự làm gì |
| --- | --- | --- |
| `escreenStart` | idle | Amplification → cần cửa đặt tên |
| `ewaitLysisTube` | **idle** | **Start lysis** → phải gửi `red` |

Hai màn hình mà nút đỏ làm hai việc trái ngược, lại đeo chung một nhãn `phase`.

## Đã sửa

`webDashboard.cpp` `fillStatus()`: `case ewaitLysisTube` → **`phase = "waitlysis"`** (title giữ
nguyên "Insert lysis tube").

**Client không phải sửa dòng nào**: điều kiện `curPhase === "idle"` không còn khớp, nên nút đỏ
gửi đúng `btn=red`.

Chọn tách `phase` thay vì vá điều kiện ở client vì vá client chỉ bịt đúng một chỗ; tách phase thì
mọi nơi khác lỡ suy đoán từ `phase` cũng được sửa theo. Đã soát các chỗ client đọc `phase`:

- `script.js:172` (`phase === "idle"` → reset `confirmed`): vẫn đúng, reset xảy ra ở
  `escreenStart` đứng trước cả luồng lysis.
- `script.js:606` (gate `reviewStoredRun`) và `915` (`waitname`): không liên quan.
- `tools/sse_test_server.py`: không mô hình hoá pha này, không phải cập nhật.

Chỉ đổi firmware — **không cần `uploadfs`**.

## Kèm theo: InputTask 3072 → 6144

Đợt cắt stack sáng nay hạ InputTask xuống 3072 dựa trên đỉnh **520 B** đo lúc rảnh. Sau khi có
thao tác nút, đỉnh lên **1480 B** (48%). Các handler nút gọi thẳng vào display ở vài nhánh, nên
biên đó quá mỏng cho một thiết bị y tế. Nâng lại 6144: vẫn giữ **18 432 B** trong tổng 23 552 B
đã lấy về, dư gấp đôi cho TLS.

**Bài học lặp lại lần hai trong ngày**: đỉnh stack đo lúc rảnh không đại diện. Với DisplayTask thì
mbedTLS đẩy nó từ 1396 lên 7848 khi upload; với InputTask thì thao tác nút đẩy 520 lên 1480. Đo
trong lúc task đang làm việc nặng nhất của nó, không phải lúc nhàn.
