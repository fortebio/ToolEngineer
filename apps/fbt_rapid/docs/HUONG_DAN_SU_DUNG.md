# Hướng dẫn sử dụng — App FBT_RAPID

Tài liệu dành cho **người dùng** app đồng hành của máy **Forte Rapid+ / FBT_RAPID** (Windows).
App giúp xem **kết quả xét nghiệm**, **đồ thị CT**, quản lý **nhiều máy qua cloud**, và **theo dõi nhiệt độ** của máy.

---

## 1. Cài đặt app

Bạn nhận được một trong hai file:

- **`FBT_RAPID-Setup-vX.Y.Z.exe`** (bộ cài — khuyến nghị): bấm đúp → Next → (chọn tạo shortcut Desktop nếu
  muốn) → Install → Finish. App cài **không cần quyền admin**, tự tạo shortcut ở Start Menu.
- **`FBT_RAPID-App-Windows.zip`** (bản chạy thẳng): **giải nén toàn bộ** thư mục ra một nơi cố định, rồi
  mở **`fbt_dxd_app.exe`**. ⚠ Giữ nguyên các file `.dll` và thư mục `data` **cạnh** file `.exe`.

> **Windows cảnh báo "Windows protected your PC"?** App chưa ký số nên Windows hỏi 1 lần. Bấm
> **More info → Run anyway**. (Chỉ cần làm một lần.)

Yêu cầu: Windows 10/11 64-bit.

---

## 2. Tổng quan giao diện

Cột bên trái có **4 tab**:

| Tab | Dùng để |
|---|---|
| **Lịch sử** | Lấy & xem kết quả **trực tiếp từ một máy** (qua WiFi/LAN), lưu trên máy tính này |
| **Cloud** | Xem lịch sử **nhiều máy** từ Google Drive |
| **Log nhiệt** | Theo dõi **nhiệt độ** máy theo thời gian thực qua cáp USB |
| **Cài đặt** | Cấu hình IP máy, thông tin người dùng… |

---

## 3. Cài đặt ban đầu (tab Cài đặt)

Mở tab **Cài đặt** và điền:

- **Địa chỉ IP của máy** — vd `192.168.1.50`. Bấm **Kiểm tra kết nối** để chắc máy phản hồi. *(Chỉ cần
  cho tab Lịch sử; máy phải bật WiFi cùng mạng với máy tính.)*
- **Khoảng đọc (giây)** — mặc định 20. Dùng để quy đổi trục thời gian của đồ thị CT.
- **Thông tin người dùng** — tên, đơn vị/phòng khám.

Bấm **Lưu**.

> Phần **Cloud** đã được **cấu hình sẵn** trong app — bạn không cần nhập gì, tab Cloud chạy ngay.

---

## 4. Tab "Lịch sử" — lấy kết quả trực tiếp từ máy

1. Đảm bảo đã nhập **IP máy** ở Cài đặt và máy ở cùng mạng WiFi.
2. Bấm nút **"Lấy kết quả từ máy"** (góc dưới). App tải **kết quả lần chạy gần nhất** và lưu thành 1 bản ghi.
3. Danh sách hiện các bản ghi (thời gian, mã máy, tóm tắt số slot Dương/Âm/Lỗi). Bấm vào một bản ghi để
   xem **chi tiết** (xem mục 7).
4. Xoá: bấm biểu tượng thùng rác ở mỗi bản ghi, hoặc **Xoá tất cả** trên thanh tiêu đề.

> Lịch sử ở tab này lưu **trên máy tính này**. Máy chỉ trả về lần chạy mới nhất, nên mỗi lần muốn lưu một
> kết quả mới bạn bấm "Lấy kết quả từ máy" sau khi máy chạy xong.

---

## 5. Tab "Cloud" — lịch sử nhiều máy

1. Mở tab **Cloud**. Lần đầu có thể chờ ~30–60 giây để tải danh sách máy (các lần sau hiện gần như tức thì).
2. **Danh sách máy**: mỗi dòng hiện **mã máy**, **số lần chạy**, **FW** (phiên bản firmware) và **lần mới nhất**.
   - **Tìm kiếm**: gõ mã máy vào ô tìm.
   - **Sắp xếp** (nút ⇅): Mới nhất · Cũ nhất · Mã máy A→Z · Mã máy Z→A.
   - **Làm mới** (nút ⟳): lấy dữ liệu mới nhất.
3. Bấm một **máy** → danh sách **các lần chạy** của máy đó (10 lần/trang; dùng **Trước/Sau** để xem thêm).
4. Bấm một **lần chạy** → xem **chi tiết + đồ thị CT** (mục 7).
5. **Đồng bộ về máy** (nút ⬇ trong màn lịch sử của máy): tải các lần chạy của trang hiện tại (kèm đường
   cong) về **lịch sử cục bộ** để xem offline ở tab Lịch sử.

---

## 6. Tab "Log nhiệt" — theo dõi nhiệt độ realtime

Dùng để xem nhiệt độ các bộ phận gia nhiệt của máy theo thời gian thực. **Cắm máy vào máy tính bằng cáp
USB** trước.

### Bắt đầu xem

1. Mở tab **Log nhiệt**. Các cổng **COM** hiện ra dưới dạng nút bấm. *(Nếu chưa thấy, bấm **Làm mới cổng**.)*
2. **Bấm một cổng COM** → app mở cổng đó và vẽ **đồ thị nhiệt realtime** của 6 kênh:
   **Lysis · Amp1 · Amp2 · Hotlid1 · Hotlid2 · Ambient** (mỗi kênh một màu, kèm giá trị hiện tại).
3. Có thể **bật nhiều cổng cùng lúc** (chúng đọc ngầm). Bấm cổng nào thì xem đồ thị cổng đó (cổng đang xem
   có **viền màu + ▶**). Bấm nút **⊞ (Tất cả đồ thị)** để xem **đồng thời tất cả** đồ thị các cổng đang chạy.

### Các thao tác trên một cổng

- **Đặt tên gợi nhớ**: **giữ** nút cổng (hoặc bấm ✏ cạnh tiêu đề đồ thị) → nhập tên như "Máy Lysis" →
  hiện "Máy Lysis (COM3)" cho dễ phân biệt.
- **Lưu log**: lưu toàn bộ dữ liệu thành file (xem mục 8).
- **Lưu đồ thị**: chụp đồ thị hiện tại thành ảnh PNG.
- **Xóa dữ liệu**: xoá dữ liệu đang vẽ (bắt đầu lại).
- **Dừng cổng**: ngừng đọc cổng đó. (**Dừng tất cả** ở thanh tiêu đề để ngừng mọi cổng.)
- **Gửi lệnh**: nếu mở cổng mà **không thấy dữ liệu**, app sẽ **tự gửi lệnh** sau ~3 giây; bạn cũng có thể
  bấm **Gửi lệnh** thủ công để máy bắt đầu xuất nhiệt.

### Xem lại dữ liệu đã lưu

Bấm nút **⌛ (Đã lưu)** trên thanh tiêu đề:

- **Log đã lưu** → danh sách các phiên đã lưu → bấm để **vẽ lại đồ thị** + bảng thống kê (min/max/cuối).
- **Đồ thị đã lưu** → các ảnh PNG đã lưu → bấm để xem to (phóng to/kéo).

### Tự kết nối lại

Nếu **tạm mất kết nối** (rút nhầm cáp, nhiễu…), app **tự nối lại** và **vẽ tiếp** với nhiệt độ kế tiếp;
trong lúc đó thẻ hiện biểu tượng cam + "đang kết nối lại…". Cắm lại cáp là tự chạy tiếp.

### Đọc đồ thị nhiệt

- Trục ngang = **thời gian (phút)**.
- Trục dọc = **nhiệt độ (°C)**, từ −10 đến (nhiệt độ cao nhất + 10), có vạch chính và vạch phụ.
- Đồ thị **tự co giãn theo kích thước cửa sổ** — kéo to cửa sổ để xem rõ hơn.

---

## 7. Đọc kết quả xét nghiệm & đồ thị CT

Khi bấm vào một lần chạy (ở tab Lịch sử hoặc Cloud), màn **chi tiết** hiện:

### Kết quả bệnh

Lưới **10 slot**, mỗi slot hiện **phân loại** (theo màu) và **giá trị CT**:

| Màu | Phân loại |
|---|---|
| 🔴 Đỏ | **Dương tính** |
| 🟠 Cam | **Dương tính nhẹ** |
| 🟢 Xanh lá | **Âm tính** |
| ⚫ Xám | **Lỗi** |
| – | Không rõ |

### Đồ thị CT

- Vẽ **10 đường cong khuếch đại**, trục ngang = phút, trục dọc = tín hiệu huỳnh quang.
- **Bật/tắt từng slot** bằng các chip ở dưới; nút **Hiện tất cả / Ẩn tất cả**.
- **Với lần chạy từ Cloud**, có thể chọn **4 dạng đường cong** (nút **Raw · Calib · Baseline · SG**) và nút
  **"Xem cả 4 đồ thị"** để so sánh cùng lúc.

---

## 8. Dữ liệu được lưu ở đâu

- **Lịch sử (tab Lịch sử & Đồng bộ cloud)**: lưu trong app trên máy tính này.
- **Log nhiệt**: lưu tại thư mục **`Tài liệu (Documents)\FBT_RAPID_templog\`**:
  - `logs\` — file `.json` (xem lại trong app) và `.csv` (mở bằng Excel).
  - `charts\` — ảnh đồ thị `.png`.
  - Tên file đặt theo **tên gợi nhớ** của cổng (nếu đã đặt). Các màn "đã lưu" có nút **Mở thư mục**.

---

## 9. Xử lý sự cố (FAQ)

| Hiện tượng | Cách xử lý |
|---|---|
| **"Windows protected your PC"** khi mở app | Bấm **More info → Run anyway** (app chưa ký số). |
| **Không lấy được kết quả từ máy** (tab Lịch sử) | Kiểm tra **IP máy** đúng chưa; máy và máy tính **cùng mạng WiFi**; bấm **Kiểm tra kết nối**. |
| **Tab Cloud tải lâu / lỗi** | Lần đầu (~30–60s) là bình thường. Kiểm tra máy tính có **Internet**; bấm **Làm mới**. |
| **Tab Log nhiệt không thấy cổng COM** | Cắm cáp USB; bấm **Làm mới cổng**. Có thể cần cài driver USB-COM của máy. |
| **Mở cổng nhưng không có nhiệt độ** | Đợi ~3s (app tự gửi lệnh) hoặc bấm **Gửi lệnh**. Kiểm tra đúng cổng COM của máy. |
| **"Cổng đang bận"** | Cổng đang bị chương trình khác giữ (vd. SerialDebug). Đóng chương trình đó rồi bấm lại. |
| **Mất kết nối khi đang đọc nhiệt** | App tự kết nối lại; cắm lại cáp là chạy tiếp. |

---

*Phiên bản app hiển thị ở màn hình máy/About. Mọi thắc mắc kỹ thuật liên hệ bộ phận kỹ thuật.*
