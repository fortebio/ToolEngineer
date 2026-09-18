# 2026-09-18 — Rapid4P: kiến trúc phần cứng ghép bo LED + bo cảm biến đã có

## Bối cảnh

Kế hoạch 5 khe (`docs/plan/rapid4p-5-slot.md`, P2) giả định phải vẽ bo cảm biến mới. Thực tế Forte đã có
**bo LED + bo cảm biến 4 khe** của thế hệ Rapid Reader trước (ReaderPlus/ReaderMax, ESP32-WROOM-32), file thiết kế
nằm ngoài repo: `01. EngineerHub/03.RapidReaderMax/02.HardwarePCB/` (KiCad 05/2025 — 3 bo rời, gerber đã sản
xuất; EasyEDA Pro `ReaderMax.eprj` 10/2025 — bo led-sensor gộp). Việc hôm nay: đọc netlist thật của các bo đó và
lập phương án ghép với board ESP32-P4C5.

## Đã làm

- **Mới** `firmware/rapid4p/docs/HARDWARE-ARCHITECTURE.md`: kiểm kê hai bo (connector, pinout, mux/kênh, pull-up,
  pitch 13,545 mm), cách bo main ESP32 cũ nuôi chúng (12 V → LDD-1200L → VOUT+ chung, S8050 sink LIGHTn), 3 điểm
  phải né (−Vout LDD không tham chiếu GND theo datasheet LDD-L; bản 10/2025 có 4,7 K nối tiếp collector → dòng LED
  thực ~mA; PWM 5 kHz ngoài dải 100–500 Hz của LDD), kiến trúc 4 bo với bo giao tiếp **`Rapid4P-IF`** mới, quyết
  định D1–D6 (đề xuất bỏ LDD → R + MOSFET từ rail 5 V; một nguồn 5 V cấp ngược P4C5 qua USB-C; bring-up 4 khe
  trước rồi rev 5 khe), đặc tả bo IF, bảng knob firmware đổi theo từng phương án, lộ trình H0–H5, **5 phép đo
  phải làm trước khi vẽ bo IF** (§7), rủi ro (TCS34725FN "停产" tại LCSC, chiều LED vẽ ngược trong schematic KiCad).
- Trỏ tới tài liệu mới: `firmware/rapid4p/CLAUDE.md` (§1, §2, §6.1), `docs/HARDWARE-PINOUT.md` §11.5,
  comment khối `BOARD_SENSOR_*` trong `board_esp32p4_43lcd.h`, `docs/plan/rapid4p-5-slot.md` (Q2, P2 → P2a/P2b).
- **Không đổi code/knob** — hardware chưa chốt; chỉ comment.

## Cách đọc netlist (để lặp lại)

- KiCad: MCP `kicad` › `extract_schematic_netlist` + `list_labels_in_schematic` trên `.kicad_sch` (nhãn cục bộ
  không được tool gộp vào net — đối chiếu bằng danh sách nhãn).
- EasyEDA Pro `.eprj` = SQLite: bảng `documents.dataStr` là `base64` + gzip của định dạng dòng JSON
  (`["COMPONENT",…]`, `["PAD_NET", comp, pad, net, …]`); bảng `devices`/`attributes` map UUID → mã linh kiện.
  Đọc **PCB** (`PAD_NET`) tin cậy hơn sheet vì đó là bản đã sản xuất.
- Datasheet LDD-L (meanwell.com `LDD-L-SPEC.PDF`) đọc bằng `pypdf` trong venv IDF
  (`C:\Espressif\python_env\idf5.5_py3.11_env`); `python` hệ thống không có pip.

## Việc tiếp

H0: đo 5 số liệu §7 (bản bo đang cầm, dòng LED thật, R collector, chiều LED, pitch khay, `BOOST_5V`) → H1 chốt
D1–D6 → H2 vẽ bo IF (có thể lắp tay bản đầu) → H3 bring-up `BOARD_SENSOR_SLOTS 4` → H4 rev 5 khe.
