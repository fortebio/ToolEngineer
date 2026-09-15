# 2026-08-21 — ASF phá vỡ giả định của quy tắc hình dạng (shape rule)

Ghi lại kết quả 18–21/08. Không có thay đổi code nào trong lần này; đây là **bằng chứng
để KHÔNG triển khai v2.4.3AT** cho tới khi ngưỡng `min_sharpness` biết phân biệt theo
từng xét nghiệm.

## Kết luận ngắn

`min_sharpness = 8.0` được hiệu chỉnh trên động học LAMP của **tôm**, nơi mẫu dương
chạy ở sharpness 20–60. **ASF không có động học đó.** Trên một đĩa ASF có đối chứng
qPCR, toàn bộ 10 giếng nằm trong khoảng sharpness **4.0–8.4**, và v2.4.3AT sẽ gắn cờ
`F` cho **8 mẫu dương thật đã được qPCR xác nhận**.

## Bằng chứng

Đĩa ASF trên RPL03007 (15/50/75/100 cp trên hạt bead, kèm PC), chạy song song trên
máy qPCR.

| mẫu | qPCR Cq | v2.4.3 hôm nay | v2.4.3AT | sự thật |
| --- | --- | --- | --- | --- |
| PC swine E1 | 6.30, 6.04 | P / P | **P / F** | dương |
| 15 cp | 15.09, 15.44 | S / N | **F / N** | dương |
| 50 cp | 12.76, 13.94 | P / P | **F / F** | dương |
| 75 cp | 11.90, 13.93 | P / P | **F / F** | dương |
| 100 cp | không chạy qPCR | P / P | **F / F** | dương (suy ra) |

- Firmware cũ: **9/10** báo là phát hiện, 1 bỏ sót.
- v2.4.3AT: **1/10** báo là phát hiện, 8 gắn cờ, 1 bỏ sót.
- NTC của qPCR sạch (Cq 37.61), nên đây không phải nhiễm.

**Ct thì đúng, sharpness thì vô nghĩa.** Ct của RAPIDPlus bám sát Cq của qPCR ở
`r = 0.993` (bỏ PC ra, n=3, để PC không kéo đường hồi quy). Trong khi đó sharpness
trải vỏn vẹn 4.0–8.4 trên một dải nồng độ rộng 9 chu kỳ qPCR — nó không phân biệt
được gì cả.

Cả hai máy đều tách được 15 cp khỏi phần còn lại và **cả hai đều không phân giải nổi
50 / 75 / 100** — đó là vấn đề thiết kế dải pha loãng, không phải vấn đề của máy.

## Quét toàn bộ dữ liệu từ 14/08

1 130 giếng, 98 lượt chạy, đọc thẳng từ file export của Google Sheet:

- **23 giếng đổi chữ (2.4%)**: 14 → `F`, 4 → `N` do sửa climb, 2 → `N` do cắt còn 30 phút
  (đều là artefact cuối run, cắt đi là đúng), 3 → `N` là **phản ứng muộn thật bị mất**
  (đều trên `proto 1`), 1 → `S`→`P` là sửa đúng một ca đọc sai.
- **Không giếng nào được thêm phát hiện.**
- 15 giếng bị gắn cờ: 7 ở slot 8, 3 ở slot 3.

Slot 8 **không** hỏng: khi slot 8 chứa mẫu không phải TPD (đĩa extraction), sharpness
trung vị của nó là 24.8, đúng bằng mức chung 24.3. Slot 8 chỉ nông khi nó chứa TPD.
Tức là **do xét nghiệm, không phải do khe**.

## Độ ổn định của tiêu chí gọi kết quả

- 26 cặp lặp trên 8 đĩa: **100% cùng chữ**.
- 957 giếng: chỉ **7 giếng (0.7%)** nằm trong phạm vi 5% của một ngưỡng.
- Chạy lại toàn bộ thuật toán với slope lệch ±10/20/30%: **0.5% / 1.5% / 3.1%** số
  giếng đổi kết quả. Gần như mọi thay đổi đều đi vào hoặc ra khỏi `F`; P và N hầu như
  không hoán đổi trực tiếp.

Quyết định Dương/Âm rất ổn định. Phần dao động là cái cờ `F` — đúng chỗ nên dao động.

## Điểm mù của `find_first_climb()` (đã ghi nhận, chưa sửa)

Bước nhảy được so với **biên độ thô** của 8 điểm trước đó. Trên đường cong đang lên,
chính cái dốc đó thổi phồng "nhiễu", nên phép thử 4× trở thành ngưỡng 36–46 RFU thay
vì 8 RFU. Nghĩa là cơ chế sửa artefact yếu nhất đúng ở nhóm đường cong lên từ từ —
tức là đúng nhóm đang bị gắn cờ.

Hướng sửa: **khử xu hướng (detrend)** 8 điểm đó trước khi lấy biên độ. Phải kiểm lại
trên toàn bộ kho 25 000 đường cong trước khi đưa lên máy.

## Việc phải làm trước khi v2.4.3AT chạm tay khách

1. **Không cài v2.4.3AT lên máy chạy ASF.** Đĩa trên là bằng chứng trực tiếp.
2. Ngưỡng sharpness phải **theo từng bệnh**. Máy đã biết bệnh của từng khe
   (`slotNames`, NVS namespace `slotlabels`) nên làm được bằng hằng số biên dịch —
   không thêm field vào `parastructure` (đang 400 B / trần 402 B).
   Con số tham chiếu: ASF cần ngưỡng khoảng **5** (mẫu dương thật thấp nhất là 5.4);
   tôm giữ **8**.
3. Lấy sự thật (qPCR) cho các giếng TPD bị gắn cờ, đặc biệt RPL02001 — riêng máy này
   chiếm 6/15.
4. Cân nhắc: nếu ASF không plateau trong 30 phút thì có thể vấn đề là **độ dài run**,
   không phải ngưỡng. Chạy thử một đĩa ASF ở 120 vòng để kiểm tra.

## Ghi chú phương pháp

Header trong sheet nằm **phía trên** hàng Amplification của chính nó (Slopes/Origin/
LED rồi mới tới Amplification, metadata nằm trên hàng Amplification). Đọc ngược lại
sẽ gán nhầm slope của lượt chạy khác — mọi giá trị increase/sharpness sai theo hệ số
1.3–1.9×. Ct **không** bị ảnh hưởng vì Ct là thời gian, không chia cho slope.
