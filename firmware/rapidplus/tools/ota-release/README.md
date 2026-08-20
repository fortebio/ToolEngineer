# Đưa fleet ≤ v2.4.3 lên **v2.4.4** — file cần đẩy lên repo OTA

**Chưa đẩy gì cả.** Đây là file soạn sẵn. Kế hoạch gốc:
[docs/plan/2026-07-28-ota-fleet-upgrade-243.md](../../docs/plan/2026-07-28-ota-fleet-upgrade-243.md).
Đợt v2.4.3 **chưa từng phát hành**, nên đích bây giờ thẳng lên **v2.4.4**.

## Nghịch lý phải hiểu trước

v2.4.4 **bỏ GitHub**, hỏi Engineer Server (`hub.fortebio.tech` + Funnel dự phòng). Nhưng máy
ngoài đồng đang chạy **firmware cũ**, và firmware cũ chỉ biết GitHub. Máy không thể dùng đường
mới để lên bản có đường mới → **đợt chuyển giao này vẫn phải đi bằng GitHub**, đúng một lần.
Lên tới v2.4.4 rồi thì các branch dưới đây thành vô dụng (máy không đọc nữa) — xoá được.

URL đó **không cần có trong source hôm nay** — nó đã nằm trong **ảnh đã nạp vào máy**. `strings`
trên ảnh v2.4.3 build lại từ commit `8b43397`: `https://raw.githubusercontent.com/wuanpham/FBTRapidplusOTA/`
+ `updateOTA.json` + `v2.4.3` (ba chuỗi rời vì `baseUrl` nối lúc chạy). Máy **đang** gọi
`.../FBTRapidplusOTA/v2.4.3/updateOTA.json` mỗi lần boot, chỉ là nhận **404** vì branch chưa có.

## Ba chặn cứng trước khi đẩy

1. **Rotate ingest Bearer + ERP X-API-Key, redeploy GAS**, cập nhật `src/secrets.h`, **build
   lại**. `strings` trên `.bin` cho ERP key @offset **3144** và ingest Bearer @**3251** — trong
   4 KB đầu, không cần dịch ngược. `FBTRapidplusOTA` là repo **PUBLIC**.
2. ⛔ **MỚI ở v2.4.4: token đó cũng mở `/ota/*` trên Engineer Server.** Đẩy `.bin` v2.4.4 lên
   repo public = **trao quyền nạp firmware cho cả 109 máy** cho bất kỳ ai chạy `strings`. Đây
   không phải rủi ro của các đợt trước.
   **Đường gỡ rẻ nhất là phía SERVER, không phải firmware**: tách scope — endpoint *engineer
   upload/publish* phải đòi một credential **không nằm trong firmware**; Bearer của máy chỉ còn
   `GET /ota/check`, `GET /ota/{file}` và POST kết quả. Khi đó token rò về đúng mức rủi ro
   dự án đã chấp nhận từ trước (giả mạo kết quả), **không phải chiếm fleet**.
   Không tách được scope thì **đừng publish** — dùng đường thủ công ở cuối file.
3. **Gate go/no-go — và nó nằm ở firmware CŨ, không phải bản mới.** Máy tự tải bằng code đang
   chạy: v2.4.3 có `NetworkTask` **6144 B** (main.cpp:401), v2.4.2/v2.4.0 có **8192**. Chỗ hẹp
   là đám v2.4.3. `httpUpdate.update()` ôm mbedTLS + HTTPClient + Update trong đó; tràn là
   **panic**, không phải `HTTP_UPDATE_FAILED`. Đo `uxTaskGetStackHighWaterMark` trên **một máy
   v2.4.3 bench** sau một lần OTA thật, trước khi mở cho cả fleet.

## Vì sao đẩy lên branch của version CŨ

`baseUrl` gắn version **đang chạy** (`src/updateOTA.cpp:11` của firmware cũ):

```text
https://raw.githubusercontent.com/wuanpham/FBTRapidplusOTA/<FirmwareVer đang chạy>/
```

Máy 2.4.3 hỏi branch `v2.4.3`, máy 2.4.2 hỏi `v2.4.2`. Bản 2.4.4 vì vậy phải nằm trên branch
của **mọi version cũ hơn**, không phải branch `v2.4.4`.

| Branch | Phủ máy nào | `currentVersion` ngoài đồng | `versionCode` publish |
| --- | --- | ---: | ---: |
| `v2.4.0` | v2.4.0 | 16 | 20 |
| `v2.4.0.x` | v2.4.0.x HotlidDisable | 16 | 20 |
| `v2.4.2` | cả hai bản 2.4.2 (17 **và** 18) | 18 | 20 |
| `v2.4.3` | **máy 2.4.3** — branch này lần trước cố ý BỎ vì 2.4.3 là đích | 19 | 20 |

Prompt chỉ hiện khi `versionCode > currentVersion` (**`>`**, không phải `>=`). Guard:
`python tools/test_ota_release_manifest.py`.

Mỗi branch cần **2 file ở gốc**: `updateOTA.json` (trong thư mục này) + `firmware.bin`
(`.pio/build/esp32dev/firmware.bin`, **sau khi rotate token**). `fileName` là đường dẫn tương
đối trên **cùng branch đó** — `fwUrl = baseUrl + fileName`, nên phải là tên file trần.

`httpUpdate` **tắt follow-redirect** mặc định (`HTTPUpdate.cpp:38`) → URL phải trả **200 trực
tiếp**: dùng `raw.githubusercontent.com`, **không** dùng link Releases.

**Không tạo branch `v2.4.4`.** Máy đã lên 2.4.4 không đọc GitHub nữa; để trống là xong.

## Cách đẩy (một branch)

```bash
git clone https://github.com/wuanpham/FBTRapidplusOTA && cd FBTRapidplusOTA
git checkout -b v2.4.3 main            # lặp lại cho v2.4.2 / v2.4.0 / v2.4.0.x
cp <repo firmware>/tools/ota-release/v2.4.3/updateOTA.json .
cp <repo firmware>/.pio/build/esp32dev/firmware.bin .
git add updateOTA.json firmware.bin && git commit -m "v2.4.4 (versionCode 20)"
git push -u origin v2.4.3
```

Kiểm bằng đúng URL mà máy sẽ gọi:

```bash
curl -sI https://raw.githubusercontent.com/wuanpham/FBTRapidplusOTA/v2.4.3/firmware.bin
# phải 200, KHÔNG 301/302
curl -s  https://raw.githubusercontent.com/wuanpham/FBTRapidplusOTA/v2.4.3/updateOTA.json
```

## Sau khi đẩy

- Firmware cũ **chỉ kiểm 1 lần mỗi boot** (~2 s sau khi bật) và **không kiểm lại** trong phiên
  — poll 6 h là tính năng của v2.4.4, máy chưa lên thì chưa có. Máy trượt cửa sổ đó thì tắt/bật,
  hoặc bấm **Check** trong tab Setting → Firmware (máy 2.4.3 có; 2.4.2/2.4.0 không có dashboard).
- Tải thất bại → máy park `OTA_FAILED`, **không tự thử lại**, phải power-cycle.
- Người vận hành vẫn phải **bấm ĐỎ** ở màn "Update available" (hoặc nút Update trên web). Không
  có bản nào tự cài im lặng.
- Fleet lên hết → **xoá `firmware.bin` khỏi các branch** để đóng cửa sổ phơi nhiễm, rồi rotate
  token lần hai và phát qua chính đường server mới.

## Đường thủ công (khi không publish)

- **Máy v2.4.3**: `POST /otaupload` có sẵn. UI nhúng của 2.4.3 **không bao giờ gửi `?md5=`**
  (hàm `md5Hex` chỉ có từ 2026-08-18), nên nạp bằng trình duyệt ở máy 2.4.3 là **không kiểm gì**
  — ảnh cụt vẫn boot. Phải dùng curl, cùng LAN với máy:

  ```bash
  curl -f -F "firmware=@firmware.bin" \
       "http://<ip-máy>/otaupload?md5=$(md5sum firmware.bin | cut -d' ' -f1)"
  ```

- **Máy v2.4.2 / v2.4.0**: **không có route `/otaupload`** (kiểm:
  `git show v2.4.2:src/webDashboard.cpp | grep -c otaupload` → 0) → **cầm dây**, `esptool`.
