#include "tca9548.h"
#include "sensor_bus.h"
#include "rapid4p.h"
#include "freertos/FreeRTOS.h"

static int s_cur = -1;

esp_err_t tca9548_select(uint8_t channel)
{
    i2c_master_dev_handle_t dev = sensor_bus_mux_dev();
    if (!dev) return ESP_ERR_INVALID_STATE;
    if (channel > 7) return ESP_ERR_INVALID_ARG;
    uint8_t mask = (uint8_t)(1u << channel);
    esp_err_t r = i2c_master_transmit(dev, &mask, 1, pdMS_TO_TICKS(50));
    s_cur = (r == ESP_OK) ? (int)channel : -1;
    if (r != ESP_OK) ESP_LOGW(TAG_SENSOR, "TCA9548 select ch%d loi %s", channel, esp_err_to_name(r));
    return r;
}

esp_err_t tca9548_close(void)
{
    i2c_master_dev_handle_t dev = sensor_bus_mux_dev();
    if (!dev) return ESP_ERR_INVALID_STATE;
    uint8_t mask = 0;
    esp_err_t r = i2c_master_transmit(dev, &mask, 1, pdMS_TO_TICKS(50));
    s_cur = -1;
    return r;
}

int tca9548_current(void) { return s_cur; }
