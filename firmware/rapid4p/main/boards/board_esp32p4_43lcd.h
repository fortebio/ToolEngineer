/**
 * Board: FBT ESP32-P4C5 + 4.3" LCD ST7102 MIPI-DSI (480×800 portrait native)
 * — phần cứng tham chiếu của Rapid4P (RAPID READER 4 SLOT).
 *
 * Kế thừa từ firmware-vimate-p4/main/boards/board_esp32p4_43lcd.h (bản 15/09/2026,
 * mọi số đo/ghi chú dưới đây là của cây đó); Rapid4P bỏ knob mặt robot/emoji, thêm
 * khối "Bo cảm biến quang 4 slot" + nút ĐO ở cuối file. Xem
 * `firmware/maping new product/MAPPING-Rapid4P.md`.
 *
 * Nguồn pinout: 3 project tham chiếu chính chủ của board trong
 *   `ESP-IDF 5/P4-IDF_ST7102-MIPI_ESP-LVGL-PORT_V9`
 *   `ESP-IDF 5/Screen and camera code/esp32p4-idf5_st7102-mipi-dsi_sc2336-mipi-csi_...`
 *   `ESP-IDF 5/Screen tear prevention code/p4-idf_st7102-mipi_lvgl-common-demo`
 *
 * Từ 10/09/2026 có thêm `docs/HARDWARE-PINOUT.md` — đọc từ schematic thật của
 * board FBT. Đó là NGUỒN CHUẨN cho mọi chân dưới đây; ba project demo ở trên chỉ
 * còn giá trị tham khảo cho phần LCD/camera.
 *
 * - LCD: ST7102 qua MIPI-DSI, 2 lane, 520 Mbps, DPI clock 37.8 MHz (60 Hz).
 *   VDD_MIPI_DPHY lấy từ LDO nội kênh 3 @ 2.5V — BẮT BUỘC acquire trước khi
 *   tạo DSI bus, nếu không esp_lcd_new_dsi_bus() fail.
 * - Touch: ST7123 I2C addr 0x55, SDA=GPIO7 / SCL=GPIO8.
 *   Giao thức register 16-bit GIỐNG HỆT touch tích hợp ST77922 (0x0010 info,
 *   0x0009 max points, 0x0014 report 7 byte/điểm) → dùng chung code path.
 * - Camera SC2336 MIPI-CSI dùng CHUNG bus SCCB GPIO7/8. Chưa bật ở bản
 *   bring-up này (xem README-P4.md).
 *
 * ⚠ GPIO22 reset CẢ panel LẪN touch (một net `TP_RST`=`LCD_RST`). Vì thế chỉ
 *   display.c được phép giữ chân này; BOARD_TOUCH_RST_GPIO cố tình để -1 —
 *   xem chú thích ở khối Touch.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ========================== LCD (MIPI-DSI, ST7102) ========================== */
#define BOARD_LCD_USE_MIPI_DSI       1
#define BOARD_LCD_USE_SPI            0
#define BOARD_LCD_USE_ST7102         1

/* DSI bus */
#define BOARD_LCD_DSI_BUS_ID         0
#define BOARD_LCD_DSI_LANES          2
#define BOARD_LCD_DSI_LANE_MBPS      520
/* LDO nội cấp VDD_MIPI_DPHY. Kênh 3 = LDO_VO3 theo mọi demo của board. */
#define BOARD_LCD_DSI_PHY_LDO_CHAN   3
#define BOARD_LCD_DSI_PHY_LDO_MV     2500

/* Panel native DỌC 480×800 (DPI video mode, không xoay được bằng phần cứng).
 * UI VIMATE thiết kế cho màn NGANG (S3: 320×240, 480×320) nên từ 12/09/2026 chạy
 * landscape 800×480: LVGL vẽ 800×480 vào 2 buffer riêng (direct mode), display.c
 * xoay từng vùng bẩn bằng PPA (bộ xoay/scale phần cứng của P4) vào frame buffer
 * DPI đang ẩn rồi đổi khung đúng vsync — vẫn chống xé hình (README-P4 §6.6).
 * BOARD_LCD_H_RES/V_RES = kích thước LOGICAL (LVGL); NATIVE_W/H = panel. */
#define BOARD_LCD_NATIVE_W           480
#define BOARD_LCD_NATIVE_H           800
/* Góc xoay theo quy ước LVGL (LV_DISPLAY_ROTATION_x = panel bị XOAY x° theo chiều
 * kim đồng hồ so với hướng dọc tự nhiên). Chỉ nhận 0 / 90 / 270:
 *   270 → cầm ngang với cạnh TRÊN của panel dọc nằm bên TRÁI (cổng USB-C bên
 *         phải) — đúng tư thế trong ảnh chụp 11/09/2026.
 *    90 → ngược lại (USB bên trái). Hình bị lộn đầu thì đổi 270 ↔ 90.
 *     0 → dọc 480×800 như bản bring-up. */
#define BOARD_LCD_ROTATION           270
#if BOARD_LCD_ROTATION == 90 || BOARD_LCD_ROTATION == 270
#define BOARD_LCD_H_RES              BOARD_LCD_NATIVE_H   /* 800 */
#define BOARD_LCD_V_RES              BOARD_LCD_NATIVE_W   /* 480 */
#elif BOARD_LCD_ROTATION == 0
#define BOARD_LCD_H_RES              BOARD_LCD_NATIVE_W
#define BOARD_LCD_V_RES              BOARD_LCD_NATIVE_H
#else
#error "BOARD_LCD_ROTATION chi nhan 0 / 90 / 270"
#endif
#define BOARD_LCD_BITS_PER_PIXEL     16
#define BOARD_LCD_DPI_CLK_MHZ        37.8
#define BOARD_LCD_INVERT_COLOR       0
#define BOARD_LCD_USE_BGR            0     /* ELEMENT_ORDER_RGB */
#define BOARD_LCD_SWAP_XY            0
#define BOARD_LCD_MIRROR_X           0
#define BOARD_LCD_MIRROR_Y           0
/* DPI panel nhận RGB565 little-endian thẳng từ LVGL — KHÔNG swap byte như SPI. */
#define BOARD_LCD_SWAP_BYTES         0

/* GPIO22 = net `LCD_RST` — CÙNG net với `TP_RST` của touch (schematic §13.1).
 * Trên board chỉ có RC (R70 10K + C95 1uF) nên panel CHỈ tự reset khi cấp nguồn;
 * reset MCU để nguyên panel ở trạng thái cũ (đã kẹt thật 11/09/2026, README §6.2).
 * display.c reset cứng hai lần, đều TRƯỚC touch_init(): một lần trước khi mở DSI
 * (tự làm) và một lần trong esp_lcd_panel_reset() của driver, nên touch cũng về
 * trạng thái sạch. Đừng toggle lại sau đó: màn sẽ chớp. */
#define BOARD_LCD_PIN_RST            22
/* GPIO6 = EN của SY7200 (boost dòng không đổi cho đèn nền). R39 10K kéo lên
 * VCC3V3 nên đèn nền MẶC ĐỊNH SÁNG ngay cả khi ESP chưa cấu hình chân — an toàn
 * lúc boot. Điều khiển bằng PWM LEDC để chỉnh độ sáng và tự tắt màn khi rảnh. */
#define BOARD_LCD_PIN_BL             6
#define BOARD_LCD_HAS_BACKLIGHT      1
#define BOARD_LCD_BL_ON_LEVEL        1
#define BOARD_LCD_BL_FREQ_HZ         5000
/* Board chỉ có MỘT chân đèn nền. Phải tắt tường minh: display.c mặc định
 * BOARD_LCD_EXTRA_BL_ENABLED = 1 và BOARD_LCD_EXTRA_BL_GPIO = GPIO_NUM_NC (-1),
 * nên khi HAS_BACKLIGHT bật lên thì khối "đèn nền phụ" được biên dịch với
 * `1ULL << (unsigned)-1` → cảnh báo shift-count-overflow. Runtime vẫn an toàn
 * (có early-return) nhưng đó là code chết, tắt hẳn cho gọn. */
#define BOARD_LCD_EXTRA_BL_ENABLED   0

/* ========================== Touch ST7123 (I2C) ========================== */
#define BOARD_TOUCH_USE_ST7123       1
#define BOARD_TOUCH_USE_FT6236       0
#define BOARD_TOUCH_I2C_NUM          I2C_NUM_0
#define BOARD_TOUCH_I2C_SCL          8
#define BOARD_TOUCH_I2C_SDA          7
#define BOARD_TOUCH_INT_GPIO         23
/* CỐ TÌNH để -1 dù schematic có chân reset: đó là GPIO22, DÙNG CHUNG với
 * LCD_RST. Nếu touch.c cũng giữ chân này thì nó sẽ reset touch SAU khi DSI đã
 * chạy → panel chớp hoặc mất tín hiệu. Cú reset của display.c (chạy trước
 * touch_init) đã phục vụ cả hai. */
#define BOARD_TOUCH_RST_GPIO         -1
#define BOARD_TOUCH_I2C_ADDR         0x55
/* INT GPIO23 — đo 13/09/2026 hai đợt cho hai kết quả trái nhau: đợt quét I2C mỗi
 * 10 ms → int=0 ở 35/35 lần DOWN; đợt có cổng INT (bỏ đọc I2C 2/3 chu kỳ) → int=1 ở
 * đa số lần DOWN dù ngón đang chạm. ⇒ INT là XUNG quanh mỗi lần chip có báo cáo mới
 * (hoặc được xoá khi host đọc), KHÔNG giữ mức suốt lúc chạm. Cổng INT vì thế chỉ
 * làm DOWN trễ thêm ≤ 20 ms mà không tiết kiệm gì đáng kể (1 byte I2C/10 ms) → TẮT.
 * Muốn dùng INT thật sự: ngắt cạnh xuống đánh thức task (chưa làm). */
#define BOARD_TOUCH_INT_ACTIVE_LOW   0
/* Task quét chạm 7 = TRÊN taskLVGL/display (6, cùng core 0). Ở 5 nó bị khối render
 * emoji (GIF 400 px, ~150 ms/khung) + chờ lock đè: log 13/09 cho thấy chạm 200 ms chỉ
 * lọt 1 mẫu. Mỗi vòng quét ≤ 1 ms I2C (block trên semaphore driver) nên không cướp
 * CPU của LVGL. Audio ở core 1 nên 7 không đụng. */
#define BOARD_TOUCH_TASK_PRIO        7
/* Touch trả toạ độ trong hệ NATIVE của panel (dọc 480×800, px∈[0,480) py∈[0,800)).
 * touch.c map sang LVGL logical theo thứ tự: swap → mirror_x (theo H_RES logical)
 * → mirror_y (theo V_RES logical). Suy từ BOARD_LCD_ROTATION, cùng phép xoay mà
 * display.c dùng cho PPA:
 *   270: logical(lx,ly) = (py, 479-px)  → swap, mirror_y
 *    90: logical(lx,ly) = (799-py, px)  → swap, mirror_x
 * Nếu chạm lệch TRỤC (ấn trái ra phải) thì knob mirror sai; nếu lệch 90° thì
 * swap sai — chỉnh ở đây, không sửa touch.c. */
#if BOARD_LCD_ROTATION == 270
#define BOARD_TOUCH_SWAP_XY          1
#define BOARD_TOUCH_MIRROR_X         0
#define BOARD_TOUCH_MIRROR_Y         1
#elif BOARD_LCD_ROTATION == 90
#define BOARD_TOUCH_SWAP_XY          1
#define BOARD_TOUCH_MIRROR_X         1
#define BOARD_TOUCH_MIRROR_Y         0
#else
#define BOARD_TOUCH_SWAP_XY          0
#define BOARD_TOUCH_MIRROR_X         0
#define BOARD_TOUCH_MIRROR_Y         0
#endif

/* ========================== Audio (docs/HARDWARE-PINOUT.md §6) ==========================
 *
 * MỘT bus I2S nuôi HAI codec:
 *   - phát: ESP → ES8311 (`DSDIN`) → NS4150B → loa J1
 *   - thu : 2 mic MEMS + 1 đường AEC reference → ES7210 (`SDOUT1`) → ESP
 *
 * `ASDOUT` (chân 7) của ES8311 KHÔNG nối trên board này ⇒ ADC của ES8311 vô
 * dụng, TOÀN BỘ đường thu phải đi qua ES7210. Vì vậy BOARD_AUDIO_USE_ES7210_ADC
 * bắt buộc = 1; để 0 là mic câm hoàn toàn.
 *
 * Ampli NS4150B lấy nguồn từ `AXP_VSYS` (pin/VBUS) chứ không phải 3.3V ⇒ công
 * suất ra phụ thuộc mức pin. */
#define BOARD_AUDIO_RUNTIME_ENABLE   1
/* Beep 880Hz 450ms lúc boot để kiểm loa + amp; mic idle probe (bản diag) nghe
 * lại được → xác nhận cả hai đường qua không khí. Đã kiểm 11/09/2026: mic ghi
 * rms 1867..2963 / peak ~7500 đúng cửa sổ beep. Bật 1 khi cần kiểm lại. */
#define BOARD_AUDIO_BOOT_TEST_BEEP   0

#define BOARD_AUDIO_USE_ES8311       1
#define BOARD_AUDIO_USE_ES7210_ADC   1
#define BOARD_AUDIO_I2S_NUM          I2S_NUM_0
#define BOARD_AUDIO_I2S_MCLK         13
#define BOARD_AUDIO_I2S_BCLK         12    /* SCLK, chung ES8311 + ES7210 */
#define BOARD_AUDIO_I2S_WS           10    /* LRCK, chung ES8311 + ES7210 */
#define BOARD_AUDIO_I2S_DIN          11    /* ES7210 SDOUT1 -> ESP (mic) */
#define BOARD_AUDIO_I2S_DOUT         9     /* ESP -> ES8311 DSDIN (loa) */
/* GPIO3 = `PA_CTRL` vào chân STD của NS4150B. HIGH = bật ampli. Bật muộn / tắt
 * sớm để tránh tiếng "pop" — vimate_es8311.c đã xử lý bằng fade + silence. */
#define BOARD_AUDIO_PA_PIN           3
#define BOARD_AUDIO_PA_ON_LEVEL      1
/* KHÔNG định nghĩa BOARD_AUDIO_TCA9555_I2C_ADDR / _PA_TCA9555_EXIO: board này
 * điều khiển ampli bằng GPIO thẳng, không qua IO expander như bo GENU v6. */

#define BOARD_AUDIO_CODEC_I2C_NUM    I2C_NUM_0
#define BOARD_AUDIO_CODEC_I2C_SCL    8     /* dùng chung bus touch/SCCB/PMIC */
#define BOARD_AUDIO_CODEC_I2C_SDA    7
#define BOARD_AUDIO_CODEC_I2C_ADDR   0x18  /* ES8311, chân CE nối GND */
#define BOARD_AUDIO_ADC_I2C_ADDR     0x40  /* ES7210 */

/* Thu và phát PHẢI cùng sample rate: ES8311 và ES7210 treo trên CÙNG MỘT bus
 * I2S (chung BCLK GPIO12 + LRCK GPIO10), nên chỉ có một tần số khả dĩ. Để lệch
 * 16000/24000 thì driver báo thẳng:
 *   E I2S_IF: Current mode record conflict sample_rate 16000 with peer mode
 *             sample_rate 24000
 * và đường thu không lên. 24000 khớp với bo GENU v6 (cũng ES8311+ES7210) và với
 * Opus encoder đang chạy 24000Hz. */
#define BOARD_MIC_SAMPLE_RATE        24000
#define BOARD_SPK_SAMPLE_RATE        24000

/* Tinh chỉnh VAD/WakeNet — ĐO trên board 11/09/2026 bằng "mic idle probe" (bản
 * diag, audio_pipeline.c), phòng làm việc yên, mic gain 80% = 28dB, 2 mic
 * sum-bão-hoà:
 *   nền sau HPF 120Hz : rms 60–100, nhảy ngắn 150–200, DC ≈ 0
 *   beep loa 880Hz 90%: rms ~1900–3000, peak ~7500 (mic nghe được loa)
 * Ngưỡng VAD mặc định 160 (audio_pipeline.c) đứng ~2× nền — ổn. Nhưng server
 * có thể hạ ngưỡng theo profile ("quiet" -10, "far" -24, "child_soft" -26) tới
 * clamp max(50, BOARD_MIC_VAD_RMS_MIN); mặc định 50 nằm DƯỚI nền → VAD coi ồn
 * nền là giọng, không bao giờ thấy im lặng để kết thúc lượt. Sàn 120 = trên nền
 * đo được, vẫn dưới 160 để profile giọng nhỏ/xa còn tác dụng. genu-v6 đặt 420
 * vì nền của bo đó cao hơn — KHÔNG chép số đó sang đây.
 * BOARD_WAKE_GAIN_PCT / _DET_THRESHOLD / _DETECTION_AGGRESSIVE giữ mặc định
 * (100 / 0.0 / 0): cần test giọng thật ("Hi Lily" ở 1m, 3m) mới chỉnh được. */
#define BOARD_MIC_VAD_RMS_MIN        120

/* ===== Tận dụng phần cứng mic (12/09/2026) — xem README-P4 §4.7 =====
 * ES7210 có 4 kênh ADC: MIC1/MIC2 = 2 mic MEMS (đã kiểm: cả hai sống, log "TDM ch
 * rms"), MIC3 = AEC reference CỨNG từ DAC_OUT ES8311, MIC4 trống. Đã thử lấy MIC3
 * qua TDM của driver esp_codec_dev (reg12=0x02, word 16 lẫn 32 bit): ES7210 KHÔNG
 * ghép 4 kênh vào khung I2S 64 BCLK — chỉ ra 2 mic MSB-aligned trong 2 slot 32-bit
 * (nửa thấp = 0 hoặc nhiễu LSB). Muốn ref cứng phải chuyển CẢ ES8311 + ES7210 + I2S
 * sang khung PCM/DSP bằng ghi thanh ghi thô → để sau (README-P4 §4.7).
 * Hiện dùng: 2 mic 16-bit + AFE "MMR" với ref MỀM (PCM loa đẩy vào ring). */
#define BOARD_AUDIO_ES7210_TDM4      0
#define BOARD_AUDIO_CODEC_STEREO_SLOTS 0
/* ESP-SR AFE thay đường mic_task/WakeNet thô: AEC (ref mềm) + BSS 2 mic + NS +
 * VAD WebRTC + WakeNet + AGC. S3 tắt vì thiếu CPU; P4 chạy được ở LOW_COST
 * (HIGH_PERF: fetch chỉ 220/312 khung/10 s + task_wdt IDLE1). AEC chỉ bật khi loa
 * đang phát (audio_pipeline.c) để core 1 còn thở. */
#define BOARD_AUDIO_AFE_RUNTIME      1
/* "M": tổng bão hoà 2 mic (+3 dB SNR) vào 1 kênh → NS + VAD + WakeNet + AGC.
 * "MMR" (BSS 2 mic + AEC ref mềm) đã thử 12/09: lib esp32p4_less_v3 không kịp —
 * AEC bật là "Ringbuffer FEED full", fetch 195/312 khung/10 s. */
#define BOARD_AUDIO_AFE_FORMAT       "M"
#define BOARD_AUDIO_AFE_HIGH_PERF    0
/* AGC: có WakeNet thì lib tự ép agc_mode=WAKENET (gain do model WakeNet tính) —
 * BẬT, vì giọng ở 50 cm chỉ tới −17 dBFS. (Ghi chú cũ "AGC đẩy sát full scale/clip"
 * là ảo giác do tool dựng WAV lệch 1 byte — README-P4 §4.6.)
 * VAD WebRTC mode 0 (nhạy nhất) báo "speech" gần liên tục trong phòng có người nói
 * chuyện → phiên nghe không kết thúc; mode 3 đỡ hơn nhưng vẫn theo môi trường. */
#define BOARD_AUDIO_AFE_AGC          1
#define BOARD_AUDIO_AFE_VAD_MODE     3
/* afe_linear_gain: 1.0 — mức được AGC WakeNet lo; gain analog ES7210 đã +5 dB bên
 * dưới. (Bản ghi "nhiễu trắng full-scale" khi gain 3.0 là ảo giác của tool dựng WAV,
 * không phải tràn số — đã kiểm lại 12/09, gain 3.0 cho đỉnh −7 dBFS sạch.) */
#define BOARD_AUDIO_AFE_LINEAR_GAIN  1.0f
/* Cộng thêm vào gain PGA ES7210 mà server gửi (80 % → 28 dB): +5 dB → 33 dB. 37,5 dB
 * (max) từng clip phụ âm trên S3 → không lên tối đa. */
#define BOARD_MIC_GAIN_DB_OFFSET     5.0f
/* Uplink ASR: 0 = gửi OUTPUT AFE (đã AEC/NS/AGC, 16k→24k); 1 = mic thô như S3.
 * S3 chọn 1 vì AFE bị đói CPU băm nát tiếng; P4 thử 0 trước. */
#define BOARD_AUDIO_ASR_UPLINK_RAW   0
/* Task AFE (WakeNet/VAD/AGC) sang core 1 prio 6: log 13/09 17:00 cho thấy ở core 0 nó
 * tranh CPU với LVGL/touch/WS → fetch 276–308/313 lúc loa phát, UI chờ lock ~100 ms.
 * Core 1: afe_feed 6, spk_dec 7, afe_fetch 4, img_worker 3 (README-P4 §9.3). */
#define BOARD_AUDIO_AFE_CORE         1
#define BOARD_AUDIO_AFE_PRIO         6
#define BOARD_AUDIO_FEED_TASK_PRIO   7     /* RX DMA 60 ms không lề: feed phải chạy ngay khi có mẫu */
#define BOARD_AUDIO_DIAG_TDM_RMS     0     /* đã xác minh thứ tự kênh (§4.7); bật khi cần */
#define BOARD_SPK_BITS_PER_SAMPLE    16
#define BOARD_SPK_CHANNELS           1
#define BOARD_SPK_VOLUME_DEFAULT     80

/* Alias tương thích đường audio wrapper cũ (giống board S3 2.8"). */
#define BOARD_MIC_I2S_NUM            BOARD_AUDIO_I2S_NUM
#define BOARD_MIC_I2S_BCLK           BOARD_AUDIO_I2S_BCLK
#define BOARD_MIC_I2S_WS             BOARD_AUDIO_I2S_WS
#define BOARD_MIC_I2S_DIN            BOARD_AUDIO_I2S_DIN
#define BOARD_MIC_BITS_PER_SAMPLE    32
#define BOARD_MIC_CHANNELS           1
#define BOARD_SPK_I2S_NUM            BOARD_AUDIO_I2S_NUM
#define BOARD_SPK_I2S_BCLK           BOARD_AUDIO_I2S_BCLK
#define BOARD_SPK_I2S_WS             BOARD_AUDIO_I2S_WS
#define BOARD_SPK_I2S_DOUT           BOARD_AUDIO_I2S_DOUT

/* ========================== Thẻ nhớ (docs/HARDWARE-PINOUT.md §7) ==========================
 *
 * Bộ chân TRÙNG KHỚP TUYỆT ĐỐI với SDMMC_SLOT0_IOMUX_PIN_NUM_* của ESP32-P4
 * ⇒ chạy slot 0 qua IOMUX, tốc độ cao nhất.
 *
 * Không xung đột với ESP-Hosted: C5 đi SDIO **slot 1** (CONFIG_ESP_HOSTED_SDIO_SLOT=1,
 * chân 18/19/14-17). Kconfig của esp_hosted còn ghi thẳng "Slot 0 connects to the
 * MicroSD Card slot". Ghi chú cũ trong README-P4 §7 nói hai thứ dùng chung SDIO là SAI.
 *
 * VDD của khe thẻ do LDO NỘI kênh 4 của P4 cấp (module chân 66 = `ESP_LDO_VO4`)
 * ⇒ không bật LDO thì thẻ không có nguồn, `send_op_cond` timeout 0x107. */
#define BOARD_SD_USE_SDMMC           1
#define BOARD_SD_MMC_SLOT            0
#define BOARD_SD_MMC_WIDTH           4
#define BOARD_SD_MMC_CLK             43
#define BOARD_SD_MMC_CMD             44
#define BOARD_SD_MMC_D0              39
#define BOARD_SD_MMC_D1              40
#define BOARD_SD_MMC_D2              41
#define BOARD_SD_MMC_D3              42
#define BOARD_SD_PWR_LDO_CHAN        4
/* Xung SDMMC lúc làm việc (kHz). 15/09/2026: thẻ SDHC 4 GB mount + R/W OK ở 400 kHz
 * (probing) trên slot 0 IOMUX 4-bit → nâng 20 MHz (= SDMMC_FREQ_DEFAULT, chuẩn
 * default-speed, không cần high-speed switch). Mount lần 1 dùng số này; fail thì
 * course_media_cache.c tự hạ về 400 kHz rồi 1-bit. 40 MHz (HIGHSPEED) chưa thử —
 * pull-up trên board 51K + nội 45K, đo lại trước khi nâng. */
#define BOARD_SD_MMC_FREQ_KHZ        20000

/* ========================== WiFi (ESP32-C5 qua ESP-Hosted) ========================== */
/* C5 hai bang; 15/09/2026 STA vao "FBT" ch=36 (5 GHz) associate duoc nhung DHCP khong
 * xong 3/3 lan, mang 2,4 GHz thi Got IP. Khoa 2,4 GHz cho giong S3 (README-P4 §5). */
#define BOARD_WIFI_BAND_2G_ONLY      1
/* Khe thẻ KHÔNG có chân card-detect (J4 chân 9 để trống) ⇒ chỉ biết có thẻ hay
 * không bằng cách thử mount. */
#define BOARD_SD_HAS_CARD_DETECT     0

/* ========================== Buttons ========================== */
/* GPIO35 = BTN2 (BOOT). Dùng chung net với `DTRTNOW` của CH343P — mạch auto-reset
 * chỉ tạo xung nên terminal mở/đóng DTR không sinh nút giả (đã kiểm: không lần
 * nào thấy "BTN tap"/"BTN long" trong log qua nhiều lần toggle DTR). */
#define BOARD_BTN_BOOT_GPIO          35
/* GPIO0 = BTN3 (R60 10K lên VCC3V3, D20 ESD, nhấn = LOW) — Rapid4P dùng làm nút
 * ĐO / XÁC NHẬN vật lý (tay ướt, găng tay không chạm màn được). input/button.c
 * quét cùng task với BOOT. */
#define BOARD_BTN_MEASURE_GPIO       0

/* ========================== LED ========================== */
/* GPIO34 = DIN của một LED WS2812B. KHÔNG khai ở đây: WS2812B cần driver RMT
 * hoặc SPI, không phải GPIO mức thường, mà firmware chưa có driver đó. Khai số
 * chân vào BOARD_LED_GPIO sẽ khiến người đọc sau tưởng nó bật/tắt được. */
#define BOARD_LED_GPIO               -1
#define BOARD_LED_COUNT              0
#define BOARD_WS2812_GPIO            34   /* tư liệu — chưa có driver */

/* ========================== Power (AXP2101) ========================== */
/* Board không có chân giữ nguồn cũng không có chân đo pin: TOÀN BỘ thông tin
 * pin/sạc phải hỏi PMIC AXP2101 qua I2C (chung bus với codec/touch), ngắt ở
 * GPIO21 active-LOW. Firmware chưa có driver AXP2101. */
#define BOARD_POWER_HOLD_GPIO        -1
#define BOARD_CHARGING_DET_GPIO      -1
#define BOARD_PMIC_I2C_ADDR          0x34   /* tư liệu — chưa có driver */
#define BOARD_PMIC_IRQ_GPIO          21     /* tư liệu — chưa có driver */

/* ========================== Bo cảm biến quang N slot (JP1) ==========================
 *
 * 2026-09-18: bo LED + bo cảm biến ĐÃ CÓ (ReaderPlus/ReaderMax 4 khe) — nhưng chúng cần bo giao tiếp
 * Rapid4P-IF (nguồn LED, công tắc khe, pull-up I2C) cắm vào JP1: docs/HARDWARE-ARCHITECTURE.md.
 * Chốt D2 (driver LED) rồi mới sửa ON_LEVEL / PWM_* dưới đây theo bảng §5 của tài liệu đó.
 * CHƯA CÓ SCHEMATIC bo IF (MAPPING-Rapid4P.md §5 mục 1). Số chân dưới đây là ĐỀ XUẤT
 * 2026-09-17, chọn trên header JP1 (2×10, mỗi GPIO có 100 R nối tiếp — HARDWARE-PINOUT
 * §11.5) để không đụng bus I2C chung GPIO7/8 (7 thiết bị, touch quét 10 ms):
 *   JP1 chân 12 = GPIO26 → SDA bus riêng I2C_NUM_1
 *   JP1 chân 13 = GPIO27 → SCL bus riêng I2C_NUM_1
 *   JP1 chân 14/15/16/11 = GPIO28/29/30/45 → enable LED chiếu slot 1..4; GPIO47 → slot 5 (ĐỀ XUẤT)
 *   JP1 chân 9  = GPIO46 → PWM độ sáng LED chung (LEDC)
 *   JP1 chân 4  = VCC3V3, chân 3/10 = GND
 * Còn dư GPIO48/49/50 trên JP1. Khi có schematic thật: sửa Ở ĐÂY, không sửa driver.
 *
 * Bo con kế thừa từ FBT-ReaderPlus-1.0: mux TCA9548A 0x70 (A0–A2 = GND), 4 kênh 0..3
 * mỗi kênh một TCS34725 (địa chỉ cố định 0x29, ID 0x44/0x4D). LED chiếu: enable riêng
 * từng slot + PWM chung (ReaderPlus: LEDC 5 kHz 8-bit, mặc định 127/255). */
#define BOARD_SENSOR_I2C_NUM         I2C_NUM_1
#define BOARD_SENSOR_I2C_SDA         26
#define BOARD_SENSOR_I2C_SCL         27
/* 100 kHz: 100 R nối tiếp + dây tới bo con + 4 nhánh sau mux; pull-up nội của P4
 * (~45 K) là quá yếu — bo con PHẢI có pull-up 4,7 K. */
#define BOARD_SENSOR_I2C_FREQ_HZ     100000
#define BOARD_SENSOR_MUX_ADDR        0x70
#define BOARD_SENSOR_TCS_ADDR        0x29
/* 5 KHE (kế hoạch docs/plan/rapid4p-5-slot.md, 2026-09-17): khe 5 = mux kênh 4, LED enable
 * GPIO47 (JP1 còn 48/49/50). Đây là NGUỒN SỰ THẬT về số khe: R4P_SLOTS, bố cục LCD, payload,
 * dashboard đều suy ra từ đây — đổi số khe chỉ sửa 3 dòng này + bo mạch. */
#define BOARD_SENSOR_SLOTS           5
#define BOARD_SENSOR_MUX_CHANNELS    { 0, 1, 2, 3, 4 }
#define BOARD_SLOT_LED_GPIOS         { 28, 29, 30, 45, 47 }
#define BOARD_SLOT_LED_ON_LEVEL      1
#define BOARD_SLOT_LED_PWM_GPIO      46
#define BOARD_SLOT_LED_PWM_FREQ_HZ   5000
#define BOARD_SLOT_LED_PWM_DEFAULT   127     /* /255, như ReaderPlus PWM_LED[] */
/* LEDC: kênh 0 + timer 0 đã dành cho đèn nền LCD (display.c). */
#define BOARD_SLOT_LED_LEDC_CHANNEL  LEDC_CHANNEL_1
#define BOARD_SLOT_LED_LEDC_TIMER    LEDC_TIMER_1

/* ========================== Board metadata ========================== */
#define BOARD_NAME                   "rapid4p_p4c5_st7102_lcd43"
#define BOARD_REV                    "p4-bringup-v1"
/* Khoá kho OTA trên Engineer Server (system/products.yaml) + PCB. Board S3 2.8" là khoá riêng
 * `rapid4p-s3` (variant_of rapid4p) để hai kho ảnh không lẫn nhau. */
#define BOARD_PRODUCT_KEY            "rapid4p"
#define BOARD_HW_VERSION             "P4C5-43"

#ifdef __cplusplus
}
#endif
