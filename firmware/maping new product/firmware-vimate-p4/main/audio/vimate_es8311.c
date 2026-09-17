/**
 * vimate_es8311.c — ES8311 codec init theo PATTERN của xiaozhi
 * (firmware/main/audio/codecs/es8311_audio_codec.cc).
 *
 * Key insight (sau khi debug nhiều giờ): KHÔNG được gọi codec_if->open/set_vol
 * trực tiếp. Phải qua esp_codec_dev wrapper:
 *   1. CreateDuplexChannels — i2s_new_channel với tx_handle+rx_handle (full-duplex)
 *      slot mode STEREO + SLOT_BOTH + mclk_multiple 256 + bit_shift true
 *   2. audio_codec_new_i2s_data — data_if wraps I2S handles
 *   3. audio_codec_new_i2c_ctrl — ctrl_if wraps I2C bus
 *   4. es8311_codec_new — codec_if
 *   5. esp_codec_dev_new(codec_if + data_if) — dev wrapper
 *   6. esp_codec_dev_open(dev, sample_info) — actually open + config
 *   7. esp_codec_dev_set_out_vol/set_in_gain
 *   8. PA pin HIGH bằng tay (xiaozhi tự handle qua hw_gain config — phụ thuộc board)
 *
 * Sau đó dùng esp_codec_dev_write/read để play/capture audio. Wrapper auto
 * route data qua I2S → ES8311 ADC/DAC → loa/mic.
 *
 * Expose TX/RX handle qua get_*() cho legacy code (sẽ migrate dần).
 */
#include "vimate_es8311.h"
#include "vimate.h"
#include "boards/board.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es7210_adc.h"
#include "es8311_codec.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

#if BOARD_AUDIO_USE_ES8311

static i2c_master_bus_handle_t s_i2c_bus = NULL;

/* Codec dev wrapper handle — write/read qua đây. */
static esp_codec_dev_handle_t s_dev = NULL;
static esp_codec_dev_handle_t s_adc_dev __attribute__((unused)) = NULL;

/* I2S channel handles — own bởi data_if sau khi tạo. */
static i2s_chan_handle_t s_tx = NULL;
static i2s_chan_handle_t s_rx = NULL;

/* Codec interface handles (giữ tham chiếu để khỏi bị GC). */
static const audio_codec_data_if_t *s_data_if = NULL;
static const audio_codec_ctrl_if_t *s_ctrl_if = NULL;
static const audio_codec_ctrl_if_t *s_adc_ctrl_if __attribute__((unused)) = NULL;
static const audio_codec_if_t      *s_codec_if = NULL;
static const audio_codec_if_t      *s_adc_codec_if __attribute__((unused)) = NULL;
static const audio_codec_gpio_if_t *s_gpio_if = NULL;

static int s_volume = BOARD_SPK_VOLUME_DEFAULT;
static int s_mic_gain_percent = 80;
static bool s_output_enabled = false;
static bool s_input_enabled = false;
static bool s_pa_enabled = false;
static bool s_fade_next_write = true;
static int16_t *s_write_stereo = NULL;
static size_t s_write_stereo_cap = 0;
static int16_t *s_read_stereo __attribute__((unused)) = NULL;
static size_t s_read_stereo_cap __attribute__((unused)) = 0;

#include <math.h>
#ifndef BOARD_AUDIO_CODEC_STEREO_SLOTS
#define BOARD_AUDIO_CODEC_STEREO_SLOTS 0
#endif
#ifndef BOARD_AUDIO_ES7210_TDM4
#define BOARD_AUDIO_ES7210_TDM4 0
#endif
#if BOARD_AUDIO_ES7210_TDM4
#define CODEC_IN_CH 4
#ifndef BOARD_AUDIO_TDM_MIC_A
#define BOARD_AUDIO_TDM_MIC_A 1
#endif
#ifndef BOARD_AUDIO_TDM_MIC_B
#define BOARD_AUDIO_TDM_MIC_B 0
#endif
#ifndef BOARD_AUDIO_TDM_REF
#define BOARD_AUDIO_TDM_REF 3
#endif
#else
#define CODEC_IN_CH 2
#define BOARD_AUDIO_TDM_MIC_A 0
#define BOARD_AUDIO_TDM_MIC_B 1
#define BOARD_AUDIO_TDM_REF   -1
#endif
#ifndef BOARD_AUDIO_PA_TCA9555_EXIO
#define BOARD_AUDIO_PA_TCA9555_EXIO -1
#endif
#ifndef BOARD_AUDIO_TCA9555_I2C_ADDR
#define BOARD_AUDIO_TCA9555_I2C_ADDR 0
#endif
#ifndef BOARD_AUDIO_ADC_I2C_ADDR
#define BOARD_AUDIO_ADC_I2C_ADDR 0
#endif
#ifndef BOARD_AUDIO_USE_ES7210_ADC
#define BOARD_AUDIO_USE_ES7210_ADC 0
#endif

static inline int pa_enabled_level(void) {
    return BOARD_AUDIO_PA_ON_LEVEL ? 1 : 0;
}

static inline int pa_disabled_level(void) {
    return BOARD_AUDIO_PA_ON_LEVEL ? 0 : 1;
}

static void pa_set_enabled(bool enabled, const char *stage) {
#if BOARD_AUDIO_TCA9555_I2C_ADDR && (BOARD_AUDIO_PA_TCA9555_EXIO >= 0)
    if (s_i2c_bus) {
        i2c_master_dev_handle_t dev = NULL;
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = BOARD_AUDIO_TCA9555_I2C_ADDR,
            .scl_speed_hz = 100000,
        };
        esp_err_t err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &dev);
        if (err == ESP_OK && dev) {
            uint8_t reg = (BOARD_AUDIO_PA_TCA9555_EXIO >= 8) ? 0x03 : 0x02;
            uint8_t bit = (uint8_t)(BOARD_AUDIO_PA_TCA9555_EXIO % 8);
            uint8_t val = 0xff;
            err = i2c_master_transmit_receive(dev, &reg, 1, &val, 1, pdMS_TO_TICKS(100));
            if (err == ESP_OK) {
                if (enabled == (BOARD_AUDIO_PA_ON_LEVEL != 0)) {
                    val |= (uint8_t)(1U << bit);
                } else {
                    val &= (uint8_t)~(1U << bit);
                }
                uint8_t data[] = {reg, val};
                err = i2c_master_transmit(dev, data, sizeof(data), pdMS_TO_TICKS(100));
            }
            ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_bus_rm_device(dev));
            if (err == ESP_OK) {
                s_pa_enabled = enabled;
            }
            ESP_LOGI(TAG_AUDIO, "PA %s TCA9555 EXIO%d=%s err=%s",
                     stage ? stage : "-", BOARD_AUDIO_PA_TCA9555_EXIO,
                     enabled ? "on" : "off", esp_err_to_name(err));
            return;
        }
    }
#endif
    if (BOARD_AUDIO_PA_PIN < 0) {
        return;
    }

    int level = enabled ? pa_enabled_level() : pa_disabled_level();
    esp_err_t err = gpio_set_level(BOARD_AUDIO_PA_PIN, level);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_AUDIO, "PA %s set=%d err=%s",
                 stage ? stage : "-", level, esp_err_to_name(err));
    } else {
        s_pa_enabled = enabled;
    }
}

/* ES8311/PA can make an audible pop if the amplifier is enabled while the DAC
 * output is not settled, or if the first PCM sample has a hard edge. Keep the
 * anti-pop treatment local to the codec wrapper so every caller benefits.
 *
 * esp_codec_dev maps channel=1 to a two-slot I2S config with channel_mask=left.
 * Pass one mono sample per audio sample; duplicating L/R here stretches playback.
 */
static int codec_write_raw_pcm16(const int16_t *pcm, size_t samples, bool fade_in) {
    if (!s_dev || !pcm || samples == 0) return 0;
    const int16_t *write_pcm = pcm;
    size_t write_samples = samples;
    /* STEREO_SLOTS: DAC mở channel=2 (để chung BCLK 64 bit/frame với RX TDM 4×16)
     * → phải nhân đôi mono ra L/R, nếu không loa phát chậm nửa tốc. */
    bool needs_copy = fade_in || BOARD_AUDIO_CODEC_STEREO_SLOTS;
    if (needs_copy) {
        size_t need = BOARD_AUDIO_CODEC_STEREO_SLOTS ? samples * 2 : samples;
        if (need > s_write_stereo_cap) {
            int16_t *next = heap_caps_realloc(
                s_write_stereo, need * sizeof(int16_t),
                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!next) {
                ESP_LOGW(TAG_AUDIO, "No internal RAM for speaker buffer (%u samples)",
                         (unsigned)need);
                return 0;
            }
            s_write_stereo = next;
            s_write_stereo_cap = need;
        }
        size_t j = 0;
        const size_t ramp_samples = BOARD_SPK_SAMPLE_RATE / 100; /* 10ms */
        for (size_t i = 0; i < samples; i++) {
            int32_t v = pcm[i];
            if (fade_in && i < ramp_samples) {
                v = (v * (int32_t)i) / (int32_t)ramp_samples;
            }
            s_write_stereo[j++] = (int16_t)v;
#if BOARD_AUDIO_CODEC_STEREO_SLOTS
            s_write_stereo[j++] = (int16_t)v;
#endif
        }
        write_pcm = s_write_stereo;
        write_samples = need;
    }
    int r = esp_codec_dev_write(s_dev, (void *)write_pcm,
                                write_samples * sizeof(int16_t));
    if (r != ESP_OK) {
        ESP_LOGW(TAG_AUDIO, "codec_dev_write ret=%d", r);
        return 0;
    }
    return (int)samples;
}

static void codec_write_silence_ms(int ms) {
    if (!s_dev || ms <= 0) return;
    int remaining = (BOARD_SPK_SAMPLE_RATE * ms) / 1000;
    int16_t zeros[240] = {0};
    while (remaining > 0) {
        size_t chunk = remaining > (int)(sizeof(zeros) / sizeof(zeros[0]))
            ? sizeof(zeros) / sizeof(zeros[0])
            : (size_t)remaining;
        if (codec_write_raw_pcm16(zeros, chunk, false) <= 0) break;
        remaining -= (int)chunk;
    }
}

static void p4_log_i2c_devices(const char *stage) {
    if (!s_i2c_bus) return;

    esp_err_t codec_probe = i2c_master_probe(
        s_i2c_bus, BOARD_AUDIO_CODEC_I2C_ADDR, 30);
    esp_err_t adc_probe = ESP_ERR_NOT_FOUND;
#if BOARD_AUDIO_USE_ES7210_ADC
    adc_probe = i2c_master_probe(s_i2c_bus, BOARD_AUDIO_ADC_I2C_ADDR, 30);
#endif
    esp_err_t expander_probe = ESP_ERR_NOT_FOUND;
#if BOARD_AUDIO_TCA9555_I2C_ADDR
    expander_probe = i2c_master_probe(s_i2c_bus, BOARD_AUDIO_TCA9555_I2C_ADDR, 30);
#endif
    ESP_LOGI(TAG_AUDIO, "I2C probe %s: ES8311 0x%02x=%s, ES7210 0x%02x=%s, TCA9555 0x%02x=%s",
             stage ? stage : "-", BOARD_AUDIO_CODEC_I2C_ADDR, esp_err_to_name(codec_probe),
             BOARD_AUDIO_ADC_I2C_ADDR, esp_err_to_name(adc_probe),
             BOARD_AUDIO_TCA9555_I2C_ADDR, esp_err_to_name(expander_probe));
}

esp_codec_dev_handle_t es8311_get_dev(void) { return s_dev; }
i2s_chan_handle_t es8311_get_tx(void) { return s_tx; }
i2s_chan_handle_t es8311_get_rx(void) { return s_rx; }

/* CreateDuplexChannels — copy chính xác từ xiaozhi es8311_audio_codec.cc.
 * STEREO + SLOT_BOTH + bit_shift + mclk_256 là requirement của ES8311 codec. */
static esp_err_t create_duplex_channels(void) {
    i2s_chan_config_t chan_cfg = {
        .id = BOARD_AUDIO_I2S_NUM,
        .role = I2S_ROLE_MASTER,
        /* GIỮ 6 desc: RAM nội cực hẹp (rxbuf đã hạ 16→8 nhường AFE, free ~vài KB).
         * Thử nâng 6→12 (+5.6KB DMA) → thiết bị OOM/crash mỗi ~2.5p khi stream mic
         * (close 1006) → ĐÃ REVERT. Chống rớt audio phải làm RAM-an-toàn (tách
         * capture↔encode/send bằng ring + giảm TLS block), KHÔNG nâng buffer DMA. */
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx, &s_rx));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)BOARD_SPK_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,    /* ES8311 cần STEREO slot config */
            .slot_mask = I2S_STD_SLOT_BOTH,       /* L+R slot — write data sẽ duplicate */
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,                     /* Philips alignment */
#ifdef I2S_HW_VERSION_2
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
#endif
        },
        .gpio_cfg = {
            .mclk = BOARD_AUDIO_I2S_MCLK,
            .bclk = BOARD_AUDIO_I2S_BCLK,
            .ws   = BOARD_AUDIO_I2S_WS,
            .dout = BOARD_AUDIO_I2S_DOUT,
            .din  = BOARD_AUDIO_I2S_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx));
    ESP_LOGI(TAG_AUDIO, "ES8311 duplex channels created %dHz 16-bit stereo slots",
             BOARD_SPK_SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t es8311_codec_init(void) {
    /* I2C bus — reuse hoặc tạo mới (touch có thể đã tạo). */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BOARD_AUDIO_CODEC_I2C_NUM,
        .sda_io_num = BOARD_AUDIO_CODEC_I2C_SDA,
        .scl_io_num = BOARD_AUDIO_CODEC_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t r = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (r == ESP_ERR_INVALID_STATE) {
        r = i2c_master_get_bus_handle(BOARD_AUDIO_CODEC_I2C_NUM, &s_i2c_bus);
        if (r == ESP_OK && s_i2c_bus != NULL) {
            ESP_LOGI(TAG_AUDIO, "I2C bus reused");
        }
    }
    if (r != ESP_OK || s_i2c_bus == NULL) {
        ESP_LOGE(TAG_AUDIO, "I2C bus init fail: %s", esp_err_to_name(r));
        return r;
    }
    p4_log_i2c_devices("before-codec-open");

    /* PA pin output mode.
     * QUAN TRỌNG: board ES3N28P (xiaozhi reference: freenove-esp32s3-display-2.8-lcd)
     * dùng PA INVERTED (active LOW). GPIO1 = LOW → enable amp, HIGH → disable.
     * Mình set HIGH lúc trước = amp tắt = loa im lặng dù codec output PCM đúng. */
    volatile int pa_pin_value = BOARD_AUDIO_PA_PIN;
    gpio_num_t pa_pin = (gpio_num_t)pa_pin_value;
    if (pa_pin >= 0) {
        gpio_config_t pa_cfg = {
            .pin_bit_mask = (1ULL << pa_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&pa_cfg);
        gpio_set_drive_capability(pa_pin, GPIO_DRIVE_CAP_3);
        pa_set_enabled(false, "init-off");
    }

    /* Step 1: I2S full-duplex channels */
    ESP_ERROR_CHECK(create_duplex_channels());

    /* Step 2: data_if wrap I2S handles */
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = BOARD_AUDIO_I2S_NUM,
        .rx_handle = s_rx,
        .tx_handle = s_tx,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!s_data_if) {
        ESP_LOGE(TAG_AUDIO, "data_if create fail");
        return ESP_FAIL;
    }

    /* Step 3: ctrl_if wrap I2C bus. ES8311 is speaker DAC; ES7210 is mic ADC
     * on Waveshare ESP32-S3-AUDIO-Board. Do not read mic from ES8311 here. */
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BOARD_AUDIO_CODEC_I2C_NUM,
        .addr = BOARD_AUDIO_CODEC_I2C_ADDR << 1,
        .bus_handle = s_i2c_bus,
    };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!s_ctrl_if) {
        ESP_LOGE(TAG_AUDIO, "ctrl_if create fail");
        return ESP_FAIL;
    }
#if BOARD_AUDIO_USE_ES7210_ADC
    i2c_cfg.addr = BOARD_AUDIO_ADC_I2C_ADDR << 1;
    s_adc_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!s_adc_ctrl_if) {
        ESP_LOGE(TAG_AUDIO, "adc ctrl_if create fail");
        return ESP_FAIL;
    }
#endif

    /* Step 4: gpio_if (codec lib dùng để control PA tự động nếu pa_pin set) */
    s_gpio_if = audio_codec_new_gpio();

    /* Step 5: ES8311 codec_if */
    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = s_ctrl_if,
        .gpio_if = s_gpio_if,
        .codec_mode = BOARD_AUDIO_USE_ES7210_ADC
            ? ESP_CODEC_DEV_WORK_MODE_DAC
            : ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = BOARD_AUDIO_PA_PIN,
        .use_mclk = true,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
        .pa_reverted = (BOARD_AUDIO_PA_ON_LEVEL == 0),
        .master_mode = false,
    };
    s_codec_if = es8311_codec_new(&es_cfg);
    if (!s_codec_if) {
        ESP_LOGE(TAG_AUDIO, "codec_if create fail");
        return ESP_FAIL;
    }
#if BOARD_AUDIO_USE_ES7210_ADC
    es7210_codec_cfg_t adc_cfg = {
        .ctrl_if = s_adc_ctrl_if,
        .master_mode = false,
#if BOARD_AUDIO_ES7210_TDM4
        /* ≥3 mic → driver bật TDM (reg12=0x02): 4 kênh trên SDOUT1, MIC3 = AEC ref cứng. */
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
#else
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2,
#endif
        .mclk_src = ES7210_MCLK_FROM_PAD,
        .mclk_div = 256,
    };
    s_adc_codec_if = es7210_codec_new(&adc_cfg);
    if (!s_adc_codec_if) {
        ESP_LOGE(TAG_AUDIO, "ES7210 codec_if create fail");
        return ESP_FAIL;
    }
#endif

    /* Step 6: dev wrappers */
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = BOARD_AUDIO_USE_ES7210_ADC
            ? ESP_CODEC_DEV_TYPE_OUT
            : ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = s_codec_if,
        .data_if = s_data_if,
    };
    s_dev = esp_codec_dev_new(&dev_cfg);
    if (!s_dev) {
        ESP_LOGE(TAG_AUDIO, "esp_codec_dev_new fail");
        return ESP_FAIL;
    }
#if BOARD_AUDIO_USE_ES7210_ADC
    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    dev_cfg.codec_if = s_adc_codec_if;
    s_adc_dev = esp_codec_dev_new(&dev_cfg);
    if (!s_adc_dev) {
        ESP_LOGE(TAG_AUDIO, "esp_codec_dev_new adc fail");
        return ESP_FAIL;
    }
#endif

    /* Step 7: open dev với sample info — đây mới là chỗ codec actually
     * configured at sample rate. */
    esp_codec_dev_sample_info_t out_fs = {
        .bits_per_sample = 16,
        /* 2 slot khi STEREO_SLOTS: esp_codec_dev "extend bits" — RX 4ch×16 = 64
         * bit/frame chia cho out channel → 32-bit slot hợp lệ (channel=1 ra slot 64
         * bit → driver I2S từ chối → esp_codec_dev tự *(int*)0=0 crash). */
        .channel = BOARD_AUDIO_CODEC_STEREO_SLOTS ? 2 : 1,
        .channel_mask = 0,
        .sample_rate = (uint32_t)BOARD_SPK_SAMPLE_RATE,
        .mclk_multiple = 0,
    };
    r = esp_codec_dev_open(s_dev, &out_fs);
    if (r != ESP_OK) {
        ESP_LOGE(TAG_AUDIO, "esp_codec_dev_open fail: %s", esp_err_to_name(r));
        p4_log_i2c_devices("after-codec-open-fail");
        return r;
    }
#if BOARD_AUDIO_USE_ES7210_ADC
    esp_codec_dev_sample_info_t in_fs = {
#if BOARD_AUDIO_ES7210_TDM4
        /* TDM: mở RX như 2 kênh × 32-bit — đúng đường "Use 2 channel to fetch TDM
         * data" trong es7210.c (driver tự hạ ES7210 về 16-bit, mỗi slot I2S 32-bit
         * chứa 2 mic). KHÔNG mở channel=4: esp_codec_dev 1.4 tự điền channel_mask=0xF
         * rồi đưa thẳng vào slot_mask STD (chỉ hợp lệ 1/2/3) → LL P4 rơi vào default,
         * không slot RX nào bật → i2s_channel_read timeout mãi (dính 12/09/2026). */
        .bits_per_sample = 32,
        .channel = 2,
#else
        .bits_per_sample = 16,
        .channel = CODEC_IN_CH,
#endif
        .channel_mask = 0,
        .sample_rate = (uint32_t)BOARD_MIC_SAMPLE_RATE,
        .mclk_multiple = 0,
    };
    r = esp_codec_dev_open(s_adc_dev, &in_fs);
    if (r != ESP_OK) {
        ESP_LOGE(TAG_AUDIO, "esp_codec_dev_open ES7210 fail: %s", esp_err_to_name(r));
        p4_log_i2c_devices("after-adc-open-fail");
        return r;
    }
#endif
    /* Set out vol + in gain. ES7210 gain max practical is ~37.5dB. */
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(s_dev, s_volume));
    es8311_codec_set_mic_gain(s_mic_gain_percent);
#if BOARD_AUDIO_ES7210_TDM4 && defined(BOARD_AUDIO_ES7210_TDM_WORDLEN)
    /* THÍ NGHIỆM TDM (12/09/2026): driver esp_codec_dev đặt ES7210 word length 16 bit
     * + TDM (reg12=0x02) nhưng host chỉ thấy 2 mic MSB-aligned trong 2 slot 32-bit
     * (nửa thấp = 0). Giả thuyết (theo cách ESP-ADF cấu hình Korvo: bits=32): trong
     * TDM, ES7210 ghép 2 kênh 16-bit vào MỖI word 32-bit. Ghi thẳng reg11[7:5]
     * word length qua I2C rồi xem log "TDM ch rms": ch0/ch2 khác 0 là đúng. */
    {
        i2c_master_dev_handle_t adc_raw = NULL;
        i2c_device_config_t adc_dcfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = BOARD_AUDIO_ADC_I2C_ADDR,
            .scl_speed_hz = 100000,
        };
        if (s_i2c_bus && i2c_master_bus_add_device(s_i2c_bus, &adc_dcfg, &adc_raw) == ESP_OK) {
            uint8_t reg = 0x11, v11 = 0, v12 = 0;
            i2c_master_transmit_receive(adc_raw, &reg, 1, &v11, 1, pdMS_TO_TICKS(50));
            reg = 0x12;
            i2c_master_transmit_receive(adc_raw, &reg, 1, &v12, 1, pdMS_TO_TICKS(50));
            uint8_t w[2] = { 0x11, (uint8_t)((v11 & 0x1f) | BOARD_AUDIO_ES7210_TDM_WORDLEN) };
            esp_err_t we = i2c_master_transmit(adc_raw, w, 2, pdMS_TO_TICKS(50));
            ESP_LOGW(TAG_AUDIO, "ES7210 TDM thi nghiem: reg11 %02x -> %02x (%s), reg12=%02x",
                     v11, w[1], esp_err_to_name(we), v12);
            i2c_master_bus_rm_device(adc_raw);
        }
    }
#endif

#if defined(BOARD_LCD_USE_ST77922) && BOARD_LCD_USE_ST77922
    /* Board 3.5": demo chính chủ set ADC volume REG17=0xC8 (+4.5dB) — esp_codec_dev
     * ghi 0xBF (0dB) lúc open nên phải ghi đè SAU open. Mic board này yếu, cần
     * cả nấc analog max lẫn ADC volume demo. */
    {
        i2c_master_dev_handle_t es_raw = NULL;
        i2c_device_config_t es_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = BOARD_AUDIO_CODEC_I2C_ADDR,
            .scl_speed_hz = 100000,
        };
        if (s_i2c_bus &&
            i2c_master_bus_add_device(s_i2c_bus, &es_cfg, &es_raw) == ESP_OK) {
            uint8_t w17[2] = {0x17, 0xC8};
            if (i2c_master_transmit(es_raw, w17, 2, pdMS_TO_TICKS(100)) == ESP_OK) {
                ESP_LOGI(TAG_AUDIO, "ES8311 REG17=0xC8 (ADC vol +4.5dB — demo chính chủ)");
            }
            i2c_master_bus_rm_device(es_raw);
        }
    }
#endif

    /* Default state: codec clocked, PA muted. First playback does a short
     * silence preroll before enabling the amplifier to avoid boot pops. */
    s_output_enabled = true;
    s_fade_next_write = true;
    pa_set_enabled(false, "codec-ready-muted");

    ESP_LOGI(TAG_AUDIO, "ES8311 codec_dev ready (vol=%d%%, %dHz %s, i2c=%d, pa=%d on=%d)",
             s_volume, BOARD_SPK_SAMPLE_RATE,
#if BOARD_AUDIO_USE_ES7210_ADC
             "es8311-out mono + es7210-in stereo-slots",
#else
             "es8311-in/out mono",
#endif
             BOARD_AUDIO_CODEC_I2C_NUM, BOARD_AUDIO_PA_PIN, pa_enabled_level());

    return ESP_OK;
}

void es8311_codec_set_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_volume = percent;
    if (s_dev) {
        esp_codec_dev_set_out_vol(s_dev, percent);
    }
}

void es8311_codec_set_mic_gain(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_mic_gain_percent = percent;
#if BOARD_AUDIO_USE_ES7210_ADC
    /* HẠ gain analog (13/06): 37.5dB (max) làm ADC ES7210 CLIP đỉnh phụ âm khi
     * nói to/gần → server peak=32768 rms thấp (crest cao) → ASR méo. AFE có AGC
     * tự nâng mức lại sạch → để analog ~28dB chừa headroom cho đỉnh.
     * 0%→12dB, 80%→28dB, 100%→32dB. */
    float gain_db = 12.0f + ((float)percent * 0.20f);
#ifdef BOARD_MIC_GAIN_DB_OFFSET
    gain_db += BOARD_MIC_GAIN_DB_OFFSET;   /* board mic yếu: bù analog (P4 4.3": +5 dB) */
#endif
    if (gain_db > 33.0f) gain_db = 33.0f;
    if (s_adc_dev) {
        esp_codec_dev_set_in_gain(s_adc_dev, gain_db);
#if BOARD_AUDIO_ES7210_TDM4
        /* set_in_gain đặt CẢ 4 PGA. Kênh ref (MIC3 = DAC_OUT ~1 Vrms) và MIC4 trống
         * phải về 0 dB, không thì ref clip cứng → AEC vô dụng. Mask bit i = MIC(i+1). */
        esp_codec_dev_set_in_channel_gain(s_adc_dev,
                                          ESP_CODEC_DEV_MAKE_CHANNEL_MASK(2) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(3),
                                          0.0f);
#endif
    } else if (s_dev) {
        /* Board ES8311 duplex (KHÔNG có ES7210): ADC nằm trên CHÍNH s_dev. Trước
         * đây gain mic không bao giờ được set (chỉ set cho s_adc_dev=NULL) → ADC
         * câm → ghi âm ra im lặng/0 byte. Set gain trực tiếp lên s_dev. */
        esp_codec_dev_set_in_gain(s_dev, gain_db);
    }
    ESP_LOGI(TAG_AUDIO, "Mic gain %d%% -> %.1fdB (es7210)", percent, (double)gain_db);
#else
    /* ES8311 single-mic board (không có ES7210). 80% khớp reference
     * Waveshare/xiaozhi là 30dB; trước đây nhánh này chỉ set s_adc_dev=NULL nên
     * gain runtime không được apply vào codec đang đọc mic. */
#if defined(BOARD_LCD_USE_ST77922) && BOARD_LCD_USE_ST77922
    /* Board 3.5" (ST77922): mic thu YẾU — driver es8311 làm tròn XUỐNG nấc 6dB
     * (REG16), 40dB → nấc 36dB. Base 24 để 80% = 42dB = NẤC MAX. Đo: 36dB+×3 vẫn
     * rms~100 server (ASR rỗng); cần max analog + REG17 demo. Rè thì giảm base. */
    float gain_db = 24.0f + ((float)percent * 0.225f);
#else
    float gain_db = 12.0f + ((float)percent * 0.225f);
#endif
    if (gain_db > 42.0f) gain_db = 42.0f;
    if (s_dev) {
        esp_codec_dev_set_in_gain(s_dev, gain_db);
    }
    ESP_LOGI(TAG_AUDIO, "Mic gain %d%% -> %.1fdB (es8311)", percent, (double)gain_db);
#endif
}

void es8311_codec_mute(bool mute) {
    if (mute) {
        if (s_output_enabled && s_pa_enabled) {
            codec_write_silence_ms(30);
            vTaskDelay(pdMS_TO_TICKS(8));
        }
        pa_set_enabled(false, "mute");
        s_output_enabled = false;
        s_fade_next_write = true;
        return;
    }

    s_output_enabled = true;
    if (!s_pa_enabled) {
        codec_write_silence_ms(20);
        vTaskDelay(pdMS_TO_TICKS(8));
        pa_set_enabled(true, "unmute");
        vTaskDelay(pdMS_TO_TICKS(8));
        codec_write_silence_ms(20);
    }
    s_fade_next_write = true;
}

/* Write PCM mono → codec_dev write. ES8311 STEREO slot config tự duplicate
 * mono data sang L+R nhờ slot_mask=BOTH. */
int es8311_codec_write_pcm16(const int16_t *pcm, size_t samples) {
    if (!s_dev || !s_output_enabled) return 0;
    if (!s_pa_enabled) {
        es8311_codec_mute(false);
    }
    bool fade = s_fade_next_write;
    int64_t t0 = esp_timer_get_time();
    int written = codec_write_raw_pcm16(pcm, samples, fade);
    int64_t elapsed_us = esp_timer_get_time() - t0;
    if (written > 0) {
        s_fade_next_write = false;
    }
    static uint32_t write_count = 0;
    write_count++;
    if (write_count <= 3 || (write_count % 500) == 0) {   /* 10 s/lần thay 2 s */
        int peak = 0;
        for (size_t i = 0; i < samples; i++) {
            int v = pcm[i] < 0 ? -(int)pcm[i] : (int)pcm[i];
            if (v > peak) peak = v;
        }
        int expected_ms = (int)((samples * 1000) / BOARD_SPK_SAMPLE_RATE);
        ESP_LOGW(TAG_AUDIO, "SPK WRITE #%lu samples=%u peak=%d write_ms=%lld expected_ms=%d i2s_slots=%d",
                 (unsigned long)write_count, (unsigned)samples, peak,
                 (long long)(elapsed_us / 1000), expected_ms,
                 BOARD_AUDIO_CODEC_STEREO_SLOTS ? 2 : 1);
    }
    return written;
}

int es8311_codec_in_channels(void) {
#if BOARD_AUDIO_USE_ES7210_ADC
    return CODEC_IN_CH;
#else
    return 1;
#endif
}

#if BOARD_AUDIO_USE_ES7210_ADC && CONFIG_VIMATE_DIAG_ENABLE
/* Diag: rms từng kênh của N frame đầu và mỗi 500 lần đọc — để xác minh thứ tự kênh
 * TDM (kênh ref chỉ có tín hiệu khi loa phát, mic có ồn phòng). */
static void codec_log_channel_rms(const int16_t *buf, size_t frames) {
#if BOARD_AUDIO_DIAG_TDM_RMS
    /* Số nguyên, không %f: chạy trong afe_feed (stack 4 KB) — printf float ăn > 1 KB stack. */
    static uint32_t calls = 0;
    calls++;
    if (calls != 5 && calls != 20 && (calls % 50) != 0) return;   /* ~3 s/lần @60 ms */
    uint64_t acc[4] = {0};
    for (size_t i = 0; i < frames; i++)
        for (int c = 0; c < CODEC_IN_CH; c++) {
            int32_t v = buf[i * CODEC_IN_CH + c]; acc[c] += (uint64_t)(v * v);
        }
    uint32_t rms[4] = {0};
    for (int c = 0; c < 4; c++) {
        uint32_t m = (uint32_t)(frames ? acc[c] / frames : 0), r = 0, bit = 1u << 30;
        while (bit > m) bit >>= 2;
        while (bit) {                                  /* isqrt nhị phân, ≤ 16 vòng */
            if (m >= r + bit) { m -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
            bit >>= 2;
        }
        rms[c] = r;
    }
    ESP_LOGI(TAG_AUDIO, "TDM ch rms #%lu: ch0=%lu ch1=%lu ch2=%lu ch3=%lu (A=%d B=%d ref=%d)",
             (unsigned long)calls, (unsigned long)rms[0], (unsigned long)rms[1],
             (unsigned long)rms[2], (unsigned long)rms[3],
             BOARD_AUDIO_TDM_MIC_A, BOARD_AUDIO_TDM_MIC_B, BOARD_AUDIO_TDM_REF);
#else
    (void)buf; (void)frames;
#endif
}
#endif

int es8311_codec_read_frames(int16_t *dst, size_t frames) {
    if (!s_dev || !s_input_enabled || !dst || frames == 0) return 0;
#if BOARD_AUDIO_USE_ES7210_ADC
    if (!s_adc_dev) return 0;
    /* ES7210 phát CODEC_IN_CH kênh interleaved 16-bit → đọc thẳng vào dst. */
    int r = esp_codec_dev_read(s_adc_dev, dst, frames * CODEC_IN_CH * sizeof(int16_t));
    if (r != ESP_OK) {
        static int errs = 0;
        if (errs++ < 3) {
            size_t got = 0;
            esp_err_t e = s_rx ? i2s_channel_read(s_rx, dst, frames * CODEC_IN_CH * sizeof(int16_t), &got, 300) : ESP_ERR_INVALID_STATE;
            ESP_LOGW(TAG_AUDIO, "read_frames: esp_codec_dev_read(%u B) = %d; i2s_channel_read truc tiep: %s got=%u",
                     (unsigned)(frames * CODEC_IN_CH * sizeof(int16_t)), r, esp_err_to_name(e), (unsigned)got);
            /* Chẩn đoán trạng thái kênh: enable() trả INVALID_STATE = đang chạy sẵn. */
            esp_err_t er = s_rx ? i2s_channel_enable(s_rx) : ESP_FAIL;
            esp_err_t et = s_tx ? i2s_channel_enable(s_tx) : ESP_FAIL;
            ESP_LOGW(TAG_AUDIO, "read_frames: i2s_channel_enable rx=%s tx=%s (INVALID_STATE = da bat san)",
                     esp_err_to_name(er), esp_err_to_name(et));
        }
        return 0;
    }
#if CONFIG_VIMATE_DIAG_ENABLE
    codec_log_channel_rms(dst, frames);
#endif
    return (int)frames;
#else
    (void)dst;
    return 0;
#endif
}

int es8311_codec_read_stereo_pcm16(int16_t *dst, size_t frames) {
#if CODEC_IN_CH == 2
    return es8311_codec_read_frames(dst, frames);
#else
    (void)dst; (void)frames;
    return 0;   /* board TDM 4 kênh: dùng es8311_codec_read_frames() */
#endif
}

int es8311_codec_read_pcm16(int16_t *dst, size_t samples) {
    if (!s_dev || !s_input_enabled) return 0;
#if BOARD_AUDIO_USE_ES7210_ADC
    if (!s_adc_dev) return 0;
    void *read_buf = dst;
    size_t read_samples = samples;
    size_t need = samples * CODEC_IN_CH;
    if (need > s_read_stereo_cap) {
        int16_t *next = heap_caps_realloc(
            s_read_stereo, need * sizeof(int16_t),
            /* 4 kênh × 1440 = 11,5 KB: RAM nội P4 chật → PSRAM (I2S driver memcpy
             * từ DMA sang buffer này, không cần DMA-capable). */
            CODEC_IN_CH > 2 ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                            : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (!next) {
            return 0;
        }
        s_read_stereo = next;
        s_read_stereo_cap = need;
    }
    read_buf = s_read_stereo;
    read_samples = need;
    int r = esp_codec_dev_read(s_adc_dev ? s_adc_dev : s_dev, read_buf, read_samples * sizeof(int16_t));
    if (r != ESP_OK) return 0;
#if CONFIG_VIMATE_DIAG_ENABLE
    codec_log_channel_rms(s_read_stereo, samples);
#endif
    /* Sum-bão-hòa 2 mic thay vì trung bình: tín hiệu (tương quan) +6dB, nhiễu
     * (không tương quan) chỉ +3dB → +3dB SNR và to gấp đôi. Trước đây average
     * làm tiếng nói thường ~1.8k peak → server ASR trả rỗng ("chưa nghe rõ");
     * cần ~6k mới ra chữ. Kẹp chống clip. */
    for (size_t i = 0, j = 0; i < samples; i++, j += CODEC_IN_CH) {
        int32_t s = (int32_t)s_read_stereo[j + BOARD_AUDIO_TDM_MIC_A]
                  + (int32_t)s_read_stereo[j + BOARD_AUDIO_TDM_MIC_B];
        if (s > 32767) s = 32767;
        else if (s < -32768) s = -32768;
        dst[i] = (int16_t)s;
    }
    return (int)samples;
#else
    /* esp_codec_dev maps channel=1 to a stereo-slot I2S config with
     * channel_mask=left, so read() already returns one mono sample per sample.
     * Reading samples*2 here made one Opus frame consume 120ms of mic data and
     * averaged adjacent mono samples, which weakens VAD/ASR on the P4. */
    int64_t t0 = esp_timer_get_time();
    int r = esp_codec_dev_read(s_dev, dst, samples * sizeof(int16_t));
    if (r != ESP_OK) return 0;
#if defined(BOARD_LCD_USE_ST77922) && BOARD_LCD_USE_ST77922
    /* Board 3.5": mic YẾU — analog 40dB vẫn rms~600 → ASR trả rỗng ("mất voice").
     * Make-up SỐ ×3 (≈+9.5dB), kẹp chống clip. Knob: rè/clip → giảm hệ số;
     * vẫn nhỏ → tăng (đo rms ở log server "ASR ... rms="). */
    for (size_t i = 0; i < samples; i++) {
        int32_t v = (int32_t)dst[i] * 3;
        if (v > 32767) v = 32767;
        else if (v < -32768) v = -32768;
        dst[i] = (int16_t)v;
    }
#endif
    static uint32_t read_count = 0;
    read_count++;
    if (read_count <= 3 || (read_count % 100) == 0) {
        int peak = 0;
        uint64_t sum_abs = 0;
        for (size_t i = 0; i < samples; i++) {
            int v = dst[i] < 0 ? -(int)dst[i] : (int)dst[i];
            if (v > peak) peak = v;
            sum_abs += (uint64_t)v;
        }
        int expected_ms = (int)((samples * 1000) / BOARD_SPK_SAMPLE_RATE);
        ESP_LOGI(TAG_AUDIO,
                 "MIC READ #%lu samples=%u peak=%d avg_abs=%u read_ms=%lld expected_ms=%d i2s_slots=%d",
                 (unsigned long)read_count, (unsigned)samples, peak,
                 (unsigned)(sum_abs / (samples > 0 ? samples : 1)),
                 (long long)((esp_timer_get_time() - t0) / 1000),
                 expected_ms,
                 BOARD_AUDIO_CODEC_STEREO_SLOTS ? 2 : 1);
    }
    return (int)samples;
#endif
}

void es8311_codec_enable_input(bool enable) {
    s_input_enabled = enable;
}

int es8311_codec_input_sample_rate(void) {
#if BOARD_AUDIO_USE_ES7210_ADC
    return BOARD_MIC_SAMPLE_RATE;       /* ES7210 mở riêng ở mic rate */
#else
    return BOARD_SPK_SAMPLE_RATE;       /* duplex: ADC chung clock với DAC (24kHz) */
#endif
}

#else /* BOARD_AUDIO_USE_ES8311 */

static i2s_chan_handle_t s_tx = NULL;
static i2s_chan_handle_t s_rx = NULL;
static int s_volume = BOARD_SPK_VOLUME_DEFAULT;
static bool s_output_enabled = false;
static bool s_input_enabled = false;
static int32_t *s_read32 = NULL;
static size_t s_read32_cap = 0;

static uint64_t raw_gpio_mask(int pin) {
    if (pin < 0 || pin >= 64) {
        return 0;
    }
    return 1ULL << (unsigned)pin;
}

esp_codec_dev_handle_t es8311_get_dev(void) { return NULL; }
i2s_chan_handle_t es8311_get_tx(void) { return s_tx; }
i2s_chan_handle_t es8311_get_rx(void) { return s_rx; }

static esp_err_t raw_i2s_output_init(void) {
    i2s_chan_config_t tx_cfg = {
        .id = BOARD_SPK_I2S_NUM,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_RETURN_ON_ERROR(i2s_new_channel(&tx_cfg, &s_tx, NULL),
                        TAG_AUDIO, "raw I2S tx channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)BOARD_SPK_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
#ifdef I2S_HW_VERSION_2
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
#endif
        },
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = BOARD_SPK_I2S_BCLK,
            .ws = BOARD_SPK_I2S_WS,
            .dout = BOARD_SPK_I2S_DOUT,
            .din = GPIO_NUM_NC,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std_cfg),
                        TAG_AUDIO, "raw I2S tx std init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx),
                        TAG_AUDIO, "raw I2S tx enable failed");
    return ESP_OK;
}

static esp_err_t raw_i2s_input_init(void) {
    i2s_chan_config_t rx_cfg = {
        .id = BOARD_MIC_I2S_NUM,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_RETURN_ON_ERROR(i2s_new_channel(&rx_cfg, NULL, &s_rx),
                        TAG_AUDIO, "raw I2S rx channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)BOARD_MIC_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
#ifdef I2S_HW_VERSION_2
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
#endif
        },
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = BOARD_MIC_I2S_BCLK,
            .ws = BOARD_MIC_I2S_WS,
            .dout = GPIO_NUM_NC,
            .din = BOARD_MIC_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std_cfg),
                        TAG_AUDIO, "raw I2S rx std init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx),
                        TAG_AUDIO, "raw I2S rx enable failed");
    return ESP_OK;
}

esp_err_t es8311_codec_init(void) {
    const int pa_pin = BOARD_AUDIO_PA_PIN;
    if (pa_pin >= 0) {
        gpio_config_t pa_cfg = {
            .pin_bit_mask = raw_gpio_mask(pa_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&pa_cfg);
        gpio_set_level((gpio_num_t)pa_pin, BOARD_AUDIO_PA_ON_LEVEL ? 0 : 1);
    }
    ESP_RETURN_ON_ERROR(raw_i2s_output_init(), TAG_AUDIO, "raw output init failed");
    ESP_RETURN_ON_ERROR(raw_i2s_input_init(), TAG_AUDIO, "raw input init failed");
    s_output_enabled = true;
    s_input_enabled = false;
    ESP_LOGI(TAG_AUDIO,
             "Raw I2S audio ready (spk=%dHz port=%d bclk=%d ws=%d dout=%d, mic=%dHz port=%d bclk=%d ws=%d din=%d)",
             BOARD_SPK_SAMPLE_RATE, BOARD_SPK_I2S_NUM, BOARD_SPK_I2S_BCLK,
             BOARD_SPK_I2S_WS, BOARD_SPK_I2S_DOUT, BOARD_MIC_SAMPLE_RATE,
             BOARD_MIC_I2S_NUM, BOARD_MIC_I2S_BCLK, BOARD_MIC_I2S_WS,
             BOARD_MIC_I2S_DIN);
    return ESP_OK;
}

void es8311_codec_set_volume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_volume = percent;
}

void es8311_codec_set_mic_gain(int percent) {
    (void)percent;
}

void es8311_codec_mute(bool mute) {
    s_output_enabled = !mute;
    if (BOARD_AUDIO_PA_PIN >= 0) {
        int enabled = BOARD_AUDIO_PA_ON_LEVEL ? 1 : 0;
        gpio_set_level(BOARD_AUDIO_PA_PIN, mute ? !enabled : enabled);
    }
}

int es8311_codec_write_pcm16(const int16_t *pcm, size_t samples) {
    if (!s_tx || !s_output_enabled || !pcm || samples == 0) return 0;
    int32_t stack_buf[240];
    size_t done = 0;
    int32_t volume_factor = ((int32_t)s_volume * (int32_t)s_volume * 65536) / 10000;
    while (done < samples) {
        size_t chunk = samples - done;
        if (chunk > sizeof(stack_buf) / sizeof(stack_buf[0])) {
            chunk = sizeof(stack_buf) / sizeof(stack_buf[0]);
        }
        for (size_t i = 0; i < chunk; i++) {
            int64_t scaled = (int64_t)pcm[done + i] * volume_factor;
            if (scaled > INT32_MAX) {
                stack_buf[i] = INT32_MAX;
            } else if (scaled < INT32_MIN) {
                stack_buf[i] = INT32_MIN;
            } else {
                stack_buf[i] = (int32_t)scaled;
            }
        }
        size_t bytes_written = 0;
        esp_err_t r = i2s_channel_write(s_tx, stack_buf,
                                        chunk * sizeof(int32_t),
                                        &bytes_written, pdMS_TO_TICKS(500));
        if (r != ESP_OK) return (int)done;
        done += bytes_written / sizeof(int32_t);
    }
    return (int)done;
}

int es8311_codec_read_pcm16(int16_t *dst, size_t samples) {
    if (!s_rx || !s_input_enabled || !dst || samples == 0) return 0;
    if (samples > s_read32_cap) {
        int32_t *next = heap_caps_realloc(s_read32, samples * sizeof(int32_t),
                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!next) {
            next = heap_caps_realloc(s_read32, samples * sizeof(int32_t),
                                     MALLOC_CAP_8BIT);
        }
        if (!next) return 0;
        s_read32 = next;
        s_read32_cap = samples;
    }
    size_t bytes_read = 0;
    esp_err_t r = i2s_channel_read(s_rx, s_read32, samples * sizeof(int32_t),
                                   &bytes_read, pdMS_TO_TICKS(200));
    if (r != ESP_OK) return 0;
    size_t got = bytes_read / sizeof(int32_t);
    for (size_t i = 0; i < got; i++) {
        int32_t v = s_read32[i] >> 12;
        if (v > INT16_MAX) v = INT16_MAX;
        if (v < INT16_MIN) v = INT16_MIN;
        dst[i] = (int16_t)v;
    }
    return (int)got;
}

void es8311_codec_enable_input(bool enable) {
    s_input_enabled = enable;
}

int es8311_codec_input_sample_rate(void) {
    return BOARD_MIC_SAMPLE_RATE;       /* raw I2S: rx mở riêng ở mic rate */
}

#endif /* BOARD_AUDIO_USE_ES8311 */
