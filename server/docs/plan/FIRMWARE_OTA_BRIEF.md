# Bàn giao: OTA phía FIRMWARE (repo `FBT-DXD`)

> **Cách dùng**: copy nguyên file này sang repo firmware (`FBT-DXD/docs/` chẳng hạn) rồi mở
> phiên Claude/làm việc ở đó. File tự chứa đủ ngữ cảnh — không cần đọc repo app/server.
> Viết 2026-08-17 khi phần server xong; **cập nhật cùng ngày**: firmware v2.4.4 đã gọi được
> `/ota/check`, và server đã có domain Cloudflare — việc còn lại đổi trọng tâm, xem §1.

## 1. Việc cần làm, một câu

~~Firmware phải tự hỏi server có bản mới không~~ — **v2.4.4 đã làm xong phần đó**.

Việc còn lại: **đổi base URL sang `https://hub.fortebio.tech` ngay trong bản đem đi nạp tay cho
109 máy**, để một lần cầm dây là máy về thẳng hạ tầng mới, khỏi phải OTA thêm một vòng nữa.

## 2. Bối cảnh: tại sao đổi URL bây giờ

- Server nay ra internet bằng **Cloudflare Tunnel** (`https://hub.fortebio.tech`), thay cho
  Tailscale Funnel (`https://fbt.basa-luma.ts.net`). **Cả hai đang chạy song song**, cùng trỏ về
  một tiến trình uvicorn trên box — nên bản firmware trỏ URL nào cũng hoạt động lúc này.
- 109 máy ngoài hiện trường **nạp cứng URL cũ** và đang chạy bản TRƯỚC v2.4.4 (chưa biết gọi
  `/ota/check`) → **đợt v2.4.4 đầu tiên bắt buộc nạp tay** (`POST /otaupload` từ trình duyệt hoặc
  cầm dây). Đã phải cầm dây thì nhân tiện đổi luôn URL, đừng để dành.
- Sau đợt này mọi bản sau đi bằng OTA bình thường.
- Server KHÔNG đẩy xuống được: thiết bị sau NAT. Bắt buộc máy phải **hỏi**.

### ⚠️ Ba thứ bản này phải xử CÙNG LÚC

**1. Root CA — chưa có câu trả lời, phải kiểm trước khi build.**
`*.ts.net` dùng cert **Let's Encrypt (ISRG Root X1)**; `hub.fortebio.tech` dùng cert edge
Cloudflare do **Google Trust Services** ký. Nếu firmware `setCACert()` ghim một root cụ thể thì
đổi URL = **cả fleet đứt TLS cùng lúc**, mà đường sửa duy nhất lại là OTA. Kiểm trong code
`FBT-DXD` xem đang `setInsecure()` hay ghim CA; ghim CA thì phải nạp thêm root của Google Trust
Services (hoặc dùng bundle nhiều root).

**2. Giữ URL cũ làm dự phòng.**
Chỉ biết mỗi Cloudflare là **cửa một chiều**: bên đó có sự cố (Access bị bật lại, cache, cert)
thì không còn đường nào đẩy bản sửa xuống. Nên thử `hub.fortebio.tech` trước, hỏng thì rơi về
`fbt.basa-luma.ts.net`. Funnel miễn phí và vẫn đang chạy, giữ nó làm lưới an toàn.

**3. Version GIỮ NGUYÊN `v2.4.4`** — chốt của chủ dự án 2026-08-17: mọi cải tiến đợt này gói
trong `v2.4.4`, không lên số mới. Hai hệ quả phải nhớ, vì chúng đổi cách vận hành:

- ✅ **Không còn bẫy hạ cấp ngược.** Máy nạp tay xong gọi `/ota/check`, thấy `fbt_v2.4.4.bin`
  trùng version đang chạy → bỏ qua. Khỏi phải `DELETE /ota/target` trước mỗi đợt nạp tay.
- ⚠️ **Mất tín hiệu "máy nào đã sang Cloudflare".** `sessions.version` là thứ DUY NHẤT tab
  "Trạng thái máy" dùng để biết máy chạy gì; hai binary cùng xưng `v2.4.4` thì nhìn bảng không
  phân biệt được máy nào còn gọi qua `ts.net`. Mà đó chính là dữ liệu để quyết định **khi nào
  tắt được Funnel**. → Phải lấy tín hiệu đó bằng đường khác (xem §6).
- ⚠️ **File `.bin` bị thay ruột mà giữ nguyên tên** → đúng kịch bản cache stale của Cloudflare.
  **Bắt buộc có Cache Rule bypass `/ota/*` TRƯỚC khi upload bản mới**, và purge cache nếu tên
  file đó đã từng được tải qua edge.

## 3. Hợp đồng server (đã chạy thật, kiểm được ngay)

Base URL mới: `https://hub.fortebio.tech` · dự phòng: `https://fbt.basa-luma.ts.net`
Xác thực: `Authorization: Bearer <RECEIVER_TOKEN>` — **cùng token firmware đang dùng để POST
kết quả**, không cần token mới.

### `GET /ota/check`

Chưa chọn bản nào:

```json
{"update": false}
```

Có bản để nạp:

```json
{
  "update": true,
  "version": "fbt_v2.4.4.bin",
  "size": 2193408,
  "sha256": "9f2b...c1",
  "url": "https://hub.fortebio.tech/ota/fbt_v2.4.4.bin"
}
```

- **`version` chính là TÊN FILE** admin đã tải lên. Server không hiểu ngữ nghĩa version —
  **so sánh là việc của firmware**. → Quy ước đặt tên file phải chứa version, vd `fbt_v2.4.4.bin`.
- `sha256` là của **nguyên file `.bin`**. Kiểm trước khi commit phân vùng OTA.
- **`url` server dựng từ chính request** (`request.url`), không hardcode → gọi `/ota/check` qua
  host nào thì nhận link tải về host đó. Firmware không phải ghép URL thủ công, và cơ chế dự
  phòng ở §2 tự động đúng: gọi qua `ts.net` thì link tải cũng về `ts.net`.

### `GET /ota/<tên file>.bin`

Trả nguyên bytes, `Content-Type: application/octet-stream`. Vẫn cần Bearer.

Kèm header **`x-MD5`** — thư viện `HTTPUpdate` của ESP32 **tự đọc đúng tên header này** và gọi
`Update.setMD5()`, nên dùng `httpUpdate.update()` là được kiểm toàn vẹn ảnh mà không cần thêm
dòng code hash nào. **Tên header phân biệt hoa/thường**, đừng đổi thành `X-MD5`/`Content-MD5`.

### Test nhanh từ máy tính (PowerShell — `curl` là alias, phải gọi `curl.exe`)

```powershell
curl.exe -s -H "Authorization: Bearer <token>" https://hub.fortebio.tech/ota/check
```

## 4. Firmware cần làm gì

1. **Khi nào hỏi**: lúc khởi động, và định kỳ (vd mỗi 6–24h).
   ⚠️ **TUYỆT ĐỐI không cập nhật khi đang chạy một lượt đo** — nạp firmware giữa chừng là hỏng
   mẫu xét nghiệm của khách. Chỉ nạp lúc máy rảnh.
2. **So version**: `update == true` và `version` khác bản đang chạy → mới tải.
3. **Tải + kiểm**: `httpUpdate.update()` đã kiểm md5 qua header `x-MD5`. Muốn kiểm thêm thì tải
   thủ công rồi so `sha256` server trả. Sai → **bỏ, không nạp**.
4. **Nạp**: ESP32 `Update.h` / `HTTPUpdate`.
5. **Reboot + tự xác nhận**: dùng cơ chế 2 phân vùng app (`esp_ota_mark_app_valid_cancel_rollback`)
   để bản mới chết là **tự quay về bản cũ**, không biến máy thành cục gạch ngoài hiện trường.
6. **Báo cáo**: KHÔNG cần endpoint riêng — nhưng **gửi kèm `?ver=<bản đang chạy>` ngay trong
   `/ota/check`** (v2.4.5+ đã làm, `updateOTA.cpp`). Server ghi vào `fw_seen.json` và tab
   "Trạng thái máy" hiện đúng bản máy đang chạy **ngay sau khi nạp**, khỏi chờ lượt đo tiếp
   theo. Không có `ver` thì server rơi về version của phiên đo gần nhất như trước.

### Gửi kèm `?device=<id_device>` ngay từ bản đầu

`GET /ota/check?device=RPL03003` — server **hiện bỏ qua** tham số này, nhưng bước sau sẽ cần
(chọn bản theo từng máy + biết máy nào đã lên bản mới). Gửi ngay từ đầu thì sau này nâng cấp
server không phải nạp lại firmware lần nữa.

## 5. Cạm bẫy đã biết (từ chính dự án này)

- **HTTPS trên ESP32 phải dùng `WiFiClientSecure`** + `setInsecure()` hoặc nạp CA — xem cảnh báo
  root CA ở §2. Dùng `WiFiClient` thường thì `http.POST`/`GET` trả **-1**, không thông báo gì rõ.
- **File `.bin` ~2MB** — kiểm phân vùng OTA đủ chỗ; `size` server trả dùng để kiểm trước khi tải.
- **`.bin` mang `RECEIVER_TOKEN` ở 4 KB đầu** (`strings` là ra) → **KHÔNG publish `.bin` lên repo
  public**. Và **rotate `RECEIVER_TOKEN` sau khi fleet lên bản mới là tự khoá mình ra ngoài** (mọi
  máy 401, mất luôn đường OTA) — muốn rotate thì phải tách token OTA riêng TRƯỚC.
- **Debug "nạp xong tự reset lặp"**: đọc log boot qua serial. `invalid header: 0xffffffff` =
  thiếu/sai bootloader hoặc sai offset · `Guru Meditation` = firmware crash · `Brownout` = nguồn
  yếu · `flash read err` = sai flash mode (DIO/QIO).
- **Thiết bị tự reset khi mở cổng serial**: DTR/RTS là mạch auto-reset (DTR→EN, RTS→GPIO0). Khi
  debug bằng công cụ PC nhớ ghim `dtr=off, rts=off`, không thì cứ mở cổng là máy reboot.

## 6. Trạng thái mọi thứ (2026-08-17)

| | |
|---|---|
| Server FastAPI trên MiniPC | ✅ chạy thật, 109 máy đang đẩy dữ liệu |
| API OTA (`/ota/check`, upload, chọn bản) | ✅ deploy + xác minh |
| App: tab Quản lý máy | ✅ dùng được |
| Firmware gọi `/ota/check` | ✅ **v2.4.4 đã làm** |
| Cloudflare Tunnel `hub.fortebio.tech` | ✅ chạy, song song với Funnel |
| **Firmware trỏ sang Cloudflare** | ❌ **việc của tài liệu này** |
| Nạp bản mới cho 109 máy (đợt tay đầu tiên) | ❌ chưa |
| Chọn bản theo TỪNG máy | ❌ chưa (hiện chỉ 1 target toàn cục) |

**Cảnh báo rollout**: hiện chỉ có **một target toàn cục** — chọn bản là cả 109 máy cùng nhận.
Firmware lỗi = hỏng cả fleet. Thử trên 1–2 máy trước; việc TIẾP THEO nên là chọn-theo-từng-máy.

**Việc phía server trước khi cho fleet chuyển** (đã ghi, chưa xong):
- ✅ **XONG 2026-08-17** (xác minh bằng `cf-cache-status: BYPASS`) — Cache Rule **bypass `/ota/*`**
  trên Cloudflare. Lý do cần: `.bin` nằm trong danh sách extension cache mặc
  định, upload trùng tên xong edge vẫn phát bản cũ, và `x-MD5` **không cứu** (header đi kèm chính
  file cũ đang cache nên firmware kiểm thấy "khớp" rồi nạp nhầm bản).
- Kiểm `url` trong `/ota/check` qua domain mới trả về **`https://`** chứ không phải `http://`. ✅ **ĐÃ KIỂM 2026-08-17: trả đúng `https://`.**

### Biết máy nào đã sang Cloudflare khi version không đổi

Giữ nguyên `v2.4.4` cho cả hai binary → `sessions.version` **không còn phân biệt được**, mà
đó là dữ liệu để quyết định khi nào tắt Funnel. Ba cách lấy lại tín hiệu, rẻ dần:

1. **Ghi `Host` lúc ingest** (1 dòng trong `main.py`, phải redeploy): log hoặc lưu header
   `Host` của mỗi lần POST → biết chính xác từng máy đi cửa nào. Đầy đủ nhất.
2. **Đếm ở tầng proxy**: Cloudflare dashboard → Analytics cho số request vào `hub.fortebio.tech`;
   so với tổng số phiên trong DB. Biết được TỆ LỆ, không biết MÁY NÀO.
3. **Tắt Funnel rồi xem ai im lặng** — dứt khoát nhưng **mất dữ liệu đo** của máy chưa chuyển
   trong lúc tắt. Chỉ dùng khi đã khá chắc, và bật lại ngay khi thấy máy rụng.

## 7. Nếu cần xem lại phía server

Repo app/server: `FBT-ToolRapidPlus/`
- Hợp đồng + code OTA: `server/app/main.py` (mục "OTA firmware")
- Nhật ký chi tiết ngày làm: `server/docs/history/2026-08-17.md`
- Việc tiếp theo tổng thể: `server/docs/plan/VIEC_TIEP_THEO.md`
- Hạ tầng Cloudflare: `server/docs/plan/CLOUDFLARE_TUNNEL.md`
- Cạm bẫy toàn dự án: `CLAUDE.md` ở gốc repo
