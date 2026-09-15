# 2026-08-02 — Bật SoftAP theo yêu cầu từ màn QR (RED long-press → GREEN)

## Yêu cầu

Giữ nút **ĐỎ** → bấm **XANH** đang mở màn QR. Muốn: chính lúc đó **bật luôn SoftAP**, và QR trở
thành mã để điện thoại **join hotspot** thay vì một địa chỉ trên LAN mà điện thoại không ở trong.

Lý do hợp lý: menu này là nơi người vận hành tìm tới khi máy **chưa có WiFi dùng được**.

## Vì sao không phải chỉ thêm một lời gọi hàm

`dashboardStartAP()` làm `WiFi.mode(WIFI_AP)` — **giết STA, không có đường về khi đang chạy**.
Chính comment trong `dashboardLoop()` đã ghi điều đó, và `test_no_runtime_wifi_begin.py` khoá bất
biến "không `WiFi.begin()` runtime" vì gọi nó từ một task làm **deadlock `async_tcp` vĩnh viễn**
(GOTCHA 8). Nên chuyện này có ba ràng buộc, không phải một:

1. **Ai đổi mode**: không được là InputTask. Nút chỉ **đặt cờ**; việc chuyển mode chạy ở
   `dashboardLoop()` — **NetworkTask**, đúng task sở hữu dashboard. Cùng khuôn với `otaState`
   (AsyncTCP ghi một byte volatile, NetworkTask nhặt).
2. **Không được làm giữa run**: hạ STA lúc đang chạy là kéo theo cả **upload cuối run**. Yêu cầu
   bị **bỏ** (không xếp hàng) nếu `dashboardDeviceBusy()`, để nó không nổ vào một thời điểm bất kỳ
   sau đó.
3. **Phải có đường ra**: AP-only là một chiều. Rời màn QR ⇒ **reboot** để về STA.

## Cách làm

```
GREEN ở eSettingMenu ──► dashboardRequestAP()   (chỉ đặt cờ, InputTask)
                          │
                          ▼
        dashboardLoop()  ──► apActive?      → không làm gì, KHÔNG đánh dấu on-demand
        (NetworkTask)    ──► busy?          → bỏ qua, QR vẫn hiện địa chỉ STA
                         ──► còn lại        → dashboardStartAP(); apOnDemand = true
                                              + changeScreen nếu đang ở eShowQR
                          │
RỜI eShowQR (mọi đường) ──┴─► apOnDemand? → chờ 1.5s → dashboardRequestRestart()
                                             (quay lại eShowQR thì đứng xuống)
```

### Ba quyết định đáng ghi

**`apOnDemand` tách khỏi `apActive`.** Nếu AP lên do **fallback lúc boot** (STA không join được)
thì rời màn QR **không được** reboot: máy sẽ chỉ quay lại đúng cái AP đó, và mỗi lần liếc QR là một
lần khởi động lại. Chỉ reboot khi chính thao tác này raise AP.

**Cờ bị tiêu thụ vô điều kiện** (`apRequested = false` ngay đầu), kể cả khi bị từ chối vì busy
hoặc `suspended`. Xếp hàng lại sẽ khiến hotspot bật lên ở một thời điểm người dùng không còn liên
quan — xem mục lỗi bên dưới, đây đúng là chỗ bản đầu tiên sai.

**Vẽ lại màn QR sau khi AP lên.** DisplayTask vẽ QR *trước* khi radio đổi mode, nên nó còn hiện URL
của STA. `dashboardStartAP()` xong thì đặt `changeScreen = true` nếu đang ở `eShowQR` —
`screen_QR()` vốn đã chọn nội dung theo `dashboardIsAP()`, nên chỉ cần vẽ lại là đúng.

## Không đụng tới

- **`WiFi.softAP()` vẫn đúng MỘT call site** (`dashboardStartAP`), `dashboardApName()` vẫn đúng một
  consumer — `test_qr_payload.py` khoá cả hai, và cả hai còn xanh.
- **Không thêm `WiFi.begin()` runtime** — `test_no_runtime_wifi_begin.py` xanh.
- **Không dùng `WIFI_AP_STA`** (giữ cả hotspot lẫn mạng ngoài). ESP32 bắt AP dùng **đúng kênh của
  STA**, tốn thêm heap cho DHCP+DNS đúng lúc TLS cần 42KB liền mạch (GOTCHA 2), và repo chưa bao
  giờ chạy chế độ đó. Đã cân nhắc và loại có chủ ý.
- **`dashboardRequestRestart()` giữ nguyên gate** (`!busy && !suspended && != escreenFinished`) nên
  reboot không thể cắt vào run hay vào lúc tính kết quả. Lưu ý: `otaRestartAt` nay có **ba** nguồn
  (OTA, đổi Device ID, và đường này).

## Ba lỗi tự soát ra ngay sau khi viết (code-review)

**Chỉ TRẮNG mới gỡ hotspot.** `handleLongPress_Red/Blue/White` đều ghi đè `type_infor` **từ mọi
state**, kể cả `eShowQR`. Vào QR (AP lên, STA chết) rồi giữ ĐỎ là máy **kẹt trên hotspot**, run sau
chạy xong **không upload được**, màn hình không nói gì. Liệt kê từng đường thoát là để lỗ — nay
`dashboardLoop()` canh điều kiện **"không còn ở `eShowQR`"**, phủ mọi đường, cả những đường thêm sau.
Nó giữ deadline riêng (`apExitAt`) thay vì arm `dashboardRequestRestart()` ngay, để **quay lại màn
QR thì đứng xuống** — cờ `otaRestartAt` dùng chung với OTA và đổi Device ID, huỷ nó là huỷ cả của
họ. Điều kiện này **không sống sót qua chính reboot nó gây ra** (`apOnDemand` là false sau boot, và
fallback không bao giờ set nó), nên không phải vòng lặp reboot.

**Yêu cầu bị treo lại khi `suspended`.** `if (suspended) return;` nằm **trên** khối tiêu thụ, nên
bấm lúc đang upload thì cờ nằm đó và hotspot bật lên ở tick đầu tiên sau khi upload xong — vài phút
sau, không ai liên hệ được với nút đã bấm. Nay khối tiêu thụ nằm **trên** early-return và từ chối
khi `suspended || busy`.

**Race đặt cờ trước `type_infor`.** InputTask (core 1) và NetworkTask (core 0) chạy thật sự song
song, loop tick ~10 ms. Đặt cờ trước rồi mới ghi `eShowQR` để lộ khe: NetworkTask tiêu thụ cờ khi
`type_infor` chưa phải `eShowQR` → **không ai xin vẽ lại**, QR giữ nguyên URL của mạng radio vừa
rời. Đổi thứ tự: `type_infor` trước, `dashboardRequestAP()` sau.

## Khoảng hở đã biết

**Yêu cầu bị từ chối vì busy thì không có tín hiệu trên màn.** Serial in
`[dash] SoftAP request ignored - device busy`, nhưng người đứng ở máy không xem Serial — họ chỉ
thấy QR hiện địa chỉ STA như cũ (vẫn dùng được). Muốn rõ hơn thì `screen_QR()` cần một dòng trạng
thái; chưa làm vì chưa chắc tình huống này có thật (menu chỉ tới được khi bấm giữ ĐỎ).

## Kiểm

Build SUCCESS **2 400 529 B (71.8%)**, 0 cảnh báo trong `src/`. 7/7 guard host-side xanh
(`test_qr_payload`, `test_no_runtime_wifi_begin`, `test_phase0_guards`, `test_device_id`,
`test_no_method_branch`, `test_ota_guards`, `test_web_assets`).

**Phần này KHÔNG kiểm được bằng mock** — nó là chuyển mode radio thật. Phải thử trên máy:

1. Máy **đang nối WiFi**, dashboard chạy được. Giữ ĐỎ → XANH.
   Serial: `[dash] SoftAP requested from the setting menu` → `[dash] SoftAP 'FBT-<id>' at http://192.168.4.1/`.
   QR trên TFT phải **đổi thành mã join WiFi**, quét bằng điện thoại → vào được dashboard qua
   captive portal. Người đang xem dashboard qua LAN **sẽ bị rớt** — đúng như thiết kế.
2. Bấm **TRẮNG** rời màn QR → Serial `[dash] left the QR screen - rebooting out of the on-demand
   SoftAP` → `[dash] deferred restart`, máy khởi động lại và **quay về STA**.
2b. Lặp lại nhưng thoát bằng **giữ ĐỎ** (về Setting menu) — phải thấy đúng hai dòng đó. Đây là
   đường mà bản đầu tiên bỏ sót.
2c. Rời QR rồi **quay lại QR trong 1.5 s** — **không** được reboot.
3. Lặp lại khi máy **đang chạy run**: phải thấy `SoftAP request ignored - device busy`, STA còn
   nguyên, và run chạy hết + upload bình thường.
4. Máy **không có WiFi nào** (AP đã lên do fallback): giữ ĐỎ → XANH → `SoftAP already up`, và bấm
   TRẮNG **không** được reboot.
