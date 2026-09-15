# Đánh giá khả năng áp dụng vào thị trường — FBT (RPL)

> Lập 2026-07-13 qua phân tích đa góc nhìn (kiến trúc/mở rộng, domain y sinh & giá trị dữ liệu, mô hình kinh doanh, bảo mật–pháp lý, vận hành) + 1 vòng red-team phản biện có kiểm chứng trực tiếp trên kho log thật (`data/`, `data/Log/`). Đây là đánh giá chiến lược, KHÔNG thay kế hoạch kỹ thuật [KE_HOACH_PHAT_TRIEN.md](KE_HOACH_PHAT_TRIEN.md).

## Kết luận 1 dòng
**Chưa ra thị trường được — và cái server này không phải phần khó.** Đây là một **pilot phần cứng** (đầu đọc khuếch đại acid nucleic 10 kênh cầm tay, lớp qPCR/LAMP) khoác câu chuyện "nền tảng dữ liệu" mà chính dữ liệu chưa đỡ nổi. Còn cách thị trường thật **nhiều năm và nhiều vốn PHI‑phần‑mềm**.

## Bản chất thật của sản phẩm
- Về kỹ thuật: **máy đọc khuếch đại thời gian thực 10 slot** + một **backend nội bộ đơn‑tenant** (~262 dòng FastAPI) để hứng và duyệt tín hiệu thô. Máy **trung lập với tác nhân** — chẩn đoán gì tùy **KIT** nạp vào.
- Mô hình thương mại đúng: **"dao cạo & lưỡi dao"** — MÁY (dao) + **KIT tiêu hao** (lưỡi) cho một xét nghiệm cụ thể. Moat nằm ở **máy + hóa chất + đăng ký pháp lý + tập khách**, **KHÔNG** ở phần mềm server.
- Server **KHÔNG** phải "nền tảng dữ liệu" (ghi tín hiệu mà không ghi ý nghĩa) và **KHÔNG** phải "SaaS bán được" (1 token tĩnh, không multi‑tenant/billing/audit).

## Nghịch lý cốt lõi (phát hiện từ red-team)
Server là **phần DUY NHẤT đã đủ tốt**, đồng thời là **<10% khối lượng** để ra thị trường. 90% vốn và công thật nằm ở **ba thứ không có một dòng nào trong repo**:
1. **Validation hiệu năng assay** — độ nhạy / đặc hiệu / LoD / phản ứng chéo so với PCR đối chứng. Đây là câu hỏi **DUY NHẤT** người mua chẩn đoán và cơ quan quản lý hỏi. Repo: **zero** bằng chứng.
2. **Sản xuất + đăng ký KIT** — "lưỡi dao", tức sản phẩm bán thật, **chưa tồn tại**. Thú y/thủy sản vẫn phải đăng ký lưu hành **Cục Thú y (Luật Thú y 2015)**; IVD‑người thì theo **NĐ 98/2021** (+ có thể ISO 13485/SaMD).
3. **Khách TRẢ TIỀN thật** — chưa có 1 đơn hàng / lần bán kit nào trong repo.

→ Đừng đánh bóng server thành SaaS/HA/đa‑tenant lúc này. Đó là **tối ưu sớm cho đúng phần đã ổn**, đúng cái bẫy mà cả 5 lăng kính lẫn KE_HOACH dễ dụ vào.

## "Tài sản dữ liệu 3.372 phiên" — là marketing, chưa phải tài sản
Kiểm chứng trực tiếp trên ~3.660 file thật:
- `kitId = "0.00"` ở **80,4%** file (2.944/3.660); phần còn lại là placeholder rác (`"10.00"`×428, `"10987654321.12"`…) → **không một lô kit thật nào** trong toàn dataset.
- **0 nhãn target/pathogen/assay** ở bất kỳ đâu trong payload; **0 ground‑truth** (không kết quả PCR/nuôi cấy đối chứng).
- File mẫu chuẩn của cả dự án (`data_RPL.json`) là run **chưa hiệu chuẩn** (slopes toàn =1, origins toàn =0); lẫn nhiều run **demo/N_A**.
- **"88 thiết bị"** là fleet **pilot nội bộ** (serial tuần tự RPL0x + máy mẫu + lệnh CURL thử), không phải traction/khách trả tiền.
- Server còn **vứt bỏ chính thời điểm đo**: 2.449/3.467 payload **có** trường `time` nhưng bị bỏ qua (`import_drive_logs.py:4` còn khẳng định sai là "payload không có mốc thời gian") → dấu hiệu đội ngũ chưa thật hiểu dữ liệu mình thu.
- Firmware **tự tính verdict N/P** là hộp đen, **~26–28 phiên bản không kiểm soát** (case‑collision `V2.3.6`/`v2.3.6`, hậu tố `_loop/_new/.x/.T/vtest`) → 3.372 bản ghi **không đồng nhất**.

## Con đường ra thị trường (xếp theo mức phù hợp)
| Con đường | Phù hợp | Công sức | Ghi chú |
|---|---|---|---|
| **Máy + KIT cho 1 vertical thủy sản** (tôm: WSSV / AHPND‑EMS / EHP) | Cao | Rất cao | Đúng nhất. Nhu cầu thật (VN top‑3 XK tôm ~3,5–4 tỷ USD/năm). Nhưng phải validate assay + sản xuất/đăng ký kit (Cục Thú y) + đấu với kẻ đến trước **GeneReach POCKIT / IDEXX** đã bán khắp trại tôm VN. |
| **Dịch vụ đo tại trại của chính FBT** (chưa bán server) | Trung bình | Trung bình | Thực tế nhất cho 1 người trong 6–12 tháng: dùng fleet để thu **dữ liệu thật có nhãn** + kiểm chứng mức sẵn lòng trả. Server hiện tại đủ dùng sau khi vá 3 lỗ. |
| **Appliance on‑prem** (máy + hộp backend đóng gói) cho lab/trại lớn | Trung bình | Cao | Né SPOF/HA và multi‑tenant (mỗi khách 1 hộp, cách ly tự nhiên). Vẫn cần kit đăng ký + hỗ trợ. |
| **SaaS "nền tảng dữ liệu" đa‑tenant bán cho bên thứ ba** | Thấp | Rất cao | **KHÔNG nên.** Không có moat phần mềm; Thermo Connect / Cepheid C360 / QIAsphere mạnh hơn nhiều bậc. Là cái bẫy tối ưu sớm. |

## Blocker xếp hạng
**Blocker tuyệt đối (phần khó thật — phi phần mềm):**
1. Không có validation hiệu năng assay.
2. Kit hóa chất chưa tồn tại (chưa định nghĩa assay/target).
3. Đăng ký pháp lý chưa xác định/chưa làm.
4. Secret quản trị (root/engineer/RECEIVER_TOKEN/khóa SSH) **đang lộ plaintext trong OneDrive**, hoãn xoay → ai có OneDrive = toàn quyền SSH.

**Major (phần server + tổ chức):**
5. Traction thật chưa kiểm chứng (fleet nội bộ ≠ khách trả tiền; thiếu unit economics: COGS máy, COGS+giá kit, giá/test).
6. Firmware verdict N/P là hộp đen chưa validate/chưa versioned.
7. Server thiếu multi‑tenant + **auth fail‑open khi token rỗng** (`logic.py:17-18`) trong khi bind `0.0.0.0` + public qua Funnel; đường GHI không có provenance/xác thực per‑device (kết quả dương/âm **giả** có hệ quả pháp lý: hủy ao / thông quan tôm bệnh).
8. Vận hành dưới ngưỡng SLA: không backup đã‑test‑restore, không cảnh báo (MTTD vô hạn), SPOF mọi tầng, mất nguồn chưa test + không UPS, `write_text` không fsync (`main.py:56`). Bus‑factor = 1.
9. Server bỏ qua metadata biến tín hiệu thành ý nghĩa (target/kit‑lot/sample‑id/location/operator/control; trường `time`).

## Roadmap (thực tế với 1 người + 1 MiniPC)
- **GĐ0 — Vệ sinh & quyết định (2–4 tuần, gần như miễn phí).** Xoay TẤT CẢ secret + gỡ khỏi OneDrive. Vá auth fail‑open → fail‑closed. **Xác định dứt điểm đối tượng chẩn đoán** (người / thú y / thủy sản) + **chốt 1 vertical duy nhất** (đề xuất: **tôm**). Bắt đầu **GHI metadata tại FIRMWARE** (kit‑lot thật + sample‑id + target + slot control); promote `time` → `measured_at`. **DỪNG** mọi ý định gold‑plate server.
- **GĐ1 — Chứng minh xét nghiệm THẬT phát hiện được (3–9 tháng, vốn wet‑lab).** Định nghĩa panel assay/target. Nghiên cứu hiệu năng phân tích vs PCR đối chứng (có ground‑truth). Chuẩn hóa + validate + khóa 1 version firmware cho production. Thu **dataset thật‑có‑nhãn** qua dịch vụ đo tại trại.
- **GĐ2 — Kit + pháp lý + kiểm chứng thị trường (6–18 tháng).** Sản xuất/OEM kit (mồi/enzyme/mix đông khô + cold chain + hạn dùng + QC lô‑đối‑lô). Đăng ký lưu hành (Cục Thú y hoặc IVD‑người). Có **unit economics thật**. Ký được **vài khách trả tiền**.
- **GĐ3 — Cứng hóa vận hành (CHỈ khi đã có khách trả tiền).** Backup+test restore; dead‑man alert + healthcheck; BIOS power‑on + UPS; fsync/chính thức hóa Google Drive là nguồn durability; siết ACL Tailscale; multi‑tenant + tách read/write + audit **chỉ khi bán cho khách thứ hai** (hoặc chọn appliance on‑prem để né hẳn); dời ingress+DB lên hạ tầng có SLA nếu cam kết uptime thật.

## Ba việc server đáng làm NGAY (vài ngày, trước khi có khách thật)
1. **Xoay secret đã lộ** + gỡ khỏi OneDrive — bắt buộc, gần như miễn phí.
2. **Vá auth fail‑open → fail‑closed** — vài dòng ở `logic.py:17-18` (từ chối chạy public nếu chưa đặt token).
3. **Bắt đầu ghi kit‑lot + sample‑id + target NGAY TẠI FIRMWARE** (không phải server) để dữ liệu **tương lai** có nghĩa; dữ liệu cũ coi như bỏ.

## Chốt thẳng thắn
Đừng đầu tư thêm một giờ nào biến server thành SaaS/HA/đa‑tenant lúc này. Dồn toàn bộ nguồn lực vào: **chốt 1 vertical (tôm) → validate xét nghiệm → kiểm chứng có ai trả tiền cho KIT hay không.** "Tài sản dữ liệu 3.372 phiên" hiện là marketing, chưa phải tài sản.
