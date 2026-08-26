# 2026-08-20 — Bảng mã lỗi cảm biến trong màn chi tiết kết quả

## Yêu cầu

> "tôi muốn thêm 1 bảng mã lỗi bên cạnh chart và dưới kết quả khi máy phát hiện lỗi sensor
> gửi về server"

## Đường đi của dữ liệu (đã kiểm, không suy)

Firmware **không** nhét lỗi vào payload kết quả. `errorCheck.cpp` gửi một POST RIÊNG:

```json
{"method":"error","id_device":"RPL02007","version":"v2.4.3",
 "error":[{"Slot":"Slot 6","error_code":"1045","error_msg":"[Sensor Light]- No data..."}]}
```

Nó đi vào **cùng đường `/ingest`** với kết quả, và `validate()` chỉ đòi `id_device` nên **được
nhận** — nằm trong `sessions` như một hàng riêng, `CT_value` NULL, `type_upload` NULL.

Hai hàm bắn nó:
* `postError_Googlesheet()` — ngay khi lỗi phát sinh, tức GIỮA run (sớm hơn kết quả tới ~1 giờ).
* `postError_fullGoogleSheet()` — xả cả sổ ở màn kết thúc, cùng chỗ kết quả được gửi; TLS mất
  30-90 s mỗi đích nên có thể tới **SAU** kết quả.

## Luật ghép (`db.session_errors`)

Bản ghi lỗi **không mang mã lần đo nào** → phải ghép theo thời gian. Ba chặn, phải có đủ:

1. sau lần đo liền TRƯỚC (không nuốt lỗi của run trước),
2. trong vòng **2 giờ** trước lần đo này,
3. không muộn hơn lần đo này quá **15 phút**.

⚠️ **Chặn (2) không thừa — bỏ nó là lỗi thật đã dẫm ngay khi kiểm trên dữ liệu production.**
RPL02007 báo 3 lỗi ngày **04/06**; lần đo kế tiếp của nó mãi **03/08** mới có. Luật "sau lần đo
liền trước" một mình gán tuốt 3 lỗi **hai tháng tuổi** vào lần đo tháng 8. Máy nằm im hàng tháng
là chuyện thường ngoài hiện trường nên vế (1) không tự chặn được gì.

Kiểm cả hai chiều trên chính DB production: sau khi thêm chặn, session 21056 ra **0 lỗi**
(đúng), còn một lần đo giả lập 10 phút sau lỗi vẫn bắt được **đủ 3** (đúng).

## App

* `GET /sessions/{id}/errors` → `{id, errors:[{at, session_id, slot, code, message}]}`.
* `FbtApi.sessionErrors()` **nuốt mọi lỗi, trả rỗng** — server chưa deploy route trả 405, và
  rỗng vốn là kết quả BÌNH THƯỜNG.
* Bảng nằm **dưới lưới Kết quả bệnh, cùng cột với nó** — lỗi cảm biến giải thích chính mấy ô
  kết quả phía trên (giếng mất dữ liệu, quá tối), đọc rời hai chỗ là mất mối liên hệ.
* **Rỗng thì KHÔNG dựng bảng.** Khung rỗng kèm "không có lỗi" là cấp giấy chứng nhận sạch cho
  một lần đo mà ta chỉ biết là *không nghe thấy gì*.
* Mã 4 chữ số in bằng **JetBrains Mono, giữ nguyên số 0 đầu** — đây đúng là mã hiện trên màn
  TFT (`module*1000 + type*100 + step*10 + slot`), kỹ sư đọc chéo hai nơi.
* Truyền VÀO màn, không để màn tự gọi API: `ResultDetailScreen` dùng chung cho **cả 4 nguồn**
  (Google Drive · RAPID ERP · Engineer Server · máy LAN) mà chỉ Engineer có endpoint này.
  Caller kiểm KIỂU (`api is FbtApi`) thay vì thêm hàm vào `CloudHistoryClient` — một nguồn có,
  ba nguồn không.

## Giới hạn phải nói ra

**Đo được 2026-08-20: 1 bản ghi lỗi trên 3706 phiên**, và nó từ 04/06. Trước firmware
2026-08-05 đường lỗi **chỉ tới Google Sheet** (`postError_*` có bản sao riêng, không qua
`postJsonToAllTargets`), mà gần cả 109 máy vẫn chạy bản cũ hơn. Endpoint + bảng này đúng,
nhưng sẽ **im lặng tới khi fleet lên bản mới**.

## Chưa sửa — cần chủ dự án quyết

Bản ghi lỗi **được đếm là "phiên"** trong `list_devices` (`count(*)`), nên cột *Số phiên* của
bảng Trạng thái máy nhích lên mỗi lần máy báo lỗi. Hiện 1/3706 nên không ai thấy; fleet lên bản
mới thì nó thành sai rõ. Sửa là đổi `count(*)` thành đếm có điều kiện — nhưng đổi ngữ nghĩa một
cột đang dùng nên không tự làm.

## Kiểm

49 test server · 112 test app · gieo 5 lỗi cho phần mới, bắt đủ 5. Phần SQL kiểm bằng chạy
THẬT trên DB production (cả ca âm lẫn ca dương), vì test local không chạm Postgres.
