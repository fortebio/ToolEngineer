# QR ở chế độ STA mã hoá `.local` thay vì IP (2026-08-04)

## Vấn đề người dùng thấy

> "tại sao tôi có mDNS rồi mà khi quét QR vẫn là 192.168…"

Vì QR và dòng chữ dưới nó là **hai chuỗi khác nhau**, và mDNS chỉ chạm vào dòng chữ:

| | Trước | Sau |
| --- | --- | --- |
| **Payload QR** (`displayLCD.cpp:276`) | `http://<ip>/` | `http://<hostname>.local/` |
| **Caption in ra** (`displayLCD.cpp:308`) | `http://<hostname>.local/` | `http://<ip>/` |

Màn QR có trước (2026-07-22), mDNS thêm sau (2026-07-24) và chỉ sửa caption — dòng payload chưa
bao giờ được xem lại. Không phải quyết định, chỉ là sót.

## Vì sao ĐỔI CHỖ chứ không chỉ đổi payload

Đây là phần dễ làm sai nhất, nên nói rõ.

`.local` **chỉ phân giải nếu phía CLIENT nói mDNS**. iOS/macOS/Windows 10+ có sẵn; Android đời cũ
thì không. Máy **không có cách nào biết** trình duyệt bên kia có phân giải được hay không — nó chỉ
vẽ ra một mã.

Nên IP không được biến mất cùng lúc: nó là **đường vào duy nhất** cho mấy máy đó. Và nó phải ở dạng
**đọc để gõ tay**, chứ không phải dạng quét — vì lý do người ta phải đọc nó chính là **quét không
vào được**. Hai dạng trên cùng một màn: cái để quét, và cái để gõ khi quét hỏng.

Đổi mỗi payload (bỏ IP đi) thì đúng những máy Android cũ sẽ **không còn đường nào** vào dashboard —
lỗi vô hình, chỉ lộ ngoài hiện trường.

## Vì sao trước đó tôi khuyên giữ IP (và vì sao đổi vẫn ổn)

Lý do mDNS ra đời là "DHCP đổi IP nên link cũ chết" — lý do đó **không áp dụng cho QR**, vì
`screen_QR()` dựng lại payload **mỗi lần vào màn**, không có mã nào được lưu để mà hết hạn.

Cái `.local` thật sự đem lại ở đây là: **đọc lên và nhớ được** (`rpl03010.local` vs
`192.168.1.147`), và giống hệt địa chỉ đang dùng trên laptop. Đổi lại là rủi ro client không phân
giải — đã được caption IP che.

## Biên payload: cửa tràn stack thứ hai

`ricmoo/QRCode` version 3 / ECC_LOW chứa **53 byte** và **không tự kiểm** — `bb_appendBits()` không
bounds-check, đích là VLA trên stack DisplayTask. Quá 53 B = panic ngay lúc mở màn QR.

IP **tự giới hạn**: một chuỗi IPv4 không thể quá 15 ký tự. Hostname thì **không** — nó dựng từ
device ID, nên đường tràn đó vừa có thêm một cửa vào.

Worst case mới: `len("http://") + clamp 24 + len(".local/")` = **7 + 24 + 7 = 38 B** ≤ 53. Clamp
nằm ở `dashboardHostname()` (`out.length() < 24`); thực tế `device_id` là `char[10]` nên ≤ 9 ký tự,
nhưng guard tính theo **clamp**, không theo giả định về nguồn.

## Guard

`tools/test_qr_payload.py` thêm **mục 1b**:

1. Chặn biên số học của payload STA theo clamp của `dashboardHostname()`.
2. `screen_QR()` phải **vừa** mã hoá `http://<dashboardHostname()>.local/` **vừa** còn in
   `localIP()`. Đây là chỗ ghim bất biến "hai dạng địa chỉ" — gộp caption về `.local` nữa trông
   **giống một cú dọn dẹp**, nên nó phải là assertion chứ không phải comment.

### `code_only()` từng tự làm mù chính nó

Viết xong mục 1b thì nó **đỏ ngay trên code đúng**. Nguyên nhân: `code_only()` bóc comment bằng
regex `//[^\n]*`, mà `payload = "http://" + dashboardHostname() + ".local/";` bị cắt còn
`payload = "http:` — **hai gạch chéo của chính scheme URL** bị đọc là comment. Guard nhìn thấy
"không có payload nào cả".

Sửa gốc: `code_only()` nay **duyệt ký tự và biết string literal** (bỏ qua `//`, `/* */`, nhưng chép
nguyên nội dung trong nháy, kể cả escape). Không vá riêng mục 1b, vì:

- lỗi này **im lặng theo hướng xanh** ở dạng khác: một check viết trên literal có chứa `//` sẽ
  **pass vì guard tự mù**, không có gì báo.
- quét literal prefix `"RAPID-"`/`"FBT-"` ở mục 2 **cần literal còn nguyên** — CLAUDE.md đã ghi
  đúng bẫy này một lần rồi (`test_device_id.py`).

### Negative test (5/5 đỏ đúng check)

| Gieo lỗi | Guard báo |
| --- | --- |
| payload quay về IP | `no longer encodes` |
| caption gộp về `.local` luôn | `no longer prints the IP` |
| nới clamp hostname lên 60 | `worst-case STA payload` |
| bỏ hẳn clamp | `does not clamp` |
| lén thêm literal `"FBT-"` (code thật) | `SSID prefix literal` |

Lần gieo thứ 5 **đầu tiên không đỏ** — vì tôi đặt literal trong một `//` comment, mà scan đó **cố
tình** bỏ qua comment. Seed sai, không phải guard hỏng; sửa thành code thật thì đỏ. Ghi lại vì đây
đúng là kiểu "negative test xanh nên tưởng guard chạy".

## File đã sửa

| File | Việc |
| --- | --- |
| `src/displayLCD.cpp` | `screen_QR()`: đổi chỗ payload ↔ caption; cập nhật doc-header |
| `tools/test_qr_payload.py` | mục 1b; `code_only()` biết string literal; docstring + dòng in kết quả |
| `CLAUDE.md` | mục QR: bất biến hai dạng địa chỉ |

## Kiểm chứng

- `python tools/test_qr_payload.py` → `AP 46 B, STA 38 B <= 53 B` ✓
- `test_device_id.py`, `test_phase0_guards.py`, `test_web_assets.py`, `test_no_method_branch.py` ✓
- Negative test 5/5 ✓
- Build `pio run -e esp32dev` ✓

**Còn phải nhìn trên máy thật**: quét QR bằng iPhone (phải vào thẳng `http://<id>.local/`), quét
bằng một máy Android cũ (nếu không vào được thì **gõ IP dưới màn** phải vào được), và kiểm dòng
caption không tràn khỏi 320 px. Mock không dựng được TFT.

## Soát chéo 4 hướng — 18 mục nêu ra, 3 sống sót

Chạy 4 agent độc lập (lan toả tài liệu · lỗi runtime trên máy · bố cục TFT · chất lượng guard), mỗi
phát hiện qua một vòng **phản biện có nhiệm vụ bác bỏ**. 15 mục bị bác — đáng chú ý là mấy mục nghe
rất hợp lý: "hai máy trùng nhãn mDNS thì QR mở nhầm dashboard máy khác" (trùng nhãn nghĩa là trùng
device ID, vấn đề đã có từ trước và không do QR), "`MDNS.begin()` chỉ thử một lần mỗi boot" (đúng về
code nhưng hệ quả nó dự đoán không xảy ra vì caption IP vẫn ở đó), và "`screen_QR()` tính lại một
cái tên đã latch — đúng cái bug SSID mà chính hàm này từng sửa" (khác nhau ở chỗ đổi ID **luôn kéo
reboot hoãn**, nên cửa sổ lệch và cửa sổ quét được QR rời nhau).

Ba mục còn lại, đều **có sẵn từ trước**, đều đã sửa trong lượt này:

1. **`line1 = "No network"` là dead store** (`displayLCD.cpp`). Hai đường không-mạng đều gán chuỗi
   đó rồi **không bao giờ vẽ**, vì lệnh in nằm trong `if (payload.length())`. Máy hiện ra màn hình
   trơn: không QR, không địa chỉ, **không lý do** — đúng trạng thái người vận hành cần được nói cho
   biết nhất. Sửa: nhấc phần in caption ra **ngoài** guard, in `line1 + ": "` khi có payload và
   `line1` trần khi không.
2. **Comment bố cục sai sự thật**: "Caption column to the right of the code (x >= 205) ... fits
   115 px" trong khi caption là **một dòng ở (20, 40)**, nằm **trên** mã QR (panel bắt đầu y=65) và
   có gần trọn 320 px. Chưa bao giờ đúng. Nguy hiểm vì đó chính là comment người sau tin khi cân
   nhắc "caption dài hơn có vừa không".
3. **`docs/architecture/06-mang-va-upload.md:34`** ghi "**`screen_QR()` phải gọi cùng hàm đó**"
   (`dashboardApName()`) — điều mà `test_qr_payload.py` mục 2 **cấm** từ 2026-07-30. Ai làm theo doc
   sẽ bị guard đánh đỏ. Đã sửa thành "builder nuôi radio, ai báo cáo thì đọc `softAPSSID()`", và bổ
   sung dòng STA (trước đó doc kiến trúc **không mô tả payload STA** — đúng nửa vừa đổi).
