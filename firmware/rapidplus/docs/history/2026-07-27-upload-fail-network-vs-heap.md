# 2026-07-27 — Upload fail cả 3 đích: mạng là thủ phạm, nhưng heap đang sát mép

## Triệu chứng

Cuối một run, cả **GAS + ingest + ERP** đều fail hết 4/4 lần thử. Log có `-32512`-họ hàng nên
nghi RAM. Kết luận: **lần fail này là do mạng**, nhưng đo ra một vấn đề heap thật nằm bên dưới.

## Phần 1 — Mạng (nguyên nhân trực tiếp)

Mọi dòng lỗi đều là lỗi mạng, không phải cấp phát:

| Log | Nghĩa |
| --- | --- |
| `DNS Failed for ...` ×3 | không phân giải được tên |
| `errno: 118` ×5 | `EHOSTUNREACH` — không có route |
| `(-76)` = `-0x004C` | `MBEDTLS_ERR_NET_RECV_FAILED` |
| `(-78)` = `-0x004E` | `MBEDTLS_ERR_NET_SEND_FAILED` |

**`code=-1 (connection refused)` là nhãn gây hiểu lầm** — `HTTPClient` gộp *mọi* lỗi connect
(DNS fail, host unreachable, TLS lỗi) vào `-1`.

Bằng chứng quyết định: **`intLargest = 42996` đứng yên tuyệt đối cả 12 lần thử**. Thiếu RAM thì
con số này phải tụt.

Chập chờn chứ không chết hẳn: GAS lần 1 chạy **8 752 ms** rồi mới đứt (đã cắm được TCP), ba lần
sau fail trong **9-13 ms** (mất route). `fbt.basa-luma.ts.net` fail DNS lần 1 nhưng lần 2 resolve
được và chạy tới 11 904 ms. Kiểm lại từ PC cùng LAN (DNS `192.168.1.1`, đúng resolver của máy):

```
api.fortebio.tech      A: 104.21.54.92, 172.67.168.107   tcp443 OK
fbt.basa-luma.ts.net   A: 103.84.155.153, 103.84.155.217 tcp443 OK
script.google.com      A: 142.250.197.238                tcp443 OK
```

Cả ba đều có bản ghi **A (IPv4)** — quan trọng vì `hostByName()` của Arduino chỉ tra IPv4. Không
có lỗi cấu hình nào; sự cố là **uplink/DNS của LAN mất tạm thời**. Dữ liệu run vẫn nằm trong
EEPROM → bấm **"Up Data"** gửi lại được.

## Phần 2 — Heap: 63 476 lúc boot, 42 996 sau ~55 phút

Đo trên máy thật (board `rpl03018`, STA), thêm `dashHeapProbe()` tạm vào các mốc boot:

| Mốc | `free` | `intLargest` | Δ liền mạch |
| --- | --- | --- | --- |
| trước nhả BT | 176 828 | 110 580 | — |
| sau nhả BT | 225 372 | 110 580 | **0** |
| sau WiFi connect | 178 084 | 110 580 | **0** |
| cuối `setup()` (6 task) | 107 596 | 77 812 | **−32 768** |
| `dashboardBegin` vào | 107 752 | 77 812 | 0 |
| sau `LittleFS.begin` | 105 820 | 77 812 | 0 |
| sau routes + serveStatic | 103 120 | 77 812 | 0 |
| sau `dashServer.begin()` | 85 760 | 63 476 | **−14 336** |
| sau `MDNS.begin` | 79 688 | 63 476 | **0** |

**Boot sạch, 0 client: `intLargest = 63 476`.** Nhưng log của run hỏng và phép đo lúc đó (uptime
~55 phút, sau một run đầy + 12 handshake hỏng) cho **42 996** — *mất ~20 KB liền mạch trong lúc
chạy*. Ngưỡng mbedTLS đo được là ~42 KB → lúc upload chỉ còn dư **996 byte**. Đó là lời giải cho
`(-16) BIGNUM - Memory allocation failed` ở hai lần thử ERP cuối: cổng heap đo **trước khi** dựng
`WiFiClientSecure`, handshake sau đó ăn ~16 KB + 16 KB **ra từ chính khối đó**, nên tới phần MPI
thì không còn chỗ.

### Ai vô can (đã đo, đừng nghi lại)

- **mDNS**: tốn 6 072 B free nhưng **0 B liền mạch**. Không phải thủ phạm dù được thêm sau lần đo
  69 620 của 21/07.
- **Client SSE**: cho client vào rồi ra (`clients` 1→2→1), `intLargest` **đứng nguyên 63 476**.
  Khớp với đo cũ (8 client không đổi `intLargest`).
- **Nhả BT sớm**: vẫn đúng vị trí ([main.cpp:314](../../src/main.cpp)), và đo cho thấy nó trả về
  48 544 B free. Nó không **tăng** `intLargest` vì khối lớn nhất lúc đó đã là 110 580 — công của
  nó là để allocator xếp WiFi/AsyncTCP **quanh** vùng đầy, đúng như GOTCHA 1 mô tả.

### Ai lấy, ở boot

- **Stack của 6 task**: −32 768 B liền mạch. Không tránh được.
- **`dashServer.begin()`**: −14 336 B liền mạch (task + buffer của AsyncTCP).

### CHỐT (đo trực tiếp, cùng ngày): người xem dashboard ăn 14 336 B **vĩnh viễn**

| Trạng thái | `intLargest` |
| --- | --- |
| boot sạch, chưa ai mở dashboard (`clients=0`) | **69 620** |
| sau khi có người mở dashboard | **55 284** |
| **sau khi đóng hết tab** | **55 284 — KHÔNG hồi lại** |

Mở dashboard **lần đầu** làm mất **14 336 B** vùng liền mạch và **đóng tab không trả lại**. Vùng
liền mạch trên board này **chỉ đi xuống**, không bao giờ đi lên, cho tới khi reboot. Ghép với
nhu cầu TLS **44 032 B** (đo: 63 476 → 19 444 trong lúc bắt tay):

- boot sạch 69 620 → upload **chạy**
- có người xem 55 284 → vẫn **chạy**
- thêm churn trong một run 40 phút → 42 996 → **< 44 032 → chết**, và vì heap không hồi nên
  **mọi lần thử lại cũng chết** ⇒ đúng triệu chứng "toàn fail".

Đo cũ "8 client không đổi `intLargest`" **không sai nhưng gây hiểu lầm**: cái đắt là client
**đầu tiên**, client thứ 2..8 gần như miễn phí.

### Đã sửa: cổng gác TLS đặt dưới nhu cầu thật

`TLS_MIN` từng là **33 KB**, rồi **45 KB** — **cả hai đều dưới 49 140**, đúng dải máy rơi vào sau
một run; handshake vẫn chết bằng `-0x0010`/`-0x004C`/`-0x004E` và **4 lần thử × 3 đích lại càng
làm vụn heap**. Nay `TLS_MIN = 56 * 1024`: **trên** mọi mức đã thấy chết (49 140), **dưới** mức đã
thấy chạy (63 476). Đồng thời **bỏ hẳn vòng chờ 2,5 giây** — log lần hỏng ghi `intLargest = 49 140`
ở **cả 12 lần thử, giống nhau từng byte**, chứng minh vòng chờ đó chưa bao giờ hồi được gì, chỉ
thêm 7,5 giây màn hình tối trước khi chết y như cũ. Retry **giữ lại cho lỗi mạng**, nhưng lỗi bộ
nhớ thì từ chối ngay lần đầu:

```
[up] GAS SKIPPED: need 46080 B contiguous, only 42996 free -> reboot to upload
```

Một dòng nói đúng bản chất, thay cho một tá dòng `DNS Failed` / `Host is unreachable` trông như
lỗi mạng.

### Số đã khớp: payload KHÔNG to, nó nằm SAI CHỖ

Test `test_endrun_upload` đo trên board: payload chiếm **8 656 B** (chuỗi 8 626 B), **có hay
không `reserve` đều thế** — `String` của ESP32 **không** nhân đôi, giả thuyết "16KB do doubling"
**sai**. Nhưng cộng lại thì khớp tuyệt đối:

```
65 524  (khối liền mạch lúc rảnh)
- 8 656 (payload)
- 7 728 (mảnh vụn còn lại phía sau payload)
= 49 140 (khối lớn nhất trong lúc upload)      và 8 656 + 7 728 = 16 384
```

Tức payload **rơi vào GIỮA** khối lớn và **chẻ đôi** nó: một mảnh 49 140 và một mảnh 7 728.
Vấn đề là **vị trí**, không phải kích thước — nên `reserve` không cứu được, và mọi cách làm
payload nhỏ đi cũng chỉ dời được vài KB.

### Vì sao "upload ngay sau boot" là lời giải chắc chắn

Đo lúc boot (probe trong `setup()`): sau `connectSavedNetworks()`, **`intLargest = 110 580`** —
gấp hơn hai lần nhu cầu TLS, và lúc đó **chưa có** AsyncWebServer (−14 336) lẫn người xem
(−14 336). Dù payload có chẻ khối đó thì phần còn lại vẫn ~93KB, thừa xa mốc 63 476 đã chạy
được. Dữ liệu run đã nằm sẵn trong EEPROM nên upload sau reboot **không mất gì**.

### Giả thuyết còn lại

`dashboardSuspend()` gọi `dashServer.end()` + `started = false`; mỗi lần resume, `dashboardBegin()`
chạy lại **cả `dashServer.begin()`**. Nếu `end()` không trả lại đúng 14 336 B mà `begin()` đã lấy,
thì **mỗi chu kỳ upload bào mất một miếng** — và `63 476 − 14 336 ≈ 49 KB`, rồi tiếp tục xuống
42 996 sau vài chu kỳ. Khớp số.

**Cách đo (probe đã nằm sẵn trong firmware trên máy)**: mở serial, bấm **"Up Data"**, đọc ba dòng
`[heap] dashboardBegin enter / after dashServer.begin / after MDNS.begin` in ra lúc resume. Nếu
`intLargest` sau resume **thấp hơn** trước khi suspend → xác nhận, và hướng sửa là đừng
`end()/begin()` server mỗi lần upload (chỉ suspend logic, giữ server sống), hoặc chấp nhận chỉ
`end()` khi thật cần.

Dòng `[dash] heap ... intLargest=` in sẵn mỗi 10 giây là công cụ theo dõi dài hạn — theo dõi nó
suốt một run là thấy đường tụt.

## Ghi chú

`dashHeapProbe()` (webDashboard.cpp/.h + 3 lời gọi trong main.cpp) là **tạm**, đánh dấu
`TEMPORARY (2026-07-27)`. Giữ lại tới khi chốt được giả thuyết trên rồi xoá.
