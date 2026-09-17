#include "sensor_bus.h"
#include "tca9548.h"
#include "tcs34725.h"
#include "rapid4p.h"
#include "esp_check.h"
#include "boards/board.h"

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_mux, s_tcs;

esp_err_t sensor_bus_init(void)
{
    if (s_bus) return ESP_OK;
    esp_err_t r = i2c_master_get_bus_handle(BOARD_SENSOR_I2C_NUM, &s_bus);
    if (r == ESP_OK && s_bus) {
        ESP_LOGI(TAG_SENSOR, "sensor: dung lai bus I2C%d", (int)BOARD_SENSOR_I2C_NUM);
    } else {
        i2c_master_bus_config_t cfg = {
            .i2c_port = BOARD_SENSOR_I2C_NUM,
            .sda_io_num = BOARD_SENSOR_I2C_SDA,
            .scl_io_num = BOARD_SENSOR_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            /* Pull-up nội chỉ là dự phòng lúc chưa cắm bo con; bo con phải có 4,7 K. */
            .flags.enable_internal_pullup = true,
        };
        r = i2c_new_master_bus(&cfg, &s_bus);
        if (r != ESP_OK) {
            ESP_LOGE(TAG_SENSOR, "sensor I2C%d SDA=%d SCL=%d loi: %s", (int)BOARD_SENSOR_I2C_NUM,
                     BOARD_SENSOR_I2C_SDA, BOARD_SENSOR_I2C_SCL, esp_err_to_name(r));
            return r;
        }
    }
    i2c_device_config_t dmux = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = BOARD_SENSOR_MUX_ADDR,
        .scl_speed_hz = BOARD_SENSOR_I2C_FREQ_HZ,
    };
    i2c_device_config_t dtcs = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = BOARD_SENSOR_TCS_ADDR,
        .scl_speed_hz = BOARD_SENSOR_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dmux, &s_mux), TAG_SENSOR, "add mux");
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dtcs, &s_tcs), TAG_SENSOR, "add tcs");
    ESP_LOGI(TAG_SENSOR, "sensor bus I2C%d SDA=%d SCL=%d %d kHz mux=0x%02X tcs=0x%02X",
             (int)BOARD_SENSOR_I2C_NUM, BOARD_SENSOR_I2C_SDA, BOARD_SENSOR_I2C_SCL,
             BOARD_SENSOR_I2C_FREQ_HZ / 1000, BOARD_SENSOR_MUX_ADDR, BOARD_SENSOR_TCS_ADDR);
    return ESP_OK;
}

i2c_master_bus_handle_t sensor_bus_handle(void) { return s_bus; }
i2c_master_dev_handle_t sensor_bus_mux_dev(void) { return s_mux; }
i2c_master_dev_handle_t sensor_bus_tcs_dev(void) { return s_tcs; }

int sensor_bus_probe(void)
{
    if (!s_bus) return 0;
    esp_err_t r = i2c_master_probe(s_bus, BOARD_SENSOR_MUX_ADDR, 50);
    ESP_LOGI(TAG_SENSOR, "probe TCA9548 0x%02X: %s", BOARD_SENSOR_MUX_ADDR, esp_err_to_name(r));
    if (r != ESP_OK) return 0;
    static const uint8_t ch[BOARD_SENSOR_SLOTS] = BOARD_SENSOR_MUX_CHANNELS;
    int alive = 0;
    for (int s = 0; s < BOARD_SENSOR_SLOTS; s++) {
        if (tca9548_select(ch[s]) != ESP_OK) continue;
        uint8_t id = 0;
        esp_err_t e = tcs34725_read_id(&id);
        ESP_LOGI(TAG_SENSOR, "slot %d (mux ch %d): TCS34725 %s id=0x%02X", s + 1, ch[s],
                 e == ESP_OK ? "OK" : esp_err_to_name(e), id);
        if (e == ESP_OK && (id == 0x44 || id == 0x4D || id == 0x10)) alive++;
    }
    tca9548_close();
    return alive;
}
