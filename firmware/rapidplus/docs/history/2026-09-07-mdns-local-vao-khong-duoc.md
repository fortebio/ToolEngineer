# 2026-09-07 — `http://<id>.local/` "thường vào không được": chẩn đoán + công cụ đo

## Triệu chứng được báo

Kane báo `http://rpl03003.local` **thường** vào không được; ảnh chụp kèm theo là iPhone
Safari mở `rpl250702.local` — **trang đen trắng trơn, thanh tiến trình còn chạy, nút X (stop)
còn hiện**, tức là trình duyệt **vẫn đang tải**, không phải đã báo "không tìm thấy server".

Chữ **"thường"** là chi tiết quan trọng nhất: lỗi **không cố định**. Một triệu chứng, ít nhất
**bốn nguyên nhân** khác nhau, và cả bốn đều hiện ra y hệt nhau trong trình duyệt.

## Vì sao không đoán được bằng mắt

| # | Nguyên nhân | Máy có trên LAN? | mDNS trả lời? | HTTP theo IP? |
| --- | --- | --- | --- | --- |
| 1 | Máy rơi về **SoftAP fallback** (STA không lên) | không | **không bao giờ** | không |
| 2 | mDNS responder chết / mạng chặn multicast | có | không | **có** |
| 2b | Multicast bị rớt lúc được lúc không | có | **lúc có lúc không** | có |
| 3 | DHCP đổi IP, client còn cache A record cũ | có | có, **IP cũ** | có (IP mới) |
| 4 | Máy + mạng khoẻ, lỗi nằm ở điện thoại | có | có | có |

Ca **1** là ca duy nhất đã được ghi trong tài liệu từ trước
([2026-07-24-mdns-stable-hostname.md](2026-07-24-mdns-stable-hostname.md)): SoftAP **cố ý
không announce mDNS** (đã có 192.168.4.1 + captive portal). Nghĩa là máy nào rơi về hotspot
thì `.local` **không tồn tại**, và đó là trạng thái không nhìn thấy được từ trình duyệt.

Ca **3** khớp nhất với ảnh chụp: tên phân giải ra một IP **chết**, TCP connect treo → trang
trắng + spinner. Nếu mDNS không phân giải được, Safari đã báo lỗi chứ không quay bánh xe.

## Ba khoảng hở tìm thấy trong firmware (đọc mã, chưa sửa)

### 1. `MDNS.begin()` chạy **đúng một lần trong đời máy**, không retry, không quan sát được

`webDashboard.cpp:2027` mở đầu bằng `if (started) return;`, và `started` được đặt `true` ở
`:2110` rồi **không bao giờ về false** — `dashboardSuspend()` chỉ đặt `suspended = true`
(từ 2026-07-27 nó không còn hạ server nữa). Khối mDNS nằm **sau** `started = true`, nên:

```cpp
static bool mdnsUp = false;
if (!mdnsUp && !apActive) { if (MDNS.begin(hn.c_str())) { ... mdnsUp = true; } }
```

được **đánh giá đúng một lần**, ngay lần network-up đầu tiên. `MDNS.begin()` trả `false` lần
đó (hết heap đúng thời điểm — nó chạy ngay sau `dashServer.begin()`) thì `.local` **chết tới
khi tắt/bật nguồn**, và dấu hiệu duy nhất là **thiếu** dòng serial `[dash] mDNS up ->`.
Không có gì trên web, trên TFT hay trong `/home` nói mDNS đang sống hay chết.

### 2. Comment ở `webDashboard.cpp:2114` đã **sai từ 2026-07-27**

> *"Started ONCE (dashboardBegin re-runs after every suspend/resume)"*

`dashboardBegin` **không** chạy lại sau suspend/resume nữa. Comment này làm guard `mdnsUp`
trông như thứ đang gánh việc, trong khi thực ra **cả khối** là one-shot. Ai đọc để sửa sẽ
tin nhầm rằng đã có đường retry.

### 3. Không có kênh nào báo trạng thái mDNS ra ngoài

`buildHomeJson()` đã có `net{ap,ssid,ip,gw,mask,dns1,dns2,rssi}` (`:553-573`) nhưng **không
có `mdns`**. Với fleet ngoài hiện trường, "máy có announce `.local` không" là câu hỏi không
trả lời được nếu không cắm cáp serial.

## Thứ đã làm trong lần này: `tools/probe_mdns.py`

Cùng tinh thần với `probe_dashboard_hang.py` — biến lời than thành số liệu. Mỗi vòng đo **ba
thứ độc lập** rồi in một dòng:

1. **Truy vấn mDNS thô** cho `<host>.local` (gửi cả bản QU và QM tới 224.0.0.251:5353, tự
   parse A record kể cả khi có nén tên) — trả lời câu *"chính con máy có đáp không?"*
2. **`getaddrinfo()` của OS** cho cùng cái tên — đây là đường **trình duyệt** đi, và nó
   **có thể hỏng trong khi truy vấn thô vẫn chạy** (cache/resolver của client).
3. **`GET /home` theo IP** vừa học được — tách "tên hỏng" khỏi "máy hỏng".

Rồi in **verdict gọi thẳng tên một trong bốn ca** ở bảng trên, kèm việc cần làm.

Chi tiết cài đặt đáng nhớ:

- **Đặt bit QU (RFC 6762 §5.4)** chứ không chỉ QM: PC nào đã cài Bonjour (iTunes) thì cổng
  5353 bận, không join được nhóm multicast — QU khiến máy trả lời **unicast thẳng về cổng
  ephemeral**, nên tool vẫn chạy. Bind 5353 thành công thì nhận thêm cả trả lời multicast;
  dòng đầu output nói rõ đang ở chế độ nào.
- **Phát hiện ca 3 bằng cách gom TẬP HỢP các IP đã thấy**, không so từng vòng: IP cũ và IP
  mới xuất hiện xen kẽ, so từng vòng sẽ không thấy gì bất thường.
- Chỉ stdlib, như mọi probe khác trong repo.

## Cách dùng (chạy trên PC **cùng WiFi** với máy, tắt VPN)

```bash
python tools/probe_mdns.py rpl03003            # chạy tới khi Ctrl+C
python tools/probe_mdns.py rpl03003 30         # dừng sau 30 vòng
python tools/probe_mdns.py rpl03003 --ip 192.168.0.103   # biết IP: test HTTP cả khi mDNS câm
python tools/probe_mdns.py rpl03003 --iface 192.168.0.50 # PC nhiều NIC: chỉ định card WiFi
```

Để nó chạy **trong lúc** thử lại trên điện thoại — thứ cần biết là hai bên có hỏng cùng lúc
không.

## Đường thoát có sẵn ngay bây giờ (không cần sửa firmware)

Màn **QR** trên máy (nút TRẮNG ở màn chính) in **IP** ngay dưới mã QR. Đó chính là lý do
2026-08-04 cố ý giữ hai dạng địa chỉ: QR mã hoá `.local`, caption in IP — *"lý do người ta
đọc dòng IP chính là vì quét không vào được"*
([2026-08-04-qr-ma-hoa-mdns-local.md](2026-08-04-qr-ma-hoa-mdns-local.md)). Gõ IP đó vào là
vào được, kể cả ở ca 1/2/2b/3.

## Đề xuất sửa (CHƯA làm — chờ quyết định)

1. **Retry mDNS trong `dashboardLoop()`** thay vì one-shot trong `dashboardBegin()`: mỗi
   ~30 s, nếu `!mdnsUp && !apActive && WiFi.status() == WL_CONNECTED` thì thử lại. Rẻ, và
   nó đóng đúng khoảng hở "hỏng một lần là chết tới lúc reboot".
2. **Re-announce khi IP đổi**: nhớ `WiFi.localIP()` lần announce cuối; IP đổi → `MDNS.end()`
   + `MDNS.begin()` lại. Đóng ca 3 ở phía máy (phía client vẫn còn TTL cache, nhưng máy sẽ
   phát bản ghi mới ngay thay vì đợi hết TTL).
3. **Thêm `net["mdns"] = mdnsUp`** vào `buildHomeJson()` + hiện ở thẻ `#setAbout`. Biến một
   trạng thái chỉ thấy qua cáp serial thành thứ đọc được từ điện thoại.
4. **Sửa comment sai ở `:2114`.**

Cả ba mục 1-3 đều là **thay đổi hành vi mạng của thiết bị y tế** → mỗi mục một commit riêng,
kèm doc riêng, theo quy tắc ở CLAUDE.md.

## Giới hạn đã biết của probe

- **Không đo được phía điện thoại.** Nó chạy trên PC; nếu PC xanh mà điện thoại vẫn đỏ thì
  verdict cuối cùng nói đúng điều đó, nhưng vẫn phải kiểm tra tay: điện thoại có đang ở
  **cùng SSID** không (guest network / băng 5 GHz tách VLAN là thủ phạm hay gặp).
- **Không phân biệt được "responder chết" với "mạng chặn multicast"** — cả hai đều là ca 2.
  Muốn tách thì phải đọc serial log của máy (`[dash] mDNS up ->`).
- mDNS là **link-local**: chạy qua VPN hoặc khác subnet thì luôn ra ca 1, sai.
