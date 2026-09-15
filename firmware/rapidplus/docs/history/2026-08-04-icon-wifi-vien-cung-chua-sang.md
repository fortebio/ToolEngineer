# Icon WiFi trên TFT: cung chưa sáng vẽ viền xám thay vì chìm vào nền đen (2026-08-04)

## Vấn đề

> "màu wifi khi ít sóng trắng thì bth nhưng màu đen của sóng yếu chìm trong nền BLACK.
> có cách nào làm nổi bật các sóng còn lại hơn không"

Đúng. Và đây là **regression do bản bitmap sinh ra**, không phải thiếu sót có sẵn.

Bản vẽ vạch cũ (`fillRect`) có thể vẽ vạch chưa sáng thành viền. Khi chuyển sang bitmap
(`image_WIFI_Lv0/Lv1/Lv2`), `drawBitmap` **chỉ tô những pixel BẬT** — cung bị bỏ đi không được vẽ
gì cả, tức là **giữ nguyên nền đen**. Kết quả: sóng yếu và "một cái icon nhỏ" trông y hệt nhau, mất
đúng tính chất mà CLAUDE.md đã ghi từ đầu:

> Vạch chưa sáng vẽ **viền** chứ không bỏ trống — "1 trên 3" mới đọc ra là yếu.

## Cách sửa: vẽ hai lượt

```cpp
_displayCLD.display->fillRect(286, 9, 19, 16, BLACK);
if (bars >= 0)
  _displayCLD.display->drawBitmap(286, 9, image_WIFI_Connect, 19, 16, DARKGREY); // quạt đầy = viền
_displayCLD.display->drawBitmap(286, 9, icon, 19, 16, WHITE);                     // phần đang sáng
```

Lượt 1 đặt **toàn bộ** quạt xuống bằng xám; lượt 2 tô đè phần đang sáng bằng trắng.

```
  # = WHITE (sáng)   . = DARKGREY (viền, trước đây là nền đen)

     Lv0                Lv1                Lv2              Lv3/Connect
       .....              .....              .....              #####
     .........          .........          .........          #########
   ....     ....      ....     ....      ....     ....      ####     ####
  ...  .....  ...    ...  .....  ...    ...  #####  ...    ###  #####  ###
 ... ......... ...  ... ......... ...  ... ######### ...  ### ######### ###
... ....   .... ... ... ....   .... ... ... ####   #### ... ### ####   #### ###
 . ...  ...  ... .   . ...  ###  ... .   . ###  ###  ### .   # ###  ###  ### #
  ... ....... ...     ... ####### ...     ### ####### ###     ### ####### ###
```

Đọc ra ngay "0 / 1 / 2 / 3 trên 3".

## Vì sao `DARKGREY` (0x7BEF)

Hai phép so, cả hai phải tự đứng vững:

| Cặp | Tỷ lệ | Ý nghĩa |
| --- | --- | --- |
| DARKGREY trên BLACK | **5.0:1** | viền nhìn thấy được |
| WHITE trên DARKGREY | **4.2:1** | rõ là cung nào đang sáng |

`LIGHTGREY` bị loại: 12:1 so với nền nhưng chỉ **1.7:1** so với trắng — sáng/chưa sáng dính vào
nhau, tức là mất luôn thông tin cần truyền. Xám chứ **không dùng màu khác**: mạnh/yếu mã hoá bằng
**số cung**, không bằng màu (cùng luật với danh sách quét WiFi trên web).

## Hai chi tiết bắt buộc

- **Không vẽ lượt xám cho `image_WIFI_Disconnect`.** Glyph đó vốn đã là **viền rỗng có gạch chéo**;
  đệm một quạt đặc phía sau là hai nét vẽ đá nhau chứ không bổ sung cho nhau. Gate bằng
  `bars >= 0` — `wifiDisplayBars()` trả `-1` **khi và chỉ khi** mất kết nối.
- **Tính lồng nhau của 4 bitmap là thứ khiến lượt trắng phủ ĐÚNG phần đang sáng** — không có pixel
  xám nào lọt trong cung đang sáng, không có pixel trắng nào rơi ra ngoài quạt.
  `tools/test_wifi_bars.cpp` đã ghim sẵn (`lv[k] & ~lv[k+1] == 0`).

`fillRect` xoá ô icon vẫn giữ: nó là thứ dọn sạch khi **đổi qua lại** giữa glyph disconnect (có
pixel nằm ngoài quạt) và các mức.

Tiện thể gộp `WiFi.status()` trong `show_IconWifi` vào chính `wifiDisplayBars()` — trước đó hai chỗ
cùng hỏi một câu hỏi.

## Kiểm chứng

- Build SUCCESS, flash 71.9% (+40 B), 0 cảnh báo.
- `g++ -O2 -std=c++17 tools/test_wifi_bars.cpp -o t && ./t` ✓ (ngưỡng, chống nhảy, lồng nhau)

**Chưa có guard cho việc "phải vẽ hai lượt"** — nó là tính chất thị giác, mà `test_wifi_bars.cpp`
là test host không link `displayLCD.cpp`. Nếu lỗi này quay lại lần nữa thì đáng viết một guard đọc
source assert `show_IconWifi` có hai `drawBitmap` với hai màu khác nhau. Còn bây giờ thì comment tại
chỗ + mục CLAUDE.md là đủ cho một hàm 3 dòng vẽ.

**Phải nhìn trên máy thật**: xám 5:1 trên nét 1-2 px của TFT ILI9341 thật có thể mảnh hơn tôi tính —
nếu viền còn mờ quá thì nâng lên khoảng `0x9CF3` (~7.6:1 so với nền), nhưng đừng vượt quá đó, tách
bạch với trắng sẽ tụt xuống dưới 3:1.
