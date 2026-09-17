#include <esp_check.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <inttypes.h>
#include <sys/param.h>
#include <stdbool.h>
#include <string.h>

#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"

#include "boards/board.h"
#include "jpeg_to_image.h"

#ifdef CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL MAX(CONFIG_LOG_DEFAULT_LEVEL, ESP_LOG_DEBUG)
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
#include <esp_log.h>

#ifdef CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_DECODER
#include "driver/jpeg_decode.h"
#endif

#define TAG "jpeg_to_image"
#define JPEG_LCD_TARGET_W BOARD_LCD_H_RES
#define JPEG_LCD_TARGET_H BOARD_LCD_V_RES

static size_t jpeg_to_image_bpp(jpeg_to_image_format_t format) {
    return (format == JPEG_TO_IMAGE_RGB888) ? 3U : 2U;
}

static uint16_t align_down_8(uint16_t v) {
    if (v < 8) return v;
    return (uint16_t)(v & ~0x7);
}

static bool choose_lcd_scale(uint16_t src_w, uint16_t src_h, uint16_t *dst_w, uint16_t *dst_h) {
    if (!dst_w || !dst_h || src_w == 0 || src_h == 0) return false;
    if (src_w <= JPEG_LCD_TARGET_W && src_h <= JPEG_LCD_TARGET_H) return false;
    uint16_t w = src_w > JPEG_LCD_TARGET_W ? JPEG_LCD_TARGET_W : src_w;
    uint16_t h = src_h > JPEG_LCD_TARGET_H ? JPEG_LCD_TARGET_H : src_h;
    w = align_down_8(w);
    h = align_down_8(h);
    if (w < 8 || h < 8) return false;
    *dst_w = w;
    *dst_h = h;
    return true;
}

static esp_err_t decode_with_new_jpeg(const uint8_t* src, size_t src_len, jpeg_to_image_format_t format,
                                      uint8_t *dst, size_t dst_len,
                                      uint8_t** out, size_t* out_len, size_t* width,
                                      size_t* height, size_t* stride) {
    ESP_LOGD(TAG, "Decoding JPEG with software decoder");
    esp_err_t ret = ESP_OK;
    jpeg_error_t jpeg_ret = JPEG_ERR_OK;
    uint8_t* out_buf = NULL;
    bool out_buf_owned = false;
    jpeg_dec_io_t jpeg_io = {0};
    jpeg_dec_header_info_t out_info = {0};

    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = (format == JPEG_TO_IMAGE_RGB888) ? JPEG_PIXEL_FORMAT_RGB888 : JPEG_PIXEL_FORMAT_RGB565_LE;
    config.rotate = JPEG_ROTATE_0D;

    jpeg_dec_handle_t jpeg_dec = NULL;
    jpeg_ret = jpeg_dec_open(&config, &jpeg_dec);
    if (jpeg_ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open JPEG decoder");
        ret = ESP_FAIL;
        goto jpeg_dec_failed;
    }

    jpeg_io.inbuf = (uint8_t*)src;
    jpeg_io.inbuf_len = (int)src_len;

    jpeg_ret = jpeg_dec_parse_header(jpeg_dec, &jpeg_io, &out_info);
    if (jpeg_ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Failed to parse JPEG header");
        ret = ESP_ERR_INVALID_ARG;
        goto jpeg_dec_failed;
    }

    ESP_LOGD(TAG, "JPEG header info: width=%d, height=%d", out_info.width, out_info.height);

    uint16_t scale_w = 0, scale_h = 0;
    if (choose_lcd_scale(out_info.width, out_info.height, &scale_w, &scale_h)) {
        ESP_LOGI(TAG, "JPEG scale %ux%u -> %ux%u",
                 (unsigned)out_info.width, (unsigned)out_info.height,
                 (unsigned)scale_w, (unsigned)scale_h);
        jpeg_dec_close(jpeg_dec);
        jpeg_dec = NULL;

        config.scale.width = scale_w;
        config.scale.height = scale_h;
        jpeg_ret = jpeg_dec_open(&config, &jpeg_dec);
        if (jpeg_ret != JPEG_ERR_OK) {
            ESP_LOGE(TAG, "Failed to reopen JPEG decoder with scale");
            ret = ESP_FAIL;
            goto jpeg_dec_failed;
        }
        jpeg_io.inbuf = (uint8_t*)src;
        jpeg_io.inbuf_len = (int)src_len;
        memset(&out_info, 0, sizeof(out_info));
        jpeg_ret = jpeg_dec_parse_header(jpeg_dec, &jpeg_io, &out_info);
        if (jpeg_ret != JPEG_ERR_OK) {
            ESP_LOGE(TAG, "Failed to parse scaled JPEG header");
            ret = ESP_ERR_INVALID_ARG;
            goto jpeg_dec_failed;
        }
    }

    /* Chặn integer-overflow: dim từ header JPEG (có thể hỏng/giả) — nếu quá lớn
     * thì width*height*2 wrap → alloc nhỏ → heap overflow lúc decode. Panel lớn
     * nhất ~1280x720 nên 4096 là trần rộng rãi an toàn. */
    if (out_info.width == 0 || out_info.height == 0 ||
        out_info.width > 4096 || out_info.height > 4096) {
        ESP_LOGE(TAG, "JPEG kích thước bất thường %dx%d — bỏ", out_info.width, out_info.height);
        ret = ESP_ERR_INVALID_ARG;
        goto jpeg_dec_failed;
    }
    size_t bpp = jpeg_to_image_bpp(format);
    size_t out_bytes = (size_t)out_info.width * (size_t)out_info.height * bpp;
    if (dst) {
        if (dst_len < out_bytes) {
            ESP_LOGE(TAG, "JPEG output buffer too small: have=%u need=%u",
                     (unsigned)dst_len, (unsigned)out_bytes);
            ret = ESP_ERR_INVALID_SIZE;
            goto jpeg_dec_failed;
        }
        out_buf = dst;
    } else {
        out_buf = heap_caps_aligned_calloc(16, 1, out_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (out_buf == NULL) {
            out_buf = heap_caps_aligned_calloc(16, 1, out_bytes, MALLOC_CAP_8BIT);
        }
        out_buf_owned = (out_buf != NULL);
    }
    if (out_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for JPEG output buffer");
        ret = ESP_ERR_NO_MEM;
        goto jpeg_dec_failed;
    }

    jpeg_io.outbuf = out_buf;
    jpeg_ret = jpeg_dec_process(jpeg_dec, &jpeg_io);
    if (jpeg_ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Failed to decode JPEG");
        ret = ESP_FAIL;
        goto jpeg_dec_failed;
    }

    ESP_LOG_BUFFER_HEXDUMP(TAG, out_buf, MIN(out_bytes, 256), ESP_LOG_DEBUG);

    *out = out_buf;
    if (out_buf_owned) {
        out_buf = NULL;
    }
    *out_len = out_bytes;
    *width = (size_t)out_info.width;
    *height = (size_t)out_info.height;
    *stride = (size_t)out_info.width * bpp;
    jpeg_dec_close(jpeg_dec);
    jpeg_dec = NULL;

    return ret;

jpeg_dec_failed:
    if (jpeg_dec) {
        jpeg_dec_close(jpeg_dec);
        jpeg_dec = NULL;
    }
    if (out_buf && out_buf_owned) {
        heap_caps_free(out_buf);
        out_buf = NULL;
    }

    *out = NULL;
    *out_len = 0;
    *width = 0;
    *height = 0;
    *stride = 0;
    return ret;
}

#ifdef CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_DECODER
static esp_err_t decode_with_hardware_jpeg(const uint8_t* src, size_t src_len, uint8_t** out, size_t* out_len,
                                           size_t* width, size_t* height, size_t* stride) {
    ESP_LOGD(TAG, "Decoding JPEG with hardware decoder");
    esp_err_t ret = ESP_OK;

    jpeg_decoder_handle_t jpeg_dec = NULL;
    uint8_t* bit_stream = NULL;
    uint8_t* out_buf = NULL;
    size_t out_buf_len = 0;
    size_t tx_buffer_size = 0;
    size_t rx_buffer_size = 0;

    jpeg_decode_engine_cfg_t eng_cfg = {
        .intr_priority = 1,
        .timeout_ms = 1000,
    };

    jpeg_decode_cfg_t decode_cfg_rgb = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
    };

    ret = jpeg_new_decoder_engine(&eng_cfg, &jpeg_dec);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create JPEG decoder engine");
        goto jpeg_hw_dec_failed;
    }

    jpeg_decode_memory_alloc_cfg_t tx_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER,
    };

    jpeg_decode_memory_alloc_cfg_t rx_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };

    bit_stream = (uint8_t*)jpeg_alloc_decoder_mem(src_len, &tx_mem_cfg, &tx_buffer_size);
    if (bit_stream == NULL || tx_buffer_size < src_len) {
        ESP_LOGE(TAG, "Failed to allocate memory for JPEG bit stream");
        ret = ESP_ERR_NO_MEM;
        goto jpeg_hw_dec_failed;
    }

    memcpy(bit_stream, src, src_len);

    jpeg_decode_picture_info_t header_info;
    ESP_GOTO_ON_ERROR(jpeg_decoder_get_info(bit_stream, src_len, &header_info), jpeg_hw_dec_failed, TAG,
                      "Failed to get JPEG header info");

    ESP_LOGD(TAG, "JPEG header info: width=%" PRId32 ", height=%" PRId32 ", sample_method=%d", (int32_t)header_info.width, (int32_t)header_info.height,
             (int)header_info.sample_method);

    /* Chặn integer-overflow như đường software ở trên (dim header không tin cậy). */
    if (header_info.width == 0 || header_info.height == 0 ||
        header_info.width > 4096 || header_info.height > 4096) {
        ESP_LOGE(TAG, "JPEG kích thước bất thường %" PRId32 "x%" PRId32 " — bỏ", (int32_t)header_info.width, (int32_t)header_info.height);
        ret = ESP_ERR_INVALID_ARG;
        goto jpeg_hw_dec_failed;
    }

    switch (header_info.sample_method) {
        case JPEG_DOWN_SAMPLING_GRAY:
        case JPEG_DOWN_SAMPLING_YUV444:
            out_buf_len = header_info.width * header_info.height * 2;
            *stride = header_info.width * 2;
            break;
        case JPEG_DOWN_SAMPLING_YUV422:
        case JPEG_DOWN_SAMPLING_YUV420:
            out_buf_len = ((header_info.width + 15) & ~15) * ((header_info.height + 15) & ~15) * 2;
            *stride = ((header_info.width + 15) & ~15) * 2;
            break;
        default:
            ESP_LOGE(TAG, "Unsupported JPEG sample method");
            ret = ESP_ERR_NOT_SUPPORTED;
            goto jpeg_hw_dec_failed;
    }

    out_buf = (uint8_t*)jpeg_alloc_decoder_mem(out_buf_len, &rx_mem_cfg, &rx_buffer_size);
    if (out_buf == NULL || rx_buffer_size < out_buf_len) {
        ESP_LOGE(TAG, "Failed to allocate memory for JPEG output buffer");
        ret = ESP_ERR_NO_MEM;
        goto jpeg_hw_dec_failed;
    }

    uint32_t out_size = 0;

    ESP_GOTO_ON_ERROR(
        jpeg_decoder_process(jpeg_dec, &decode_cfg_rgb, bit_stream, src_len, out_buf, out_buf_len, &out_size),
        jpeg_hw_dec_failed, TAG, "Failed to decode JPEG");

    ESP_LOGD(TAG, "Expected %" PRIu32 " bytes, got %" PRIu32 " bytes", (uint32_t)out_buf_len, out_size);

    if (out_size != out_buf_len) {
        ESP_LOGE(TAG, "Decoded image size mismatch: Expected %" PRIu32 " bytes, got %" PRIu32 " bytes", (uint32_t)out_buf_len, out_size);
        ret = ESP_ERR_INVALID_SIZE;
        goto jpeg_hw_dec_failed;
    }

    if (header_info.sample_method == JPEG_DOWN_SAMPLING_GRAY) {
        // convert GRAY8 to RGB565
        uint32_t i = header_info.width * header_info.height;
        do {
            --i;
            uint8_t r = (out_buf[i] >> 3) & 0x1F;
            uint8_t g = (out_buf[i] >> 2) & 0x3F;
            // b is same as r
            uint16_t rgb565 = (r << 11) | (g << 5) | r;
            out_buf[2 * i + 1] = (rgb565 >> 8) & 0xFF;
            out_buf[2 * i] = rgb565 & 0xFF;
        } while (i != 0);
        out_size = header_info.width * header_info.height * 2;
        ESP_LOGD(TAG, "Converted GRAY8 to RGB565, new size: %" PRIu32, out_size);
    }

    ESP_LOG_BUFFER_HEXDUMP(TAG, out_buf, MIN(out_size, 256), ESP_LOG_DEBUG);

    *out = out_buf;
    out_buf = NULL;
    *out_len = (size_t)out_size;
    jpeg_del_decoder_engine(jpeg_dec);
    jpeg_dec = NULL;
    heap_caps_free(bit_stream);
    bit_stream = NULL;
    *width = header_info.width;
    *height = header_info.height;

    return ret;

jpeg_hw_dec_failed:
    if (out_buf) {
        heap_caps_free(out_buf);
        out_buf = NULL;
    }
    if (bit_stream) {
        heap_caps_free(bit_stream);
        bit_stream = NULL;
    }
    if (jpeg_dec) {
        jpeg_del_decoder_engine(jpeg_dec);
        jpeg_dec = NULL;
    }
    *out = NULL;
    *out_len = 0;
    *width = 0;
    *height = 0;
    *stride = 0;
    return ret;
}
#endif  // CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_DECODER

esp_err_t jpeg_to_image_ex(const uint8_t* src, size_t src_len, jpeg_to_image_format_t format,
                           uint8_t** out, size_t* out_len, size_t* width,
                           size_t* height, size_t* stride) {
#ifdef CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
    if (src == NULL || src_len == 0 || out == NULL || out_len == NULL || width == NULL || height == NULL ||
        stride == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    if (format == JPEG_TO_IMAGE_RGB888) {
        return decode_with_new_jpeg(src, src_len, format, NULL, 0, out, out_len, width, height, stride);
    }
#ifdef CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_DECODER
    esp_err_t ret = decode_with_hardware_jpeg(src, src_len, out, out_len, width, height, stride);
    if (ret == ESP_OK) {
        return ret;
    }
    ESP_LOGW(TAG, "Failed to decode with hardware JPEG, fallback to software decoder");
    // Fallback to esp_new_jpeg
#endif
    return decode_with_new_jpeg(src, src_len, JPEG_TO_IMAGE_RGB565, NULL, 0,
                                out, out_len, width, height, stride);
}

esp_err_t jpeg_to_image(const uint8_t* src, size_t src_len, uint8_t** out, size_t* out_len, size_t* width,
                        size_t* height, size_t* stride) {
    return jpeg_to_image_ex(src, src_len, JPEG_TO_IMAGE_RGB565,
                            out, out_len, width, height, stride);
}

esp_err_t jpeg_to_image_rgb888_into(const uint8_t* src, size_t src_len, uint8_t *dst, size_t dst_len,
                                    size_t* out_len, size_t* width, size_t* height, size_t* stride) {
    if (!dst || dst_len == 0) return ESP_ERR_INVALID_ARG;
    uint8_t *out = NULL;
    return decode_with_new_jpeg(src, src_len, JPEG_TO_IMAGE_RGB888, dst, dst_len,
                                &out, out_len, width, height, stride);
}
