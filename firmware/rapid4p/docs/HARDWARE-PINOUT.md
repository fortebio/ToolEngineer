# Pinout phần cứng — board FBT ESP32-P4C5 + LCD 4.3" + SC2336

> **Nguồn:** schematic Altium `ESP32P4_KSDIY_P4C5.SchDoc` (bản PDF `原理图.pdf` /
> `ESP32-P4C5.pdf`, ngày 6/04/2026, 1 trang A1).
> Toàn bộ bảng dưới đây được trích bằng cách dựng lại netlist từ vector của file PDF
> (đường dây, junction dot, net label) rồi đối chiếu lại bằng ảnh render từng khối,
> **không suy đoán từ thứ tự text**.
>
> **Đối chiếu chéo đã làm:**
> - Chân SDMMC khớp `SDMMC_SLOT0_IOMUX_PIN_NUM_*` của `esp-idf-v5.5.1/components/soc/esp32p4`.
> - Console UART khớp mặc định `ESP_CONSOLE_UART_TX_GPIO = 37` cho `IDF_TARGET_ESP32P4`.
> - I2C touch/SCCB (GPIO7/GPIO8) khớp firmware đang chạy thật và
>   `CONFIG_EXAMPLE_MIPI_CSI_SCCB_I2C_{SDA,SCL}_PIN` trong project demo camera.

---

## 1. Tổng quan

| Khối | Linh kiện | Ghi chú |
|---|---|---|
| MCU | Module **ESP32-P4C5** (鱼鹰光电), 88 chân | P4 + co-processor **ESP32-C5** tích hợp trong module |
| Màn hình | LCD1 `YDP430BT009-V1`, FPC 30 chân 0.5 mm | MIPI-DSI 2 lane + touch I2C |
| Camera | FPC1 `AFC01-S24FCA-00`, 24+2 chân | MIPI-CSI 2 lane (SC2336) |
| Audio out | ES8311 (DAC) + NS4150B (ampli class-D) | loa qua J1 MX1.25-2P |
| Audio in | ES7210 (ADC 4 kênh) + 2 mic MEMS `MSM381A3729H9CP` | kênh 3 = AEC reference |
| PMIC | AXP2101 | pin Li-ion qua J2 MX1.25-2P |
| Thẻ nhớ | J4 TF-CARD, 4-bit SDMMC | VDD lấy từ LDO nội của P4 (kênh 4) |
| 4G | ML307R-DL (中移 AT) + SIM + IPEX | UART qua level shifter |
| Khác | RS485 (SN65HVD3082E), IMU LSM6DS3TR-C, DAC MCP4725, WS2812B, boost 5V SY7088 | |

---

## 2. Bảng pinout tổng — GPIO0…GPIO54

Cột **Module** = số chân trên module ESP32-P4C5.
Cột **Net** = tên net trong schematic (dùng để tra ngược lại bản vẽ).

| GPIO | Module | Net | Chức năng | Trở/tụ ngoài · ghi chú |
|---|---|---|---|---|
| 0  | 88 | `GPIO0` | **BTN3** — nút người dùng | R60 10K lên VCC3V3, D20 ESD. Nhấn = LOW |
| 1  | 16 | `GPIO1` | *(trống)* | Chỉ có net label, không nối đi đâu |
| 2  | 17 | `C5_BOOT` | Strap boot cho **ESP32-C5** | Nối thẳng `C5_IO25` (chân 9); qua R67 0R tới `C5_IO26/27/28` (chân 8/7/6) |
| 3  | 18 | `PA_CTRL` | Enable ampli NS4150B (chân STD) | HIGH = bật loa |
| 4  | 19 | `4G_VABT_EN` | Bật nguồn module 4G | HIGH → Q1 SS8050 → Q3 AO3401A (P-FET) dẫn `VCC_BAT_IN` sang `4G_VBAT` |
| 5  | 20 | `GPIO5` | *(trống)* | |
| 6  | 21 | `LCD_BL` | Enable/PWM backlight (SY7200 EN) | R39 10K lên VCC3V3 → **mặc định đèn nền BẬT** |
| 7  | 22 | `ES_I2C_SDA` | **I2C SDA dùng chung** | R47 2.2K lên VCC3V3, C54 22pF. Alias: `TP_SDA`, `AXP_IIC_SDA` |
| 8  | 23 | `ES_I2C_SCL` | **I2C SCL dùng chung** | R48 2.2K lên VCC3V3, C55 22pF. Alias: `TP_SCL`, `AXP_IIC_SCL` |
| 9  | 24 | `CODEC_I2S0_DSDIN` | I2S **DOUT** (ESP → ES8311 `DSDIN`) | đường phát ra loa |
| 10 | 25 | `CODEC_I2S0_LRCK` | I2S **WS / LRCK** | C57 22pF |
| 11 | 26 | `CODEC_I2S0_SDOUT` | I2S **DIN** (ES7210 `SDOUT1` → ESP) | đường thu từ mic |
| 12 | 27 | `CODEC_I2S0_SCLK` | I2S **BCLK** | C58 22pF |
| 13 | 28 | `CODEC_I2S0_MCLK` | I2S **MCLK** | C59 22pF |
| 20 | 29 | `BOOST_ON` | Enable boost 5V SY7088 | qua R44 1K vào chân EN |
| 21 | 30 | `ESP_IIC_IRQ` | **IRQ từ AXP2101**, active LOW | R68 10K lên VCC3V3; dịch mức qua Q2 2N7002 sang `AXP_IIC_IRQ` (miền VRTC) |
| 22 | 31 | `TP_RST` = `LCD_RST` | **Reset CHUNG cho LCD và touch** | R70 10K lên VCC3V3 + C95 1uF xuống GND (trễ RC lúc bật nguồn) |
| 23 | 32 | `TP_INT` | Touch interrupt | LCD1 chân 28 |
| 24 | 50 | `USB1P1_N` | USB 1.1 FS **D−** → J5 USB-C | PHY nội USB-Serial/JTAG của P4 |
| 25 | 51 | `USB1P1_P` | USB 1.1 FS **D+** → J5 USB-C | |
| 26 | 53 | `GPIO26` | JP1 chân 12 | R62 100R nối tiếp |
| 27 | 54 | `GPIO27` | JP1 chân 13 | R63 100R |
| 28 | 55 | `GPIO28` | JP1 chân 14 | R64 100R |
| 29 | 56 | `GPIO29` | JP1 chân 15 | R65 100R |
| 30 | 57 | `GPIO30` | JP1 chân 16 | R66 100R |
| 31 | 58 | `RS485_TX` | SN65HVD3082E `D in` (chân 4) | |
| 32 | 59 | `RS485_DE` | `DE` + `RE` (chân 3+2, nối chung) | HIGH = chế độ phát |
| 33 | 60 | `RS485_RX` | SN65HVD3082E `R out` (chân 1) | |
| 34 | 61 | `WS2812_DAT` | `DIN` của **WS2812B** (1 LED RGB) | `DOUT` bỏ trống |
| 35 | 62 | `GPIO35_BOOTMODE` | **BTN2 = nút BOOT** + `DTRTNOW` của CH343P | R28 10K lên VCC3V3, D19 ESD |
| 36 | 63 | `GPIO36` | *(trống)* | Chỉ có R29 10K lên VCC3V3 |
| 37 | 64 | `UART0_TXD` | **Console TX** → CH343P `RXD` (chân 5) | alias `ESP32_TX` |
| 38 | 65 | `UART0_RXD` | **Console RX** ← CH343P `TXD` (chân 4) | alias `ESP32_RX` |
| 39 | 67 | `SD_DATA0` | SDMMC D0 | R26 51K lên `ESP_LDO_VO4` |
| 40 | 68 | `SD_DATA1` | SDMMC D1 | R27 51K |
| 41 | 69 | `SD_DATA2` | SDMMC D2 | R23 51K |
| 42 | 70 | `SD_DATA3` | SDMMC D3 | R24 51K |
| 43 | 71 | `SD_CLK` | SDMMC CLK | không có pull-up (đúng chuẩn) |
| 44 | 72 | `SD_CMD` | SDMMC CMD | R25 51K |
| 45 | 73 | `GPIO45` | JP1 chân 11 | R61 100R |
| 46 | 74 | `GPIO46` | JP1 chân 9 | R59 100R |
| 47 | 75 | `GPIO47` | JP1 chân 8 | R58 100R |
| 48 | 76 | `GPIO48` | JP1 chân 7 | R57 100R |
| 49 | 77 | `GPIO49` | JP1 chân 6 | R56 100R |
| 50 | 78 | `GPIO50` | JP1 chân 5 | R55 100R |
| 51 | 79 | `4G_ESP_DTR` | DTR tới ML307R (chân 19) | qua Q5 SS8050 → **đảo mức** |
| 52 | 80 | `4G_ESP_RX` | **UART RX** ← TX của 4G | qua Q4 (LMBT3904DW1T1G), R30 10K lên VCC3V3 |
| 53 | 81 | `4G_ESP_TX` | **UART TX** → RX của 4G | qua Q4, R43 10K lên `VDD_EXT` |
| 54 | 82 | `GPIO54` | *(trống)* | Chỉ có net label |

**Chân đặc biệt (không phải GPIO):**

| Module | Tên | Nối tới |
|---|---|---|
| 1, 3, 15, 33, 40, 47, 52, 83 | `GND` | GND |
| 2 | `LNA_OUT` | **không nối** |
| 4, 5 | `C5_U0RXD`, `C5_U0TXD` | **không nối** (UART debug của C5 không đưa ra) |
| 6, 7, 8 | `C5_IO28/27/26` | nối chung → R67 0R → `C5_BOOT` |
| 9 | `C5_IO25` | `C5_BOOT` |
| 10–14 | `C5_IO24/23/6/5/4` | **không nối** |
| 34–39 | DSI D1P/D1N/CLKN/CLKP/D0P/D0N | LCD1 (§4) |
| 41–46 | CSI D0N/D0P/CLKP/CLKN/D1N/D1P | FPC1 camera (§5) |
| 48, 49 | `DM`, `DP` | `ESP_USB_N` / `ESP_USB_P` — USB 2.0 HS (§8) |
| 66 | `ESP_LDO_VO4` | VDD thẻ nhớ + 5 điện trở pull-up 51K (§7) |
| 84 | `RTC_VBAT` | `VCC3V3` |
| 85, 86 | `ESP_3V3` | `VCC3V3` (C89 0.1uF + C90 22uF) |
| 87 | `CHIP_PU` | R22 10K lên VCC3V3, C56 1uF xuống GND, nút **RST1**, `RTS` của CH343P |

---

## 3. Bus I2C dùng chung — GPIO7 (SDA) / GPIO8 (SCL)

**Chỉ có MỘT bus I2C trên toàn board.** Mọi thiết bị dưới đây nằm chung:

| Thiết bị | Địa chỉ 7-bit | Cách xác định |
|---|---|---|
| Touch panel (ST7123) | `0x55` | LCD1 chân 25/26; firmware đã xác nhận chạy |
| AXP2101 (PMIC) | `0x34` | mặc định của chip (schematic không có chân strap địa chỉ) |
| ES8311 (DAC) | `0x18` | chân `CE` (20) qua R9 10K xuống GND → CE = 0 |
| ES7210 (ADC) | `0x40` | `AD0`(1) và `AD1`(2) nối GND — bảng ngay trong schematic ghi `AD1=0, AD0=0 → 0x80 (8bit) / 0x40 (7bit)` |
| LSM6DS3TR-C (IMU) | `0x6A` | `ADO/SA0`(1) nối GND — schematic chú thích `1101010b = 6A` |
| MCP4725 (DAC 12-bit) | `0x60` | `A0`(6) nối GND |
| Camera SC2336 (SCCB) | theo sensor | FPC1 chân 20/21 |

Pull-up: **R47/R48 = 2.2K lên VCC3V3**, kèm C54/C55 22pF. Với 7 thiết bị trên
cùng một bus nên giữ tốc độ ≤ 400 kHz và để ý tải dung của đường dây.

---

## 4. Màn hình LCD1 — `YDP430BT009-V1` (FPC 30 chân, 0.5 mm)

| Chân | Tên | Nối tới |
|---|---|---|
| 1, 2 | LEDA | `VLED+` (từ boost SY7200) |
| 3 | NC | — |
| 4, 5 | LEDK | `VLED−` |
| 6 | NC | — |
| 7, 10, 13, 16, 19, 29 | GND | GND |
| 8, 9, 20, 24 | NC | — |
| 11 | MIPI_CLKP | module chân 37 `DSI_CLKP` |
| 12 | MIPI_CLKN | module chân 36 `DSI_CLKN` |
| 14 | MIPI_D1P | module chân 34 `DSI_D1P` |
| 15 | MIPI_D1N | module chân 35 `DSI_D1N` |
| 17 | MIPI_D0P | module chân 38 `DSI_D0P` |
| 18 | MIPI_D0N | module chân 39 `DSI_D0N` |
| 21 | LCM_RST | **GPIO22** (`LCD_RST`) |
| 22, 23 | VDD3.3 | `VCC3V3` |
| 25 | TP_SCL | **GPIO8** |
| 26 | TP_SDA | **GPIO7** |
| 27 | TP_RST | **GPIO22** — *cùng net với LCM_RST* |
| 28 | TP_INT | **GPIO23** |
| 30 | TP_VDD3.3 | `VCC3V3` (C93 22uF + C94 0.1uF) |

Cặp vi sai đi thẳng, **không đảo P/N** (đã kiểm tra từng nét dây).

### Đèn nền — U8 `SY7200` (boost dòng không đổi)

```
VCC3V3 ──┬─ C70/C71 10uF ── L2 10uH ── SW(1) ──┐
         └─ VIN(6)                             │
LCD_BL (GPIO6) ── EN(4)   [R39 10K lên VCC3V3] │
                  FB(3) ── VLED− ── R38 5.1R ──┴─ D11 SK24 ── VLED+ (C64 10uF/50V)
```

- Ngưỡng FB ghi trên bản vẽ = **200 mV** → `I_LED = 0.2 / 5.1 ≈ 39 mA` (bản vẽ chú `40mA`) — khớp.
- `EN` có pull-up 10K nên **đèn nền sáng ngay khi cấp nguồn**, kể cả khi GPIO6 đang thả nổi.
  Muốn tắt / chỉnh độ sáng thì GPIO6 phải là **output** (kéo xuống để tắt, PWM để dimming).

---

## 5. Camera — FPC1 `AFC01-S24FCA-00`

| Chân | Nối tới | Chân | Nối tới |
|---|---|---|---|
| 1, 2 | NC | 14 | `CSI_XCLK` |
| 3 | `CSI_A_DATA1_N` → module 45 | 15 | GND |
| 4 | `CSI_A_DATA1_P` → module 46 | 16 | `VCC_1V8` |
| 5 | GND | 17 | `CSI_RESET` |
| 6 | `CSI_A_CLK_N` → module 44 | 18 | NC |
| 7 | `CSI_A_CLK_P` → module 43 | 19 | GND |
| 8 | GND | 20 | `ES_I2C_SDA` (**GPIO7**) |
| 9 | `CSI_A_DATA0_N` → module 41 | 21 | `ES_I2C_SCL` (**GPIO8**) |
| 10 | `CSI_A_DATA0_P` → module 42 | 22 | GND |
| 11, 12 | NC | 23 | NC |
| 13 | GND | 24 | `ALDO2V9` (2.9 V) |
| | | 25, 26 | GND (shield) |

Hai điểm quan trọng cho firmware:

- **`CSI_RESET` KHÔNG nối vào GPIO nào.** Nó chỉ có R32 10K kéo lên `VCC_1V8`
  và C68 0.1uF xuống GND → luôn ở mức cao sau một trễ RC.
  ⇒ giữ `reset_pin = -1`, `pwdn_pin = -1` (đúng như `sdkconfig` của project demo camera).
- **`CSI_XCLK` đến từ dao động thạch anh 24 MHz độc lập** (X1 `XTAL3225`, cấp nguồn bằng
  `3V3_SENSOR`, qua R34 10R và C69 10pF), **không lấy từ ESP32-P4**.
  ⇒ không cần cấu hình LEDC/XCLK trong firmware.

Nguồn camera: `VCC_1V8` (= `ALDO1V8`, C61 0.1uF + C62 10uF),
`ALDO2V9` (C47 0.1uF + C48 10uF), `3V3_SENSOR` (= `VCC3V3` qua R36 0R, chỉ nuôi X1).

---

## 6. Audio

### 6.1 Bus I2S — một bus, hai codec

| Tín hiệu | GPIO | ES8311 | ES7210 |
|---|---|---|---|
| MCLK | **13** | `MCLK` (2) | `MCLK` (5) |
| BCLK / SCLK | **12** | `SCLK/DMIC_SCL` (6) | `SCLK` (9) |
| WS / LRCK | **10** | `LRCK` (8) | `LRCK` (10) |
| DOUT (ESP phát) | **9** | `DSDIN` (9) | — |
| DIN (ESP thu) | **11** | — | `SDOUT1/TDMOUT` (11) |

- **`ASDOUT` (chân 7) của ES8311 KHÔNG nối** → không dùng ADC của ES8311.
  Toàn bộ đường thu đi qua ES7210.
- `SDOUT2/TDMIN` (12), `INT` (13), `DMIC_CLK` (14) của ES7210: không nối.

### 6.2 Đường thu — ES7210

| Kênh | Chân | Nguồn |
|---|---|---|
| MIC1 | 15/16 | mic MEMS **MIC1** `MSM381A3729H9CP` (C31/C32 1uF, R3 0R, bias `ADC_MICBIAS12`) |
| MIC2 | 20/19 | mic MEMS **MIC2** `MSM381A3729H9CP` (C23/C29 1uF, R5 0R) |
| MIC3 | 32/31 | **AEC reference** — lấy từ `DAC_OUT_N/P` của ES8311 qua R10/R7 0R (C5/C9 1uF) |
| MIC4 | 27/28 | **không nối** |

⇒ Cấu hình ES7210 ở chế độ **3 kênh** (2 mic + 1 reference), hoặc 4 kênh TDM và bỏ kênh 4.

### 6.3 Đường phát — ES8311 → NS4150B

```
ES8311 OUTN(13) ─ R6 0R ─ DAC_OUT_N ─ C3 0.1uF ─ R1 150K ─ IN−(4) ┐
ES8311 OUTP(12) ─ R8 0R ─ DAC_OUT_P ─ C4 0.1uF ─ R2 150K ─ IN+(3) ┤ NS4150B (U1)
PA_CTRL (GPIO3) ────────────────────────────────── STD(1)         │
AXP_VSYS ───────────────────────────────────────── VCC(6)         │
                                    VO+(5)/VO−(8) → SPEAKER_P/N ──┴→ J1 MX1.25-2P
```

- Ampli lấy nguồn từ **`AXP_VSYS`** (pin/VBUS), *không* phải 3.3 V → công suất phụ thuộc mức pin.
- **GPIO3 = HIGH để bật ampli.** Bật muộn / tắt sớm để tránh tiếng "pop".
- Nguồn codec: ES8311 `DVDD`/`PVDD` ← `ALDO3V3`, `AVDD` ← `VCC3V3`;
  ES7210 `VDDP/VDDD/VDDA/VDDM` ← `ALDO3V3`.

---

## 7. Thẻ nhớ — J4 `TF-CARD H1.8`

| Chân | Tên | Nối tới |
|---|---|---|
| 1 | DATA2 | `SD_DATA2` = **GPIO41** |
| 2 | CD/DATA3 | `SD_DATA3` = **GPIO42** |
| 3 | CMD | `SD_CMD` = **GPIO44** |
| 4 | VDD | **`ESP_LDO_VO4`** (module chân 66) + C60 0.1uF + D12 ESD |
| 5 | CLK | `SD_CLK` = **GPIO43** |
| 6 | VSS | GND |
| 7 | DATA0 | `SD_DATA0` = **GPIO39** |
| 8 | DATA1 | `SD_DATA1` = **GPIO40** |
| 9 | K (card detect) | **không nối** |
| 10 | GND | GND |

- Bộ chân này **trùng khớp tuyệt đối** với `SDMMC_SLOT0_IOMUX_PIN_NUM_*` của ESP32-P4
  (CLK 43, CMD 44, D0 39, D1 40, D2 41, D3 42) ⇒ dùng **slot 0 + IOMUX**, tốc độ cao nhất,
  không cần khai báo chân thủ công.
- **VDD thẻ nhớ do LDO nội kênh 4 của P4 cấp** ⇒ firmware bắt buộc gọi
  `sd_pwr_ctrl_new_on_chip_ldo()` với `ldo_chan_id = 4` rồi gán vào
  `sdmmc_host_t.pwr_ctrl_handle`; không làm thì thẻ không có nguồn.
- Không có chân card-detect ⇒ phải tự phát hiện bằng cách thử mount.
- Pull-up 51K (R23–R27) lên chính rail `ESP_LDO_VO4`.

---

## 8. USB — ba cổng, hai PHY

| Connector | Loại | D−/D+ | PHY | Vai trò |
|---|---|---|---|---|
| **J3** | USB-C | `ESP_USB_N`/`ESP_USB_P` → module 48/49 (`DM`/`DP`) | USB 2.0 **High-Speed** | cổng device (OTG) |
| **USB1** | `HX 8.5 CB1.9ZB` (USB-A) | cùng `ESP_USB_N`/`ESP_USB_P` | USB 2.0 **High-Speed** | cổng **host**, VBUS = `BOOST_5V` |
| **J5** | USB-C | `USB1P1_N`/`USB1P1_P` → module 50/51 (GPIO24/25) | USB 1.1 Full-Speed | USB-Serial/JTAG, nạp + debug |
| **J6** | USB-C | `TTL_DP`/`TTL_DN` → CH343P | — | cầu USB↔UART (console) |

⚠️ **J3 và USB1 dùng chung một cặp D+/D− trên PHY HS** → chỉ được dùng **một trong hai**
tại một thời điểm. Muốn chạy host qua USB1 thì phải bật `BOOST_ON` (GPIO20) để có 5 V.

VBUS của **cả ba** cổng USB-C (J3, J5, J6) nối chung thành net `VBUS` → `VBUS_IN` → chân
`VBUS` (37) của AXP2101. Cắm bất kỳ cổng nào cũng cấp nguồn / sạc cho board.

### Nạp firmware qua CH343P (U9)

| CH343P | Nối tới | Ý nghĩa |
|---|---|---|
| `TXD` (4) | `ESP32_RX` = GPIO38 | |
| `RXD` (5) | `ESP32_TX` = GPIO37 | |
| `RTS` (13) | `CHIP_PU` | reset |
| `DTRTNOW` (12) | `GPIO35_BOOTMODE` | vào chế độ download |
| `UD+/UD−` (7/8) | `TTL_DP`/`TTL_DN` → J6 | |

`VIO`(1), `VDD5`(3), `V3`(6), `VBUS`(9) đều lấy `VCC3V3`.
Các chân `RI/CTS/DSR/DCD/ACT#`: không nối.

---

## 9. Nút nhấn và LED

| Ký hiệu | Net | GPIO | Mạch |
|---|---|---|---|
| **RST1** | `CHIP_PU` | — | R22 10K lên VCC3V3, C56 1uF xuống GND, nút xuống GND |
| **BTN2** | `GPIO35_BOOTMODE` | **35** | R28 10K lên VCC3V3, D19 ESD → nút **BOOT** |
| **BTN3** | `GPIO0` | **0** | R60 10K lên VCC3V3, D20 ESD → nút **người dùng** |
| **BTN1** | `AXP_PWRON` | — | vào chân `PWRON`(30) của AXP2101, R14 510R + C51 1nF — nút nguồn cứng |
| **WS2812B** | `WS2812_DAT` | **34** | 1 LED RGB, VCC = VCC3V3, C53 0.1uF |
| **D4** (LED trắng 0603) | — | — | `VCC3V3 → R19 2.2K → D4 → GND`, **luôn sáng**, không điều khiển được |
| **D2** (LED đỏ) | `CHGLED` | — | báo sạc, do AXP2101 điều khiển (R13 1K từ `VRTC`) |

---

## 10. Cây nguồn

### 10.1 AXP2101 (U4)

Đầu vào: `VBUS`(37) ← `VBUS_IN` (3 cổng USB-C), `BAT`(33) ← `VCC_BAT_IN` (J2 MX1.25-2P),
`TS`(31) ← NTC 10K `SDNT1005X103F3950FTF`, `VSYS`(34) → `AXP_VSYS`.

| Kênh | Chân | Rail ra | Điện áp | Nuôi cho |
|---|---|---|---|---|
| **DCDC1** | VIN1(23)/LX1(22)/FB1(21) + L1 1uH | **`VCC3V3`** | 3.3 V | module ESP32-P4C5, LCD, touch, CH343P, WS2812B, pull-up I2C, `3V3_SENSOR` |
| DCDC2 | 24/25/26 | — | — | **không dùng** |
| DCDC3 | 6/5/4 | — | — | **không dùng** |
| DCDC4 | 7/8/9 | — | — | **không dùng** |
| ALDOIN | 17 | ← `AXP_VSYS` | — | đầu vào khối ALDO |
| **ALDO1** | 18 | **`ALDO1V8`** = `VCC_1V8` | 1.8 V | camera FPC chân 16 |
| ALDO2 | 19 | — | — | **không dùng** |
| **ALDO3** | 16 | **`ALDO3V3`** | 3.3 V | ES8311, ES7210, LSM6DS3TR-C |
| **ALDO4** | 15 | **`ALDO2V9`** | 2.9 V | camera FPC chân 24 |
| BLDO1/2, DLDO1/2 | 12/14/20/11 | — | — | **không dùng** |
| VRTC | 28 | `VRTC` | — | pull-up IRQ, cổng Q2, LED sạc |

> ⚠️ **Rất dễ nhầm khi viết driver AXP2101:** rail 3.3 V analog ra từ **ALDO3**, rail 2.9 V ra
> từ **ALDO4**, còn **ALDO2 bỏ trống**. Đừng suy số kênh từ tên rail (`ALDO3V3`, `ALDO2V9`).

I2C của AXP: `AXP_IIC_SDA` / `AXP_IIC_SCL` chính là GPIO7 / GPIO8.
`IRQ`(38) → dịch mức Q2 2N7002 → `ESP_IIC_IRQ` = **GPIO21**.
`PWROK`(29) đưa ra test point TP2.

### 10.2 Boost 5 V — U7 `SY7088`

`AXP_VSYS` → L3 2.2uH → `LX`(1,2); `IN`(6) ← `AXP_VSYS`; `EN`(3) ← R44 1K ← **`BOOST_ON` (GPIO20)**;
`OUT`(8) → **`BOOST_5V`**; hồi tiếp R45 33K / R46 10K.

`BOOST_5V` nuôi: VBUS của cổng host USB1, `VCC` của SN65HVD3082E (RS485),
`VDD` của MCP4725, và JP1 chân 2.

### 10.3 Nguồn module 4G

`VCC_BAT_IN` → Q3 `AO3401A` (P-FET, R18 75K kéo cổng lên) → `4G_VBAT` → `VBAT`(42,43) của ML307R.
Cổng Q3 do Q1 `SS8050` kéo xuống, base Q1 = **`4G_VABT_EN` (GPIO4)**.
⇒ **GPIO4 = HIGH thì module 4G có điện.** Tụ đệm: C67 47uF, C80 47uF, C81 33pF, C82 100nF.

---

## 11. Các khối ngoại vi khác

### 11.1 RS485 — IC2 `SN65HVD3082EDGKR`

| Chân | Tên | Nối tới |
|---|---|---|
| 1 | `R out` | `RS485_RX` = **GPIO33** |
| 2+3 | `RE` + `DE` (nối chung) | `RS485_DE` = **GPIO32** (HIGH = phát) |
| 4 | `D in` | `RS485_TX` = **GPIO31** |
| 6 | `A` | → R51 0R → `RS485_AA` → JP1 chân 20 |
| 7 | `B` | → R50 0R → `RS485_BB` → JP1 chân 19 |
| 8 | `VCC` | `BOOST_5V` (C83 0.1uF) |

Trở kết cuối **R52 120R** giữa `RS485_AA` và `RS485_BB`, bảo vệ **D15 `PESD1CAN`**.
⚠️ Transceiver chạy 5 V ⇒ RS485 chỉ hoạt động khi `BOOST_ON` (GPIO20) đang bật.

### 11.2 IMU — IC1 `LSM6DS3TR-C`

`VDDIO`(5) và `VDD`(8) ← `ALDO3V3` (C84 0.1uF); `SDA`(14) = GPIO7; `SCL`(13) = GPIO8;
`ADO/SA0`(1) + `SDX`(2) + `SCX`(3) → GND ⇒ **địa chỉ 0x6A**;
`INT1`(4), `INT2`(9) **không nối** ⇒ không có ngắt, phải polling.

### 11.3 DAC — `MCP4725`

`VOUT`(1) → net `DAC` → JP1 chân 1; `VDD`(3) ← `BOOST_5V` (C85/C87 22uF, C86/C88 0.1uF);
`A0`(6) → GND ⇒ **0x60**; `SCL`(5) = GPIO8; `SDA`(4) = GPIO7.

### 11.4 Module 4G — U11 `ML307R-DL`

| ML307R | Net | Phía ESP |
|---|---|---|
| `UART0_TXD` (18) | `4G_UART_TX` → Q4 → `4G_ESP_RX` | **GPIO52** (RX) |
| `UART0_RXD` (17) | `4G_UART_RX` ← Q4 ← `4G_ESP_TX` | **GPIO53** (TX) |
| `UART0_DTR` (19) | `4G_UART_DTR` ← Q5 SS8050 ← `4G_ESP_DTR` | **GPIO51** (đảo mức) |
| `VBAT` (42,43) | `4G_VBAT` | bật bằng **GPIO4** |
| `ANT_MAIN` (35) | IPEX1 | |
| `USIM_*` (11–14) | `SIM0_DATA/RESET/CLK/PWR` → CARD1 + D14 ESD | |
| `USIM_DET` (79) | R49 10K → GND | |
| `BOOT_MODE` (82) | TP1 (test point) | |
| `RESET` (15) | `4G_RESET` | **không nối vào ESP** |
| `GPIO4` (16) | `4G_NET_LED` | **không nối vào ESP** |

Level shifter Q4 `LMBT3904DW1T1G` chuyển giữa 3.3 V (ESP) và `VDD_EXT` (module 4G),
base hai transistor đều kéo qua R37/R40 4.7K lên `VDD_EXT`.

### 11.5 Header mở rộng JP1 (2×10)

| Chân | Tín hiệu | Chân | Tín hiệu |
|---|---|---|---|
| 1 | `DAC` (ra của MCP4725) | 2 | `BOOST_5V` |
| 3 | GND | 4 | `VCC3V3` |
| 5 | **GPIO50** (R55 100R) | 6 | **GPIO49** (R56) |
| 7 | **GPIO48** (R57) | 8 | **GPIO47** (R58) |
| 9 | **GPIO46** (R59) | 10 | GND |
| 11 | **GPIO45** (R61) | 12 | **GPIO26** (R62) |
| 13 | **GPIO27** (R63) | 14 | **GPIO28** (R64) |
| 15 | **GPIO29** (R65) | 16 | **GPIO30** (R66) |
| 17 | GND | 18 | GND |
| 19 | `RS485_BB` | 20 | `RS485_AA` |

Mọi chân GPIO trên header đều có **100R nối tiếp** để chống chập.

**Gán cho bo cảm biến 5 khe (đề xuất 2026-09-17, `board_esp32p4_43lcd.h` khối `BOARD_SENSOR_*` —
chưa có schematic bo con):** chân 12/13 = GPIO26/27 SDA/SCL bus I2C_NUM_1 riêng (mux TCA9548 0x70
kênh 0..4, TCS34725 0x29 ×5) · chân 14/15/16/11/**8** = GPIO28/29/30/45/**47** enable LED khe 1..5 ·
chân 9 = GPIO46 PWM chung · chân 4 VCC3V3, 3/10 GND. Còn trống: GPIO48/49/50 (chân 7/6/5). Nguồn LED
(VCC3V3 hay BOOST_5V chân 2) và dòng qua 100R phải đo trên bo thật; LED cần dòng lớn → transistor trên bo con.

**Kiến trúc ghép với bo LED + bo cảm biến ĐÃ CÓ (2026-09-18):** `HARDWARE-ARCHITECTURE.md` — P4C5 không có
12 V/LDD/transistor như bo main ESP32 cũ nên cần **bo giao tiếp `Rapid4P-IF`** cắm JP1 (nguồn LED 5 V + công tắc
N khe + pull-up I2C + cấp 5 V ngược cho P4C5 qua USB-C); netlist hai bo cũ và 5 phép đo phải làm ở đó.

---

## 12. Đối chiếu với `main/boards/board_esp32p4_43lcd.h`

Header hiện tại ghi *"AUDIO / SD / SERVO / LED: board P4 CHƯA có schematic"*. Schematic này
lấp đầy toàn bộ chỗ trống đó. Các giá trị **cần sửa**:

| Macro | Đang là | Theo schematic |
|---|---|---|
| `BOARD_LCD_PIN_RST` | `-1` | **`22`** (dùng chung với touch) |
| `BOARD_LCD_PIN_BL` | `-1` | **`6`** |
| `BOARD_LCD_HAS_BACKLIGHT` | `0` | **`1`** (EN có pull-up nên mặc định sáng) |
| `BOARD_TOUCH_RST_GPIO` | `-1` | **`22`** — *cùng net với LCD RST* |
| `BOARD_TOUCH_INT_GPIO` | `-1` | **`23`** |
| `BOARD_AUDIO_I2S_MCLK` | `-1` | **`13`** |
| `BOARD_AUDIO_I2S_BCLK` | `-1` | **`12`** |
| `BOARD_AUDIO_I2S_WS` | `-1` | **`10`** |
| `BOARD_AUDIO_I2S_DIN` | `-1` | **`11`** |
| `BOARD_AUDIO_I2S_DOUT` | `-1` | **`9`** |
| `BOARD_AUDIO_PA_PIN` | `-1` | **`3`** (active HIGH — `BOARD_AUDIO_PA_ON_LEVEL 1` đã đúng) |
| `BOARD_AUDIO_CODEC_I2C_ADDR` | `0x18` | đúng (CE nối GND) |
| `BOARD_BTN_BOOT_GPIO` | `35` | đúng |
| `BOARD_BTN_STOP_GPIO` | `-1` | có thể dùng **`0`** (BTN3) |
| `BOARD_LED_GPIO` | `-1` | **`34`** — nhưng là **WS2812B**, cần driver RMT/SPI chứ không phải GPIO thường |
| `BOARD_POWER_HOLD_GPIO` | `-1` | không có chân giữ nguồn; nguồn do AXP2101 quản lý (`PWRON`/`PWROK`) |
| `BOARD_CHARGING_DET_GPIO` | `-1` | không có chân riêng; đọc trạng thái sạc qua **AXP2101 trên I2C**, ngắt ở **GPIO21** |

Còn thiếu, nên bổ sung:

```c
/* ADC mic — ES7210 dùng chung bus I2C/I2S với ES8311 */
#define BOARD_AUDIO_USE_ES7210       1
#define BOARD_AUDIO_ADC_I2C_ADDR     0x40
#define BOARD_AUDIO_ADC_MIC_CH       2     /* MIC1 + MIC2 */
#define BOARD_AUDIO_ADC_AEC_CH       1     /* MIC3 = reference từ DAC_OUT */

/* PMIC */
#define BOARD_PMIC_USE_AXP2101       1
#define BOARD_PMIC_I2C_ADDR          0x34
#define BOARD_PMIC_IRQ_GPIO          21
/* Ánh xạ rail: ALDO1=1V8(camera) ALDO3=3V3(codec) ALDO4=2V9(camera). ALDO2 bỏ trống. */

/* Thẻ nhớ — SDMMC slot 0 IOMUX, VDD từ LDO nội kênh 4 */
#define BOARD_SD_SLOT                0
#define BOARD_SD_BUS_WIDTH           4
#define BOARD_SD_LDO_CHAN            4
#define BOARD_SD_PIN_CLK             43
#define BOARD_SD_PIN_CMD             44
#define BOARD_SD_PIN_D0              39
#define BOARD_SD_PIN_D1              40
#define BOARD_SD_PIN_D2              41
#define BOARD_SD_PIN_D3              42

/* IMU / RS485 / boost / 4G */
#define BOARD_IMU_I2C_ADDR           0x6A   /* LSM6DS3TR-C, không có chân INT */
#define BOARD_RS485_TX_GPIO          31
#define BOARD_RS485_RX_GPIO          33
#define BOARD_RS485_DE_GPIO          32
#define BOARD_BOOST5V_EN_GPIO        20     /* bật thì RS485/DAC/USB-host mới chạy */
#define BOARD_LTE_TX_GPIO            53
#define BOARD_LTE_RX_GPIO            52
#define BOARD_LTE_DTR_GPIO           51     /* qua Q5 → đảo mức */
#define BOARD_LTE_PWR_EN_GPIO        4      /* HIGH = cấp nguồn module 4G */
```

---

## 13. Lưu ý khi lập trình và các điểm cần kiểm chứng trên board thật

**Ràng buộc chắc chắn (đọc trực tiếp từ schematic):**

1. **GPIO22 reset CẢ panel LẪN touch.** Không thể reset riêng touch. Chỉ reset một lần lúc
   khởi tạo, trước khi mở DSI; sau đó không toggle nữa nếu không muốn màn chớp.
2. **Một bus I2C duy nhất** cho 7 thiết bị (§3). Nếu dùng chung với SCCB của camera thì phải
   truyền cùng một `i2c_master_bus_handle_t` vào driver camera, đừng khởi tạo bus hai lần.
3. **`BOOST_ON` (GPIO20) là điều kiện tiên quyết** cho RS485, MCP4725 và cổng USB host.
4. **Thẻ nhớ không có nguồn nếu chưa bật LDO nội kênh 4** của ESP32-P4.
5. **Không có chân ADC đo pin.** Mọi thông tin pin/sạc phải hỏi AXP2101 qua I2C.
6. **`ESP_LDO_VO4` (module chân 66) đã bị chiếm** cho thẻ nhớ — không dùng cho việc khác.
7. GPIO1, GPIO5, GPIO36, GPIO54 hoàn toàn trống (GPIO36 có sẵn pull-up 10K) — dành cho mở
   rộng, nhưng **chỉ có ở chân module**, không ra header.

**Cần đo / kiểm tra khi bring-up (schematic có điểm đáng ngờ):**

8. **`CS` (chân 12) của LSM6DS3TR-C thả nổi.** Con IMU này chọn I2C khi `CS` ở mức cao.
   Nếu quét I2C không thấy `0x6A`, khả năng cao là do chân này — cần hàn pull-up lên `VDDIO`.
   *(Một số phiên bản LSM6DS3 có pull-up nội trên CS nên vẫn có thể chạy — phải thử mới biết.)*
9. **MCP4725 chạy `VDD = BOOST_5V` (5 V) nhưng bus I2C chỉ kéo lên 3.3 V.**
   Ngưỡng `VIH` của MCP4725 là 0.7 × VDD = 3.5 V > 3.3 V ⇒ có thể không nhận lệnh.
   Nếu DAC không phản hồi, đây là nguyên nhân đầu tiên nên nghi. Cũng nên kiểm tra khi
   `BOOST_5V` **tắt**: diode ESD của MCP4725 có thể ghì bus I2C.
10. **Chia áp hồi tiếp của boost SY7088: R45 33K / R46 10K.**
    Với `V_FB = 0.6 V` (giá trị phổ biến của dòng SY708x) thì
    `Vout = 0.6 × (1 + 33/10) ≈ 2.6 V`, **không phải 5 V** như tên net `BOOST_5V` gợi ý.
    Nên **đo thực tế điện áp tại `BOOST_5V`** trước khi cắm thiết bị USB host.
    *(Mạch backlight SY7200 ở §4 tính ra đúng 40 mA như bản vẽ chú thích, nên cách đọc bản
    vẽ là tin được — chỗ này thực sự cần đo.)*
11. Pull-up thẻ nhớ 51K hơi cao so với thông lệ 10K–47K; ở tần số SDMMC cao có thể cần hạ
    xuống nếu gặp lỗi CRC.

---

## 14. Danh mục connector

| Ký hiệu | Loại | Công dụng |
|---|---|---|
| J1 | MX1.25-2P | Loa (`SPEAKER_P/N`) |
| J2 | MX1.25-2P | Pin Li-ion (`VCC_BAT_IN`), có D1 chống ngược |
| J3 | USB Type-C | USB 2.0 HS (device) |
| J4 | TF-CARD H1.8 | Thẻ microSD |
| J5 | USB Type-C | USB 1.1 FS — Serial/JTAG, nạp firmware |
| J6 | USB Type-C | CH343P — console UART |
| USB1 | HX 8.5 CB1.9ZB | USB-A host, VBUS = `BOOST_5V` |
| JP1 | Header 2×10 | GPIO mở rộng + RS485 + DAC (§11.5) |
| LCD1 | FPC 30P 0.5 mm | Màn hình + touch |
| FPC1 | AFC01-S24FCA-00 | Camera MIPI-CSI |
| CARD1 | Khay SIM 6P | SIM cho ML307R |
| IPEX1 | IPEX/U.FL | Anten module 4G |
| SP1–SP4 | Trụ đồng M3 | Bắt vít |

---

## 15. Board thứ hai — AI-IoT VN **ES3N28P-LCD-2.8** (ESP32-S3 + LCD 2.8", khoá `rapid4p-s3`)

> Thêm 2026-09-19. **Nguồn:** `firmware-vimate/main/boards/board_esp32s3_28lcd.h` (Bizgeni, biến thể
> `s3-28lcd` đã build + chạy thật 12/09/2026; bản chép trong repo
> `firmware/maping new product/firmware-vimate-p4/main/boards/board_esp32s3_28lcd.h`), gốc từ
> `xiaozhi-esp32_vietnam/.../es3n28p-lcd-2.8/config.h`. **Chưa có schematic** — mọi chân ngoài bảng
> "board đã dùng" là ĐỀ XUẤT, phải đối chiếu header thật trước khi hàn bo cảm biến.
> Header firmware: `main/boards/board_esp32s3_28lcd.h`.

### 15.1 Board đã dùng (chắc chắn, theo vimate)

| GPIO | Chức năng | Ghi chú |
|---|---|---|
| 0 | BOOT | tap = quay lại, giữ 5 s = xoá WiFi |
| 1 | PA enable loa (active LOW) | Rapid4P không dùng audio |
| 4 / 5 / 7 / 6 / 8 | I2S0 MCLK / BCLK / WS / DIN / DOUT (ES8311) | không dùng |
| 10 / 11 / 12 / 13 | LCD CS / MOSI / SCLK / MISO (SPI3, 40 MHz) | ILI9341, RST nối reset hệ thống |
| 46 | LCD DC | strap — đã bị board dùng |
| 45 | LCD backlight (LEDC ch0/timer0, 5 kHz) | strap — đã bị board dùng |
| 15 / 16 | I2C0 SCL / SDA | touch FT6236G 0x38 (+ codec ES8311 0x18, không init) |
| 17 / 18 | touch INT / RST | RST cần xung LOW 10 ms → HIGH 300 ms |
| 42 | LED đơn | `BOARD_STATUS_LED_GPIO`, chưa có driver |
| 19 / 20 | USB D− / D+ | Serial/JTAG (console phụ) |
| 43 / 44 | UART0 TX / RX | console chính + nạp qua USB-UART |
| 26–37 | flash QIO + PSRAM Octal (N16R8) | KHÔNG dùng |
| 3 | strap JTAG | tránh |

### 15.2 GPIO còn trống → gán cho bo cảm biến 5 khe (ĐỀ XUẤT)

Trống: **2, 9, 14, 21, 38, 39, 40, 41, 47, 48** (10 chân).

| Chức năng | GPIO | Knob |
|---|---|---|
| I2C bus cảm biến — **chung I2C0 với touch** (SDA 16 / SCL 15, mux+TCS 100 kHz, touch 400 kHz) | 16 / 15 | `BOARD_SENSOR_I2C_SDA/SCL` (`I2C_NUM_0`) |
| LED enable khe 1..5 | 2, 9, 14, 21, 38 | `BOARD_SLOT_LED_GPIOS` |
| PWM độ sáng LED chung (LEDC ch1/timer1, 5 kHz) | 39 | `BOARD_SLOT_LED_PWM_GPIO` |
| **Nút XANH / ĐỎ / TRẮNG** của vỏ máy (pull-up nội, nhấn = LOW; ReaderPlus cũ 16/13/4) | 47 / 48 / 41 | `BOARD_BTN_GREEN/RED/WHITE_GPIO` (−1 nếu không nối) |
| Dư | 40 | — |

- Cảm biến đi chung I2C0 để dành GPIO41 cho nút TRẮNG (2026-09-20); `sensor_bus.c` tái dùng bus qua
  `i2c_master_get_bus_handle()`, driver `i2c_master` khoá theo giao dịch. Muốn tách bus: `I2C_NUM_1` trên 40/41 và
  chuyển nút TRẮNG sang GPIO40.

**Audio — loa bíp (2026-09-20, `main/audio/beep.c`, `CONFIG_RAPID4P_BEEP`)** — phần cứng có sẵn trên ES3N28P:

| Chức năng | GPIO | Knob |
|---|---|---|
| Codec ES8311 (I2C0 chung, 0x18) | 16 / 15 | `BOARD_AUDIO_CODEC_I2C_*` |
| I2S0 MCLK / BCLK / WS / DOUT (DIN 6 = mic, không dùng) | 4 / 5 / 7 / 8 | `BOARD_AUDIO_I2S_*` |
| PA enable — **ACTIVE-LOW** (LOW = bật amp) | 1 | `BOARD_AUDIO_PA_PIN`, `BOARD_AUDIO_PA_ON_LEVEL 0` |
| Loa 8 Ω ~1 W nối ngõ PA của board (vỏ máy Rapid gắn loa nhỏ) | — | `BOARD_BEEP_VOLUME 80` |

Bíp: phím 2 kHz 40 ms · đo xong 1,2 kHz + 1,6 kHz · lỗi 400 Hz 400 ms · boot 1 kHz + 1,5 kHz. Không có codec → `beep_init`
WARN một lần, không bíp, máy chạy bình thường.
- Bo con (TCA9548A 0x70 + 5× TCS34725 0x29, LED VOUT+ chung + LIGHT1..5 sink) cần bo giao tiếp
  như `Rapid4P-IF` (`docs/HARDWARE-ARCHITECTURE.md`): nguồn LED, công tắc khe, pull-up 4,7 K.
- Không có PMIC/pin/SD trên board; WiFi 2,4 GHz nội (không ESP-Hosted), BT tắt trong sdkconfig.
