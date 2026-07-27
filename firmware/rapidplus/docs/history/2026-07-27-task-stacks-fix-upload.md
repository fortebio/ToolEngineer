# 2026-07-27 — Upload chạy được: cắt stack task thừa (không phải lỗi mạng, không phải TLS)

## Kết quả

```
[up] GAS    POST OK in 8034 ms, code=302 (try 1)
[up] ingest POST OK in 5096 ms, code=200 (try 1)  {"ok":true,"db":true,"id":20996}
[up] ERP    POST OK in 2657 ms, code=200 (try 1)  {"status":"ok","result_id":"19be622f-..."}
```

Cả ba đích, **ngay lần thử đầu**, sau khi cắt 23 552 B stack. Không đụng gì tới mạng, TLS,
thứ tự dựng payload hay dashboard.

## Nguyên nhân

6 task được cấp **65 536 B** stack, chưa ai đo bao giờ. Đo bằng
`uxTaskGetStackHighWaterMark()` (đỉnh thật, tính bằng **byte** trên ESP32) lúc rảnh:

```
Control=1124/8192  Sensor=1476/16384  Display=1396/16384
Network=2292/8192  Input=532/8192     Setting=648/8192   -> unused = 58 068 B
```

**58/64 KB chưa bao giờ được dùng.** Trong khi upload chết vì thiếu ~20 KB.

## Đã cắt

| Task | Cũ | Mới | Lý do |
| --- | --- | --- | --- |
| Control | 8192 | 4096 | đỉnh 1112 |
| Sensor | 16384 | 10240 | đỉnh 1464, chừa cho thuật toán phát hiện |
| Display | 16384 | **10240** | **xem cảnh báo dưới** |
| Network | 8192 | 6144 | đỉnh 2280 (OTA) |
| Input | 8192 | 3072 | đỉnh 1480 |
| Setting | 8192 | **8192 giữ nguyên** | OTA check chạy TLS trên stack này |

Đo trước/sau, lúc rảnh 0 client:

| | free | intLargest |
| --- | --- | --- |
| trước | 79 792 | 69 620 |
| sau | **103 348** | **86 004** |

**+23 556 B free, +16 384 B liền mạch.** Lúc upload: 49 140 → **69 620**, trong khi handshake
cần 32 768–36 352. Dư hơn gấp đôi.

## CẢNH BÁO: đừng hạ DisplayTask dưới 10240

Đỉnh của DisplayTask lúc rảnh là **1396**, nhưng **trong lúc upload là 7848**:

```
[stack] ... Display=7848/10240 ...   <- đo ngay khi đang POST
```

Vì **mbedTLS bắt tay trên stack của task gọi nó**, mà `postData_GoogleSheet` chạy từ
DisplayTask (`screen_Result`). Kế hoạch ban đầu là cắt xuống 8192 — như vậy chỉ còn **344 B**
dự phòng, gần như chắc chắn tràn stack giữa lúc upload. Cùng lý do, **Setting giữ 8192** vì
`checkFirmware()` cũng chạy TLS ở đó (đỉnh đo lúc rảnh 648 là *không* đại diện).

**Bài học chung: đo stack lúc rảnh là vô nghĩa với task nào có TLS chạy trên nó.** Phải đo
trong lúc upload.

## Những gì KHÔNG phải nguyên nhân (đã loại trừ bằng đo đạc)

- **Mạng/DNS/endpoint**: POST thử từ PC bằng đúng token của máy → GAS 302, ingest 200
  (`{"ok":true}`), ERP tới được ứng dụng. Cả ba server đều nhận dữ liệu bình thường.
  Cloudflare **không** chặn ESP32 (403/1010 lần trước chỉ xảy ra với GET).
- **`String` nhân đôi payload**: đo được payload chiếm 8 656 B cho chuỗi 8 626 B, có hay không
  `reserve` đều thế. ESP32 String không nhân đôi.
- **Thứ tự TLS/payload**: thử cả ba hoán vị. Payload-trước làm handshake chết; TLS-trước làm
  JSON cụt (GAS 411, ERP `Invalid JSON`); bản lai làm `write()` treo 60 s (`code=-3`). Không
  hoán vị nào tạo thêm RAM — chỉ dời chỗ chết.
- **mDNS, client SSE**: đo ra 0 B ảnh hưởng tới khối liền mạch (client *đầu tiên* tốn
  14 336 B, các client sau gần như miễn phí).

## Còn lại

- **`AsyncTCP begin(): bind error: -8`** vẫn xuất hiện sau upload. Dashboard vẫn phục vụ được
  (`clients=1`), nhiều khả năng vì listen pcb cũ chưa đóng nên `begin()` mới bind trượt. Chưa
  sửa.
- **ERP trả `"device_matched":false`** — bản ghi được nhận nhưng ERP không khớp thiết bị. Cần
  đăng ký `id_device` phía ERP, không phải việc của firmware.
- Dòng `[stack]` in mỗi 10 giây trong `loop()` là **tạm** (`ponytail:` comment). Giữ tới khi
  chạy trọn một run 40 phút để xác nhận SensorTask không vọt, rồi xoá.
