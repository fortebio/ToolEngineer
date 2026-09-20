/**
 * beep.c — bíp qua ES8311 + PA trên board ES3N28P (xem beep.h). Rút từ
 * firmware/maping new product/firmware-vimate-p4/main/audio/vimate_es8311.c (Bizgeni, chạy thật
 * trên cùng board 12/09/2026): giữ đúng cấu hình I2S (STEREO slot + SLOT_BOTH + bit_shift +
 * MCLK ×256 — yêu cầu của ES8311), esp_codec_dev open channel=1 (mono, driver map sang slot trái),
 * PA GPIO1 active-LOW; bỏ mic/ES7210/AFE/diag.
 *
 * Kiến trúc: beep_play() chỉ đẩy id vào queue; task "beep" (ưu tiên thấp, core IO) sinh sóng sin
 * 16-bit theo từng khúc 10 ms và ghi codec. Chống "pop": bật PA → 20 ms im → tiếng (fade 5 ms
 * vào/ra) → 20 ms im → tắt PA; giữa các tiếng PA tắt nên không rè khi máy im.
 * PA điều khiển TAY (pa_pin = -1 trong codec cfg) để trình tự trên chắc chắn.
 */
#include "beep.h"

#if CONFIG_RAPID4P_BEEP

#include "rapid4p.h"
#include "boards/board.h"
#include "core/task_profile.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

#define TAG_BEEP "r4p.beep"

#ifndef BOARD_BEEP_VOLUME
#define BOARD_BEEP_VOLUME 80
#endif

#define CHUNK_SAMPLES   (BOARD_SPK_SAMPLE_RATE / 100)   /* 10 ms */
#define FADE_MS         5
#define PAD_MS          20
#define AMPLITUDE       14000                            /* ~0,43 FS: loa nhỏ không vỡ tiếng */

/* Một "nốt": tần số (0 = nghỉ) + thời lượng. */
typedef struct { uint16_t hz, ms; } note_t;

static const note_t s_key[]   = { { 2000, 40 } };
static const note_t s_done[]  = { { 1200, 110 }, { 0, 60 }, { 1600, 140 } };
static const note_t s_error[] = { { 400, 400 } };
static const note_t s_boot[]  = { { 1000, 60 }, { 0, 30 }, { 1500, 80 } };

static const struct { const note_t *n; int count; } s_tunes[BEEP_COUNT] = {
    [BEEP_KEY]   = { s_key,   sizeof(s_key) / sizeof(s_key[0]) },
    [BEEP_DONE]  = { s_done,  sizeof(s_done) / sizeof(s_done[0]) },
    [BEEP_ERROR] = { s_error, sizeof(s_error) / sizeof(s_error[0]) },
    [BEEP_BOOT]  = { s_boot,  sizeof(s_boot) / sizeof(s_boot[0]) },
};

static struct {
    bool ok;
    QueueHandle_t q;
    i2s_chan_handle_t tx;
    const audio_codec_data_if_t *data_if;
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    const audio_codec_if_t *codec_if;
    esp_codec_dev_handle_t dev;
    int16_t buf[CHUNK_SAMPLES];
} s;

static void pa_set(bool on)
{
    if (BOARD_AUDIO_PA_PIN < 0) return;
    gpio_set_level(BOARD_AUDIO_PA_PIN, on ? BOARD_AUDIO_PA_ON_LEVEL : !BOARD_AUDIO_PA_ON_LEVEL);
}

static void write_silence(int ms)
{
    memset(s.buf, 0, sizeof(s.buf));
    for (int t = 0; t < ms; t += 10) esp_codec_dev_write(s.dev, s.buf, sizeof(s.buf));
}

/* Sin liên tục theo pha (không nhảy pha giữa hai khúc), fade vào/ra để không "tách". */
static void play_note(const note_t *n)
{
    if (n->hz == 0) { write_silence(n->ms); return; }
    const int total = BOARD_SPK_SAMPLE_RATE * n->ms / 1000;
    const int fade = BOARD_SPK_SAMPLE_RATE * FADE_MS / 1000;
    const float step = 2.0f * (float)M_PI * (float)n->hz / (float)BOARD_SPK_SAMPLE_RATE;
    float phase = 0.0f;
    for (int done = 0; done < total; done += CHUNK_SAMPLES) {
        for (int i = 0; i < CHUNK_SAMPLES; i++) {
            const int k = done + i;
            float a = 1.0f;
            if (k >= total) { s.buf[i] = 0; continue; }
            if (k < fade) a = (float)k / (float)fade;
            else if (total - k < fade) a = (float)(total - k) / (float)fade;
            s.buf[i] = (int16_t)(a * AMPLITUDE * sinf(phase));
            phase += step;
            if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        }
        esp_codec_dev_write(s.dev, s.buf, sizeof(s.buf));
    }
}

static void beep_task(void *arg)
{
    (void)arg;
    beep_id_t id;
    while (xQueueReceive(s.q, &id, portMAX_DELAY) == pdTRUE) {
        if (id >= BEEP_COUNT) continue;
        pa_set(true);
        write_silence(PAD_MS);
        for (int i = 0; i < s_tunes[id].count; i++) play_note(&s_tunes[id].n[i]);
        /* Gom các bíp phím tới dồn (nhấn liên tiếp) mà không tắt/bật PA từng tiếng. */
        while (xQueueReceive(s.q, &id, pdMS_TO_TICKS(30)) == pdTRUE) {
            if (id < BEEP_COUNT) for (int i = 0; i < s_tunes[id].count; i++) play_note(&s_tunes[id].n[i]);
        }
        write_silence(PAD_MS);
        pa_set(false);
    }
}

static esp_err_t i2s_tx_init(void)
{
    i2s_chan_config_t chan_cfg = {
        .id = BOARD_AUDIO_I2S_NUM,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 4,
        .dma_frame_num = CHUNK_SAMPLES,
        .auto_clear_after_cb = true,      /* hết dữ liệu → phát 0, không lặp khúc cũ */
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    esp_err_t r = i2s_new_channel(&chan_cfg, &s.tx, NULL);
    if (r != ESP_OK) return r;
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)BOARD_SPK_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,     /* ES8311 cần slot STEREO */
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,                      /* Philips */
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
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    r = i2s_channel_init_std_mode(s.tx, &std_cfg);
    if (r == ESP_OK) r = i2s_channel_enable(s.tx);
    return r;
}

esp_err_t beep_init(void)
{
#if !BOARD_AUDIO_USE_ES8311
    ESP_LOGI(TAG_BEEP, "board khong co codec -> khong bip");
    return ESP_ERR_NOT_SUPPORTED;
#else
    /* I2C: tái dùng bus touch/cảm biến (I2C0) — có rồi thì lấy handle, chưa có thì tạo. */
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t r = i2c_master_get_bus_handle(BOARD_AUDIO_CODEC_I2C_NUM, &bus);
    if (r != ESP_OK || !bus) {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port = BOARD_AUDIO_CODEC_I2C_NUM,
            .sda_io_num = BOARD_AUDIO_CODEC_I2C_SDA,
            .scl_io_num = BOARD_AUDIO_CODEC_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        r = i2c_new_master_bus(&bus_cfg, &bus);
        if (r != ESP_OK) { ESP_LOGW(TAG_BEEP, "I2C bus: %s -> khong bip", esp_err_to_name(r)); return r; }
    }
    if (i2c_master_probe(bus, BOARD_AUDIO_CODEC_I2C_ADDR, 50) != ESP_OK) {
        ESP_LOGW(TAG_BEEP, "khong thay ES8311 0x%02X tren I2C%d -> khong bip (loa/codec chua co)",
                 BOARD_AUDIO_CODEC_I2C_ADDR, (int)BOARD_AUDIO_CODEC_I2C_NUM);
        return ESP_ERR_NOT_FOUND;
    }

    if (BOARD_AUDIO_PA_PIN >= 0) {
        gpio_config_t pa = { .pin_bit_mask = 1ULL << BOARD_AUDIO_PA_PIN, .mode = GPIO_MODE_OUTPUT };
        gpio_config(&pa);
        gpio_set_drive_capability(BOARD_AUDIO_PA_PIN, GPIO_DRIVE_CAP_3);
        pa_set(false);
    }

    r = i2s_tx_init();
    if (r != ESP_OK) { ESP_LOGW(TAG_BEEP, "I2S: %s -> khong bip", esp_err_to_name(r)); return r; }

    audio_codec_i2s_cfg_t i2s_cfg = { .port = BOARD_AUDIO_I2S_NUM, .rx_handle = NULL, .tx_handle = s.tx };
    s.data_if = audio_codec_new_i2s_data(&i2s_cfg);
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BOARD_AUDIO_CODEC_I2C_NUM, .addr = BOARD_AUDIO_CODEC_I2C_ADDR << 1, .bus_handle = bus,
    };
    s.ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    s.gpio_if = audio_codec_new_gpio();
    if (!s.data_if || !s.ctrl_if || !s.gpio_if) { ESP_LOGW(TAG_BEEP, "codec if fail"); return ESP_FAIL; }

    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = s.ctrl_if,
        .gpio_if = s.gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,                         /* PA điều khiển tay (pa_set) */
        .use_mclk = true,
        .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
        .master_mode = false,
    };
    s.codec_if = es8311_codec_new(&es_cfg);
    if (!s.codec_if) { ESP_LOGW(TAG_BEEP, "es8311_codec_new fail"); return ESP_FAIL; }
    esp_codec_dev_cfg_t dev_cfg = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = s.codec_if, .data_if = s.data_if };
    s.dev = esp_codec_dev_new(&dev_cfg);
    if (!s.dev) { ESP_LOGW(TAG_BEEP, "esp_codec_dev_new fail"); return ESP_FAIL; }
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16, .channel = 1, .channel_mask = 0,
        .sample_rate = (uint32_t)BOARD_SPK_SAMPLE_RATE, .mclk_multiple = 0,
    };
    r = esp_codec_dev_open(s.dev, &fs);
    if (r != ESP_OK) { ESP_LOGW(TAG_BEEP, "codec open: %s -> khong bip", esp_err_to_name(r)); return r; }
    esp_codec_dev_set_out_vol(s.dev, BOARD_BEEP_VOLUME);

    s.q = xQueueCreate(6, sizeof(beep_id_t));
    if (!s.q) return ESP_ERR_NO_MEM;
    xTaskCreatePinnedToCore(beep_task, "beep", 3072, NULL, R4P_TASK_PRIO_BACKGROUND, NULL, R4P_TASK_CORE_IO);
    s.ok = true;
    ESP_LOGI(TAG_BEEP, "ES8311 ok: I2S%d MCLK%d BCLK%d WS%d DOUT%d PA GPIO%d (on=%d) %d Hz vol %d",
             (int)BOARD_AUDIO_I2S_NUM, BOARD_AUDIO_I2S_MCLK, BOARD_AUDIO_I2S_BCLK, BOARD_AUDIO_I2S_WS,
             BOARD_AUDIO_I2S_DOUT, BOARD_AUDIO_PA_PIN, BOARD_AUDIO_PA_ON_LEVEL, BOARD_SPK_SAMPLE_RATE,
             BOARD_BEEP_VOLUME);
    beep_play(BEEP_BOOT);
    return ESP_OK;
#endif
}

bool beep_available(void) { return s.ok; }

void beep_play(beep_id_t id)
{
    if (!s.ok || !s.q) return;
    xQueueSend(s.q, &id, 0);   /* đầy → bỏ, không chặn task gọi (LVGL/display) */
}

#endif /* CONFIG_RAPID4P_BEEP */
