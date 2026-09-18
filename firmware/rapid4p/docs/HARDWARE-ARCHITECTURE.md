# Kiến trúc phần cứng Rapid4P — ghép board P4C5 với bo LED + bo cảm biến ĐÃ CÓ

Ngày lập: 2026-09-18 · Trạng thái: **đề xuất, chờ đo 5 số liệu (§7) rồi chốt D1–D6 (§2)** ·
Tiếp nối `docs/plan/rapid4p-5-slot.md` (P2 "bo cảm biến 5 khe") và `HARDWARE-PINOUT.md` §11.5.

> Nguồn sự thật của tài liệu này là **netlist đọc từ file thiết kế thật** (KiCad + EasyEDA) của
> Rapid Reader thế hệ trước (ReaderPlus / ReaderMax, ESP32-WROOM-32), không phải suy đoán từ ảnh:
>
> | Bản | Vị trí (ngoài repo) | Nội dung |
> |---|---|---|
> | **05/2025 — KiCad, 3 bo rời** | `01. EngineerHub/03.RapidReaderMax/02.HardwarePCB/pcb-ReaderPlus/reader-plus/{led,sensor,reader-plus}/` (gerber đã sản xuất ở `02.HardwarePCB/{01. main,02. sensor,03. led}/`, 2025-05-08) | bo LED 4 × LED 5 mm, bo sensor 4 × TCS34725 + TCA9548A, bo main ESP32 + LDD-1200L |
> | **10/2025 — EasyEDA Pro, "readermassproduct"** | `02.HardwarePCB/ReaderMax.eprj` (SQLite; sheet `main` + `led-sensor`, PCB panel 2 bo, backup v103→v129 10/2025) | bo main ESP32-WROOM-32E + LDD-1200L, bo **led-sensor gộp**: TCA9548A + 4 × TCS34725 + **8 × LED 2835** (2 LED song song mỗi khe, 2 hàng) |
>
> Người dùng nói "đã có board LED + board sensor" — tài liệu ghi cả hai bản; **§7 mục 0 là xác nhận
> bản nào đang cầm trên tay**. Giao diện điện của hai bản giống nhau (§1.3) nên phương án dùng chung.

## 0. Tóm tắt một phút

1. Rapid4P = **4 bo**: `P4C5 + LCD 4,3"` (có, đã chạy) · **`Rapid4P-IF`** (bo giao tiếp/driver — **MỚI, phải
   thiết kế**) · `bo cảm biến` (có) · `bo LED` (có). Bo IF cắm vào JP1 (2×10, 2,54 mm) và mang toàn bộ phần
   mà bo main ESP32 cũ đang đảm nhiệm cho hai bo con: nguồn LED, công tắc từng khe, pull-up I2C, cấp nguồn 5 V
   cho P4C5.
2. Bo cảm biến và bo LED **dùng lại nguyên** cho bring-up 4 khe (build `BOARD_SENSOR_SLOTS 4`); bản 5 khe là
   **rev của chính hai bo này** (thêm 1 TCS ở mux kênh 4 + 1 LED, connector LED 5 → 6 chân), pitch khe giữ
   **13,545 mm** (bản KiCad) hoặc 15,0 mm (bản EasyEDA).
3. Đề xuất **bỏ LDD-1200L**: bằng chứng trong thiết kế 10/2025 (điện trở 4,7 K nối tiếp collector → dòng LED
   thực chỉ ~1–2 mA) cho thấy driver 1,2 A là thừa và đang bị "vô hiệu hoá" bằng điện trở; thay bằng
   **điện trở + công tắc low-side từ rail 5 V trên bo IF** (D2). Chỉ giữ LDD nếu phép đo §7.1 cho dòng ≥ 300 mA.
4. Firmware **không đổi mô hình**: N chân enable + 1 chân PWM chung (`BOARD_SLOT_LED_*`), I2C_NUM_1 GPIO26/27.
   Chỉ đổi số trong `board_esp32p4_43lcd.h` khi chốt (§5).

## 1. Kiểm kê hai bo đã có (đọc từ netlist)

### 1.1 Bo cảm biến (KiCad `sensor/sensor.kicad_sch`, 83,8 × 23,9 mm, 2 lớp)

| Khối | Linh kiện | Nối | Ghi chú |
|---|---|---|---|
| Connector **J1** | Molex KK-254 **1×4** (`AE-6410-04A`) | **1 GND · 2 +3V3 · 3 SCL · 4 SDA** | thứ tự dây vật lý: GND-3V3-SCL-SDA |
| Mux **U1** | TCA9548APWR (TSSOP-24) | A0/A1/A2 = GND → **0x70**; `~RESET` ← R1 10 K lên 3V3; SD/SC 0..3 dùng, 4..7 **bỏ trống** | kênh 4 còn trống → khe 5 thêm ngay được |
| Cảm biến U2/U3/U4/U5 | TCS34725FN ×4 (SON-6) | mux kênh **0/1/2/3** tương ứng; INT **không nối** | địa chỉ cố định 0x29 |
| Pull-up | R2/R3 10 K trên SCL/SDA **phía bus chính**; R4–R11 10 K trên SDA0..3/SCL0..3 | | bo main ESP32 cũ **không** có pull-up → pull-up nằm ở bo này |
| Lọc nguồn | C1 10 µF + C2–C5 0,1 µF (mỗi TCS 1 tụ) | | |
| Cơ khí | 4 TCS tại x = 165,34 / 178,88 / 192,43 / 205,97 mm → **pitch 13,545 mm** | | trùng pitch LED (§1.2) |

### 1.2 Bo LED (KiCad `led/led.kicad_sch`, 83,8 × 23,8 mm)

| Khối | Linh kiện | Nối |
|---|---|---|
| Connector **J1** | Molex KK-254 **1×5** (`AE-6410-05A`) | **1 VOUT+ · 2 LIGHT0 · 3 LIGHT1 · 4 LIGHT2 · 5 LIGHT3** |
| D1–D4 | LED **5 mm THT** nằm ngang (`LED_D5.0mm_Horizontal_O6.35mm_Z15.0mm`), pitch 13,545 mm | mỗi LED: một chân về **VOUT+** chung, chân kia về **LIGHTn** riêng; **không có điện trở** trên bo |

⚠️ Trong schematic KiCad, chân **K (cathode) của D1–D4 nối VOUT+** và **A nối LIGHTn** — ngược với chiều dòng
mà mạch main tạo ra (VOUT+ là cực dương của LDD, LIGHTn bị sink xuống VOUT− qua NPN). Bo thật chắc chắn đang
sáng (đã sản xuất), nên hoặc footprint đảo pad, hoặc LED được cắm ngược khi lắp. **Đo diode-test trên bo thật
trước khi thiết kế bo IF** (§7.3) — bo IF phải tôn trọng chiều thật, không tôn trọng schematic.

### 1.3 Cách bo main ESP32 cũ nuôi hai bo (để bo IF thay thế đúng vai trò)

Bản 05/2025 (`reader-plus.kicad_sch`) và 10/2025 (`ReaderMax.eprj` sheet `main`) cùng một topology:

```
12 V DC ──► J1/J2 (VH 3,96) ──┬──► AP63205 buck 5 V ──► AMS1117 3,3 V ──► ESP32, LCD, bo cảm biến (3V3)
                              └──► PS1 LDD-1200L (+Vin 23/24, −Vin 1/2)
                                       │ DIM (21) ◄── ESP32 IO12 "PWM" (LEDC 5 kHz, mặc định 127/255)
                                       │             [10/2025: R46 10 K kéo xuống GND]
                                       ├──► +Vout (13/14) = VOUT+ ──► J3/J13 (KK-254 1×5) chân 1 ──► bo LED
                                       └──► −Vout (11/12) = VOUT− ──► E của Q1..Q4 S8050
                                                    LIGHTn ◄── C của Qn   [10/2025: qua R 4,7 K !]
                                                    B của Qn ◄── R 4,7 K ◄── ESP32 IO25/26/27/14 "LEDn"
I2C: ESP32 IO33 SDA / IO32 SCL ──► J4 (KK-254 1×4: GND,3V3,SCL,SDA) ──► bo cảm biến
     [10/2025: thêm R6/R7 10 K pull-up trên main vì bo led-sensor gộp bỏ pull-up bus chính]
```

Ba điểm phải né khi thiết kế lại (đều là sự thật đọc từ netlist, không phải phỏng đoán):

| # | Vấn đề | Bằng chứng | Hệ quả cho Rapid4P |
|---|---|---|---|
| A | **−Vout của LDD-L không được nối −Vin/GND** (datasheet LDD-L 2024-08-02, bảng pin: "−Vin: Don't connect to −Vout"). Bo cũ lại lấy −Vout làm emitter của NPN mà base kích từ GPIO tham chiếu GND | `Q1..Q4.E = VOUT−`, `PS1.-Vin = GND` | công tắc từng khe **không** được đặt trong đường −Vout nếu còn dùng LDD; phải chuyển sang high-side hoặc bỏ LDD |
| B | Bản 10/2025 thêm **4,7 K nối tiếp collector** (R9/R12/R16/R18) trong đường dòng LED | `R12: 1=Q3.C, 2=LIGHT3` | LDD 1,2 A bị ép về ~(10 − 3 − 0,6)/4,7 K ≈ **1,4 mA** — tức ánh sáng thật cần rất nhỏ (TCS34725 dễ bão hoà). Đây là lý do chính của D2 |
| C | ESP32 quét PWM **5 kHz** vào chân DIM, datasheet LDD-1000L~1500L: **PWM 100–500 Hz**, DIM > 2,6–5,5 V hoặc **hở = BẬT 100 %**, < 0,4 V = tắt | `BOARD_SLOT_LED_PWM_FREQ_HZ 5000` kế thừa | nếu giữ LDD: đổi tần số + **bắt buộc pull-down** trên DIM (GPIO P4 lúc boot là input nổi → LED sáng hết) |

Ngoài ra: LDD-1000L~1500L vào **6–36 V**, ra 2–30 V; LCSC đánh dấu TCS34725FN "停产" (ngừng sản xuất tại
LCSC, `C2649487`) → **rủi ro nguồn cung khi làm bo 5 khe**, cần xác nhận kênh mua trước khi vẽ lại (§8).

## 2. Kiến trúc đề xuất — 4 bo, 1 nguồn

```
                         ┌───────────────────────────────────────────────────────┐
 5 V DC (USB-C PD / ─────┤  Rapid4P-IF  (MỚI, cắm JP1 2×10 hoặc cáp 20 dây)     │
 adapter 5 V 3 A)        │  • cấp 5 V ra USB-C J3 của P4C5 (VBUS → AXP2101)     │
                         │  • rail LED 5 V + công tắc N khe + PWM chung           │
                         │  • pull-up I2C 4,7 K (bus riêng I2C_NUM_1)            │
                         │  • connector: KK-254 1×4 (I2C) + KK-254 1×5/1×6 (LED) │
                         └───┬───────────────┬───────────────────────┬───────────┘
        JP1: GPIO26/27 I2C1  │   GPIO28/29/30/45/47 EN1..5, GPIO46 PWM│           │ GND/VCC3V3 (chỉ tham chiếu)
   ┌─────────────────────────┴───┐    ┌──────────────────────────────┴──┐   ┌────┴──────────────────┐
   │ P4C5 + LCD 4,3" DSI + touch │    │ Bo LED (có) — N LED, VOUT+ chung│   │ Bo cảm biến (có)      │
   │ ESP-IDF, firmware/rapid4p   │    │ LIGHT1..N sink                  │   │ TCA9548A 0x70 → N TCS │
   └─────────────────────────────┘    └─────────────────────────────────┘   └───────────────────────┘
```

| Quyết định | Đề xuất | Lý do | Ai chốt |
|---|---|---|---|
| **D1 Số bo** | 4 bo như hình; **không** vẽ lại bo cảm biến/LED cho bring-up; bo IF là bo duy nhất phải thiết kế | dùng lại linh kiện đã có, tách rủi ro: IF hỏng không kéo theo LCD/P4 | HW |
| **D2 Driver LED** | **Phương án A (đề xuất)**: rail 5 V trên bo IF → điện trở hạn dòng nối tiếp mỗi khe (đặt ngay trên bo IF, cạnh connector LED) → LED → LIGHTn → **MOSFET low-side** (2N7002/AO3400, gate ← GPIO ENn qua 100 R sẵn trên JP1, pull-down 100 K); độ sáng = **PMOS high-side chung** trên rail LED do GPIO46 PWM (qua NPN đảo mức) — hoặc đơn giản hơn: PWM chính GPIO ENn (LEDC N kênh) và bỏ chân PWM chung. **Phương án B (dự phòng)**: giữ LDD-1200L trên bo IF, 12 V vào, công tắc khe chuyển sang **high-side** trên +Vout (PMOS + level shift), DIM có pull-down 10 K, PWM 100–500 Hz | A: không cần 12 V, không có mục A/C ở §1.3, chi phí ~1/10; bằng chứng B §1.3 cho thấy dòng thật cỡ mA. B chỉ khi §7.1 đo ≥ 300 mA | HW + chủ thuật toán (độ sáng) |
| **D3 Nguồn** | **Một nguồn 5 V duy nhất** vào bo IF; bo IF cấp ngược cho P4C5 qua **USB-C J3** (VBUS ba cổng nối chung → AXP2101). Không dùng `BOOST_5V` JP1 chân 2 làm rail LED (hồi tiếp R45/R46 gợi ý ~2,6 V, HARDWARE-PINOUT §12 mục 10) và không kéo LED từ VCC3V3 (DCDC1 AXP đang nuôi P4 + LCD) | P4C5 vốn là board chạy pin/USB, không có ngõ 12 V; một adapter, một dây | HW |
| **D4 I2C** | Bus riêng `I2C_NUM_1` GPIO26/27 (JP1 12/13), 100 kHz, **pull-up 4,7 K trên bo IF**; bo cảm biến KiCad có sẵn 10 K → song song ≈ 3,2 K vẫn trong dải; bản EasyEDA gộp không có pull-up bus chính → bo IF **bắt buộc** có | 100 R nối tiếp trên JP1 + cáp + 4–5 nhánh sau mux | FW (đã khai) |
| **D5 Bring-up** | Nạp `BOARD_SENSOR_SLOTS 4` với hai bo có sẵn → đạt tiêu chuẩn §4 CLAUDE.md (log `slot 1..4 OK`, `4/4`, chu trình đo) → mới làm bo 5 khe | tách lỗi firmware khỏi lỗi bo mới | FW |
| **D6 Bo 5 khe** | Rev bo cảm biến: thêm U6 TCS ở mux **kênh 4** (đã trống) + 2 pull-up; rev bo LED: thêm D5, connector KK-254 **1×6** (VOUT+, LIGHT1..5); bo IF vẽ sẵn 6 chân + 5 công tắc (GPIO47) ngay từ đầu; pitch khe giữ như cơ khí khay ống (§7.4) | tránh vẽ bo IF hai lần | HW + cơ khí |

## 3. Bo Rapid4P-IF — đặc tả để vẽ schematic

### 3.1 Connector

| Tên | Loại | Chân | Ghi chú |
|---|---|---|---|
| **JP1-IF** | 2×10 2,54 mm (đối ứng JP1 P4C5, HARDWARE-PINOUT §11.5) | dùng: 3/10 GND · 4 VCC3V3 (chỉ để tham chiếu mức / pull-up I2C) · 12 GPIO26 SDA · 13 GPIO27 SCL · 14 GPIO28 EN1 · 15 GPIO29 EN2 · 16 GPIO30 EN3 · 11 GPIO45 EN4 · **8 GPIO47 EN5** · 9 GPIO46 PWM. Không dùng 1 (DAC), 2 (BOOST_5V), 5/6/7 (GPIO50/49/48 — để dành) | mọi GPIO đã có 100 R nối tiếp trên P4C5 |
| **J-SENS** | Molex KK-254 1×4 | 1 GND · 2 +3V3 · 3 SCL · 4 SDA (khớp J1 bo cảm biến) | 3V3 cho bo cảm biến lấy từ **LDO 3,3 V riêng trên bo IF** (AMS1117/ME6211, ≤ 30 mA tổng: TCA9548A + 5 TCS ≈ 5 × 0,3 mA + 0,1 mA) — không kéo từ VCC3V3 của P4C5 qua JP1 |
| **J-LED** | Molex KK-254 **1×6** | 1 VOUT+ · 2..6 LIGHT1..5 (bo LED 4 khe cắm 5 chân đầu) | |
| **J-PWR** | USB-C (chỉ nguồn) hoặc DC jack 5,5/2,1 mm 5 V | vào 5 V ≥ 3 A | |
| **J-P4** | USB-C đực trên cáp / hoặc 2 chân VBUS-GND hàn vào P4C5 J3 | ra 5 V cho P4C5 | polyfuse 2 A |

### 3.2 Khối LED — phương án A (đề xuất)

```
 5 V_IN ──► PMOS Q_PWM (AO3401) ──► LED_RAIL ──► R_k (mỗi khe, xem bảng) ──► J-LED.VOUT+ ... bo LED ... LIGHTk
             │ gate ◄── NPN đảo mức ◄── GPIO46 PWM (LEDC 5 kHz, giữ nguyên)            │
                                                                                      ▼
                                                              LIGHTk ──► NMOS Q_k (2N7002) ──► GND
                                                                          gate ◄── GPIO EN_k (100 R có sẵn), R 100 K xuống GND
```

- Vì R_k nằm giữa rail chung và VOUT+ **chung**, thực tế **một điện trở chung** là đủ khi firmware chỉ bật
  **một khe một lúc** (`measure.c`: `slot_led_on(slot)` → đo → `slot_led_off`, chưa bao giờ 2 khe cùng bật).
  Vẽ **1 R chung + jumper 0 R** cho từng khe để có thể bù độ sáng lệch giữa khe nếu cần.
- Giá trị R: chờ §7.1. Ước lượng để đặt chỗ: LED 2835 trắng Vf 3,0 V, muốn 20 mA → R = (5 − 3,0 − 0,1)/0,02 ≈
  **100 R**, 0,1 W; muốn 150 mA → 12 R 0,5 W. LED 5 mm bản KiCad: 20 mA là trần.
- Cực tính bo LED: nếu §7.3 cho thấy LED cắm "K về VOUT+" (như schematic), thì VOUT+ phải là **cực âm chung** và
  LIGHTk là nguồn dương → đổi khối trên thành **PMOS high-side từng khe** (`BOARD_SLOT_LED_ON_LEVEL 0`). Vẽ bo IF
  sau khi đo, không đoán.
- PWM: nếu bỏ PMOS chung, đặt LEDC PWM trực tiếp lên GPIO EN_k (P4 có 8 kênh LEDC, timer 1 đã dành). Khi đó
  `BOARD_SLOT_LED_PWM_GPIO -1` và `slot_led.c` cần nhánh "PWM trên từng chân enable" (hiện chưa có — việc FW).

### 3.3 Khối LED — phương án B (giữ LDD, chỉ nếu §7.1 ≥ 300 mA)

- 12 V vào J-PWR → LDD-1200L (hoặc LDD-350L/500L đúng dòng đo được) → **+Vout** → PMOS high-side từng khe
  (gate kéo lên +Vout qua 10 K, kéo xuống bằng NPN từ GPIO EN_k) → LED → **−Vout** trực tiếp, không transistor.
- DIM (21): R 10 K xuống GND (mặc định TẮT khi P4 chưa cấu hình GPIO), GPIO46 qua 1 K; `BOARD_SLOT_LED_PWM_FREQ_HZ`
  → **200**. Buck 12 → 5 V (AP63205 như bo cũ) cấp P4C5 qua USB-C.
- Nhược: cần adapter 12 V, LDD 31,8 × 20,3 × 12,2 mm chiếm chỗ, giá ≈ 10× phương án A.

### 3.4 Khối I2C và nguồn cảm biến

- Pull-up 4,7 K SDA/SCL lên **3V3_SENS** (LDO trên bo IF), không lên VCC3V3 của P4 (tránh dòng chảy ngược khi
  P4 tắt mà bo IF còn nguồn).
- Nếu muốn dịch mức an toàn: 3V3_SENS và VCC3V3 đều 3,3 V → không cần level shifter.
- Tuỳ chọn: chân `~RESET` của TCA9548A ra JP1 GPIO48 (còn trống) để firmware reset mux khi bus kẹt — bo cảm biến
  hiện kéo lên 10 K cố định, muốn dùng phải cắt/nối trên rev 5 khe.

### 3.5 Ngân sách nguồn (phương án A, 5 V vào)

| Tải | Dòng |
|---|---|
| P4C5 + LCD 4,3" + đèn nền + C5 WiFi (đo trên bo P4, README-P4 §6 vimate) | ≈ 0,6–0,9 A đỉnh |
| LED chiếu (1 khe bật/lần) | 0,02–0,15 A |
| Bo cảm biến | < 0,01 A |
| Sạc pin qua AXP2101 (nếu gắn pin) | tới 1 A |
| **Tổng** | **≤ 2 A** → adapter 5 V 3 A, polyfuse 2 A trên đường ra P4 |

## 4. Kiến trúc dữ liệu/điều khiển không đổi

`measure.c` → `slot_led_on(k)` (EN_k = ON, PWM = `led_pwm[k]`) → `vTaskDelay(settle)` → `tca9548_select(ch_k)` →
`tcs34725_read` → `slot_led_off(k)`. Bo IF chỉ đổi **cái gì đứng sau GPIO**, không đổi thứ tự này. Hợp đồng dữ liệu
(`slots: N`, `slot_*`) và calib NVS không liên quan tới bo IF.

## 5. Việc firmware khi chốt D2/D6 (chỉ sửa `main/boards/board_esp32p4_43lcd.h` + comment)

| Knob | Hiện | Phương án A | Phương án B |
|---|---|---|---|
| `BOARD_SENSOR_SLOTS` | 5 | **4** khi bring-up với bo có sẵn → 5 khi có rev | như A |
| `BOARD_SLOT_LED_GPIOS` | {28,29,30,45,47} | giữ | giữ |
| `BOARD_SLOT_LED_ON_LEVEL` | 1 | 1 (NMOS low-side) / **0** nếu §7.3 buộc PMOS high-side | 0 (PMOS high-side qua NPN: GPIO=1 → bật, tuỳ cách vẽ; ghi rõ khi vẽ) |
| `BOARD_SLOT_LED_PWM_GPIO` | 46 | 46 (PMOS chung) hoặc **−1** nếu PWM trên từng EN | 46 → DIM |
| `BOARD_SLOT_LED_PWM_FREQ_HZ` | 5000 | 5000 | **200** (dải 100–500 Hz) |
| `BOARD_SLOT_LED_PWM_DEFAULT` | 127 | đo lại theo §7.1 (calib UI vẫn cho chỉnh) | như cũ |
| Comment khối `BOARD_SENSOR_*` | "CHƯA CÓ SCHEMATIC bo con" | ghi "bo IF rev X, schematic <đường dẫn>, đo ngày …" | |

Ngoài header: nếu chọn "PWM trên từng EN" → `slot_led.c` thêm nhánh N kênh LEDC (nửa ngày); `registry_check`
kiểm `BOARD_SENSOR_SLOTS == products.yaml optical_slots` (đã có trong kế hoạch P4) — khi tạm hạ về 4 để bring-up,
đặt registry `optical_slots` **giữ 5** và build 4 chỉ ở nhánh dev, không tag.

## 6. Lộ trình (nối tiếp §3 của `rapid4p-5-slot.md`)

| Bước | Việc | Đầu ra | Chặn bởi |
|---|---|---|---|
| H0 | Đo 5 số liệu §7 trên bo đang có (1 buổi) | bảng số đo dán vào §7 | có bo + máy ReaderPlus chạy |
| H1 | Chốt D1–D6 (chủ sản phẩm + HW) | ghi vào §2 + `docs/history/` | H0 |
| H2 | **Bo IF rev A** (KiCad, thư mục `01. EngineerHub/…/Rapid4P/02.HardwarePCB/01.Rapid4P-IF/` — ngoài repo firmware; repo chỉ giữ pinout đã chốt trong `HARDWARE-PINOUT.md` §11.5 + header board) | schematic + BOM + gerber; có thể **lắp tay bản đầu trên perfboard** để bring-up trước | H1 |
| H3 | Bring-up 4 khe: P4C5 + IF + bo có sẵn, build `SLOTS 4` | log đạt §4 CLAUDE.md, số đo lux/thời gian thật, calib qua UI | H2 |
| H4 | Rev bo cảm biến + bo LED **5 khe** (D6), build `SLOTS 5` | máy 5 khe đầu tiên | H3 + cơ khí khay + nguồn TCS |
| H5 | Hợp đồng dữ liệu P4 + phát hành P6 (kế hoạch cũ) | | H4 |

## 7. Phép đo phải làm trước khi vẽ bo IF (điền kết quả vào đây)

| # | Đo cái gì | Cách | Kết quả |
|---|---|---|---|
| 0 | **Bản bo đang có**: KiCad 05/2025 (2 bo rời, LED 5 mm, KK-254) hay EasyEDA 10/2025 (bo gộp, 8 LED 2835, XH 2,5)? | nhìn bo | ☐ |
| 1 | **Dòng LED thật** khi ReaderPlus đo ở PWM mặc định 127: ampe kế nối tiếp dây LIGHTn; và điện áp VOUT+ − VOUT− | máy ReaderPlus đang chạy | ☐ mA / ☐ V |
| 2 | Giá trị thật R9/R12/R16/R18 (nối tiếp collector) trên bo main 10/2025 — 4,7 K hay 0 R? | ohm kế / đọc mã | ☐ |
| 3 | **Chiều LED trên bo LED**: diode-test từ J1 chân 1 (VOUT+) sang chân 2..5; LED sáng khi đỏ ở đâu? | đồng hồ | ☐ |
| 4 | Pitch khe của khay ống cơ khí Rapid4P (`ModuleSensor.SLDASM` / khay mới) — 13,545 hay 15,0 mm; chỗ cho khe 5 | CAD | ☐ mm |
| 5 | Điện áp thật `BOOST_5V` JP1 chân 2 trên P4C5 khi `GPIO20=1` (nghi ~2,6 V) | vôn kế | ☐ V |

## 8. Rủi ro

- **Nguồn cung TCS34725FN** (LCSC "停产"): xác nhận ams-OSRAM/Digi-Key/Mouser còn hàng trước khi vẽ rev 5 khe; nếu
  không, mọi khe phải cùng đổi sang một cảm biến khác (calib/thuật toán đổi theo — không được trộn).
- **Chiều LED ngược trong schematic KiCad** (§1.2): vẽ bo IF theo số đo §7.3, không theo file.
- **Độ sáng thay đổi khi bỏ LDD**: ngưỡng calib min/max (`calib_store`) và ngưỡng bệnh (600/500) phải đo lại
  trên bo IF — đã có UI calib; chủ thuật toán quyết định số.
- **Bo IF cấp 5 V ngược vào P4C5 qua USB-C**: cắm cùng lúc máy tính vào J5/J6 → hai nguồn VBUS song song; AXP2101
  chịu được (VBUS input), nhưng bo IF cần diode/ideal-diode trên đường ra để không bị nạp ngược.
- **JP1 100 R nối tiếp**: đủ cho gate MOSFET/NPN; **không** kéo LED trực tiếp từ GPIO (đã ghi trong plan §4).
