Home
Screen
│
├── Header: Hiển thị tên thiết bị (Height: ~80)
│   ├── Text: Tên thiết bị
│   └── Text: tên thông tin công ty = 1/3 size của tên thiết bị
│
├── Thông báo: Hiện thị thông báo khi thiết bị đã thực hiện xong 1 qui trình,cần người dùng thực hiện tiếp theo. Thông báo này sẽ ẩn khi nào có thông báo từ máy mới xuất hiện(Height: ~90)
│   ├── Icon: icon quả chuông. 
│   ├── Title: Tên qui trình đã thực hiện xong.
│   └── Subtitle: Mô tả ngắn gọn nội dung qui trình (báo cho người dùng sẽ phải nhấn nút gì hay làm thao tác gì tiếp theo).
│
├── Trạng thái: Hiện thị qui trình hiện tại đang hoạt động (Height: ~90)
│   ├── Icon: Heater thì Icon nhiệt, đang trong process (lysis,amplification) thì icon đồng hồ
│   ├── Title: Tên qui trình đang thực hiện
│   └── Subtitle: Mô tả ngắn gọn nội dung qui trình (mất bao nhiêu phút, đang làm gì).

│
├── ScrollView
│   │
│   ├── Room Card
│   │     ├── Nhiệt độ Heater Lysis
│   │     ├── Nhiệt độ Amplification Right
│   │     ├── Nhiệt độ Amplification Left
│   │     ├── Nhiệt độ Top Right
│   │     ├── Nhiệt độ Top Left
│   │
│   ├── Room Button
│   │     ├── Button Green
│   │     ├── Button Red
│   │     ├── Button White
└── Bottom Navigation
      ├── Home
      ├── Process
      └── Setting


Cấu trúc Room Card
┌─────────────────────────────────────────────┐
│ Lysis                                       │
│                        ┌──────────────┐     │
│                        │ 18°C         │     │
│                        │ Temperature  │     │
│                        └──────────────┘     │
└─────────────────────────────────────────────┘
┌─────────────────────────────────────────────┐
│ Amplification                               │
│     ┌──────────────┐   ┌──────────────┐     │
│     │ 18°C         │   │ 18°C         │     │
│     │ Temperature  │   │ Temperature  │     │
│     └──────────────┘   └──────────────┘     │
│     ┌──────────────┐   ┌──────────────┐     │
│     │ 18°C         │   │ 18°C         │     │
│     │ Temperature  │   │ Temperature  │     │
│     └──────────────┘   └──────────────┘     │
└─────────────────────────────────────────────┘

Cấu trúc Room Button
 ┌─────────────────────────────────────────────┐
│ Living Room                                 │
│                                             │
│    ○ GREEN        ○ RED         ○ WHITE     │
│                                             │
└─────────────────────────────────────────────┘