# Kịch bản Ct < 4: sàn Ct 3,0 chưa tới được sigmoid thường — tay trái vẫn bị kẹp ở 3,67′

Ngày 2026-09-15, nhánh `v2.4.5at`, tiếp nối
[2026-09-15-kich-ban-mo-phong-danh-gia-ket-qua.md](2026-09-15-kich-ban-mo-phong-danh-gia-ket-qua.md)
và đo lại điều [2026-09-11-dieu-kien-tra-error-tang-som.md](2026-09-11-dieu-kien-tra-error-tang-som.md)
ghi là *"chưa chạy máy thật"*.

## Vì sao

Kane: *"tập trung thêm kịch bản liên quan tới ct<4"*. Vùng này là nơi `d7775b1` đổi hành vi (sàn Ct
4,0 → 3,0, dựa trên 15 giếng ERP có Ct 3,0–3,7 mà người duyệt lật sang P, 12/15 là chứng dương), và
catalogue cũ chỉ chạm nó bằng 5 giếng của S05 — trong đó hai giếng `F` lý do 2 phải dựng bằng hình
dạng hai pha mới kích được. Chưa có thang nào đo **máy trả gì cho một dương thường có Ct 3,0–3,6**.

## Trước

Trong `find_sigmoidal_feature()` (`Alg/Algo.cpp:771-774`) tay trái được tìm ngược từ đỉnh nhưng
**không được đi quá `discard_index − 1`** = chỉ số 11 = **3,67′** ở 20 s/vòng. `d7775b1` hạ sàn Ct
xuống 3,0 nhưng không đụng kẹp này. Với sigmoid logistic, tay trái (50 % đỉnh đạo hàm) nằm **sau**
điểm Ct (40 %) đúng `0,3/k` phút — tức 0,1–0,4′ với k 0,8–3. Vậy một dương thường có Ct < ~3,4 có tay
trái trước 3,67′ → `detected_shape()` sai → **`E`** ("phân tích không chạy được"), không phải `P`;
dưới 3,0 cũng `E`, không phải `F` lý do 2. Sàn 3,0 chỉ có tác dụng với những đường có tay trái *sau*
3,67′ mà Ct *trước* 3,67′: sườn rất thoải (k ≤ 0,7) hoặc hai pha. Mirror khẳng định trước, máy khẳng
định sau (dưới).

## Nay

### Ba kịch bản mới (`tools/sim_cases.py`, catalogue 11 → 14, 110 → 140 giếng)

- **S10_ct_floor_ladder** — thang Ct × độ dốc của chứng dương (A 140, k 1,5, nền 380, warm-up 40):
  Ct báo 2,33 · 2,67 · 3,00 · 3,33 · 3,67 · 4,00; cùng Ct 3,00–3,33 ở k 2,0 và 3,0; và một sườn thoải
  k 0,6. Mỗi giếng 35/35 trên mirror (7 seed × 5 slope).
- **S11_ct_floor_field** — cùng vùng với những gì hiện trường thêm vào: nhiễu σ3, warm-up 250, creep
  sau plateau, **bậc đồng bộ 3,0′ +50 của chính RPL01015** đặt lên sườn sớm (vá / không vá), xung
  +40 hai vòng, hai pha dẫn nhập yếu, quá độ ấm quang học đơn thuần.
- **R03_real_positive_shifted** — dương THẬT duy nhất trong repo (`tools/slots.txt` slot 6, Ct 4,33,
  rise 107) **dịch sớm 0…7 vòng**: cùng một hình dạng thật với nhiễu và warm-up của nó, Ct 4,33 →
  2,0. Cộng hai âm thật cũng dịch.

Ba trường `expect` / `known` / `ct` được dùng đúng nghĩa: `expect` là ý đồ của `d7775b1` (Ct ≥ 3,0
có phản ứng thật → P; Ct < 3,0 → F lý do 2), `known="E"` là điều máy trả hôm nay. Report đánh
KNOWN-WEAK, không đậu không rớt — sửa xong thì cột này tự đổi màu.

### Runner

- `--only` nhận nhiều id cách nhau bằng dấu phẩy (`--only S10,S11,R03`).
- **`--regrade` khớp section với kịch bản bằng echo**, không bằng thứ tự: máy in lại nguyên văn từng
  message nạp, nên mỗi section getResult được gán cho kịch bản có nhiều giếng trùng byte nhất. Log
  viết trước khi catalogue thêm/bớt/đảo thứ tự vẫn chấm được; section không trùng kịch bản nào (review
  của lượt restore, hoặc kịch bản đã đổi hết 10 recipe) được báo và bỏ qua; kịch bản không có trong
  log được liệt kê. Cần thiết vì S10/S11 chen vào **trước** R01/R02 — chấm theo thứ tự thì log 12:01
  và 16:11 gãy ngay.

## Bằng chứng — RPL01015, 15/09/2026 16:48

[`docs/reports/simcases/2026-09-15-1648-RPL01015.md`](../reports/simcases/2026-09-15-1648-RPL01015.md)
(3 kịch bản, log serial cùng tên). **30 giếng: 20 PASS · 0 FAIL · 10 KNOWN-WEAK**, máy và mirror
**cùng chữ 30/30 và cùng Ct tới từng vòng**.

| Ct máy báo → | 2,00 | 2,33 | 2,67 | 3,00 | 3,33 | 3,67 | 4,00 | 4,33 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Chứng dương k 1,5 (S10) | | **E** | **E** | **E** | P | P | P | |
| Chứng dương k 2,0 / 3,0 (S10) | | | | **E** (k2) | **E** (k3) | P (k3) | | |
| Sườn thoải k 0,6 (S10) | | | | | P | | | |
| Dương THẬT slots.txt dịch sớm (R03) | **E** | **E** | **E** | **E** | **E** | P | P | P |
| Ý đồ `d7775b1` | F/2 | F/2 | F/2 | P | P | P | P | P |

Đọc ra:

1. **Sàn Ct hữu dụng của máy là 3,33′ (k ≤ 1,5) tới 3,67′ (k ≥ 2), không phải 3,0.** Ct 3,00 → `E` ở
   k 1,5 và 2,0 trên máy và trên đường thật (mirror chỉ cho P ở k ≤ 0,8, tức sườn thoải hơn mọi chứng
   dương đã thấy); Ct 3,33 → `E` khi k ≥ 2 (mirror: k 2,0 lẫn lộn, k 3,0 chắc chắn E). Đường dương
   thật dịch sớm 1,0′ (Ct 3,33) đã là `E`. Phần dưới của dải ERP 3,0–3,7 — chính những giếng người duyệt
   lật sang P — máy hôm nay vẫn không gọi P; chúng đổi từ `!` sang `E`, cùng một ký tự `!` trên TFT.
2. **`F` lý do 2 hầu như không với tới được bằng sigmoid thường.** Dưới 3,0 sigmoid thường ra `E`
   (S10 slot 1–2, R03 slot 6–8). F/2 chỉ xuất hiện ở hai pha (S05, S11 slot 8 ở dạng P) hoặc **bậc 3,0′
   không vá + sườn sớm** (S11 slot 6: F, Ct 1,67 — cờ đúng, số sai) hoặc sườn rất thoải k ≤ 0,7 với xác
   suất thấp (mirror: 3–5/35).
3. **Bậc đồng bộ 3,0′ của RPL01015 đổi kết luận, không chỉ đổi Ct.** Cùng sườn Ct 3,3 là `E` không
   bậc (S10 slot 3) nhưng **`P` Ct 4,00** khi có bậc +50 ở vòng 9 được vá (S11 slot 5): phép vá nội
   suy qua bậc kéo điểm 40 % lên 4,0′ và mở nhánh P. Có đuôi warm-up (bậc không vá) thì thành `F/2`
   Ct 1,67 hoặc `P` Ct 4,0–4,33 tuỳ nhiễu (mirror 10/35 F). Một artefact máy, ba kết luận.
4. Những thứ **không** dịch biên: nhiễu σ3, warm-up 250/τ2,5, creep 1,5/′ sau plateau, xung hai vòng
   (chỉ đẩy Ct trễ 4,33), bậc trước một sườn 5,0′, quá độ ấm quang học 300 đơn thuần (N, sharpness 7,1
   — sát `min_sharpness` 8, đúng như tài liệu 11/09 cảnh báo về nguồn nhiễu dưới 3,0).

## Ứng viên sửa (mirror what-if, CHƯA đưa vào firmware)

Kẹp tay trái theo **chỉ số của `MIN_CALLABLE_CT` − 1** (= 8, tức 2,67′) thay vì `discard_index − 1`:

```cpp
// Algo.cpp:771-774, find_sigmoidal_feature()
int floor_index = find_crossing_higher_than(record.time_data, MIN_CALLABLE_CT, 0);
record.peak_features.left_arm.i = find_crossing_lower_than_reversed(diff, peak_y * arm_percentile,
                                                                    main_peak.i, floor_index - 1);
```

Đỉnh vẫn bị ép sau 4,0′ (`discard_index` không đổi), chỉ tay trái được nhìn lùi tới sàn Ct. Chạy trên
mirror với cả 140 giếng × 5 slope: **đúng 8 giếng `known=E` chuyển sang chữ ý đồ** (S10 slot 1, 2, 3,
8, 9 · R03 slot 4, 5, 6 → P/F đúng như bảng), **0 giếng khác đổi chữ hay Ct**. R03 slot 7–8 (Ct 2,33
/ 2,0, tay trái trước 2,67′) vẫn `E` — hợp với nghĩa "không có pha lag trong cửa sổ quan sát". Bỏ kẹp
hẳn (`-1`) thì thêm S05 slot 7 và R03 slot 7–8 sang F/2.

**Vì sao chưa sửa**: quy tắc của repo — đổi thuật toán phải có số trên bộ nhãn 726 kênh
(`tools/algo_labels.tsv`, `test_algo_accuracy.py`), mà bộ đó **không tồn tại trên nhánh này**. 140
giếng mô phỏng nói ứng viên không làm hỏng gì *trong catalogue*; chúng không nói gì về 726 kênh thật
(đặc biệt các đường trôi chậm mà tay trái sớm hơn 3,67′ có thể biến N thành P — chính lý do kẹp này
tồn tại). Đây là quyết định của Kane sau khi có số; tài liệu này chỉ đưa ra ứng viên và cái giá đo
được của việc không sửa: **phần Ct 3,0–3,33 của dải ERP vẫn là `!`**.

## Đã loại

- **Đặt `expect="E"` cho các giếng Ct 3,0–3,33** để catalogue "xanh" — là chép hành vi hiện tại vào
  ý đồ, đúng thứ `known` sinh ra để tránh.
- **Chỉ dựng sigmoid tổng hợp** — R03 có mặt vì một đường cong thật dịch sớm không cãi được về hình
  dạng; nó ra đúng kết quả của S10.
- **Sửa firmware trong cùng lượt** — xem trên.
- **Chấm lại log cũ theo thứ tự với danh sách loại trừ** (`--only` cho 11 id cũ) — vá cho guard xanh,
  còn người dùng chấm log cũ vẫn gãy; khớp theo echo rẻ hơn và tự đúng.

## Còn nợ

- Quyết định về kẹp tay trái (trên) — cần bộ nhãn hoặc bằng chứng ERP tương đương.
- Ct của sườn thoải (k 0,6) dao động 3,0–4,0 giữa các seed trên mirror (điểm 40 % nhạy nhiễu khi
  đạo hàm phẳng) — chưa đo dải này trên máy với nhiều seed.
- Các nợ cũ (dời `climbs_fixed` lên trước `toJSON()`, backup raw gốc qua `/curve`, "Up Data" mất
  tên bệnh, byte `0xFF` cuối thân phản hồi) chưa động.
