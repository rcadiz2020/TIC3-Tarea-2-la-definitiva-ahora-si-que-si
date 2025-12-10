#include "bme688.h"
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "BME688"
#define BME_ADDR 0x76 

// Variables globales de calibración
static uint16_t par_t1, par_t2, par_t3;
static uint16_t par_p1, par_p2, par_p3, par_p4, par_p5, par_p6, par_p7, par_p8, par_p9, par_p10;
static uint16_t par_h1, par_h2, par_h3, par_h4, par_h5, par_h6, par_h7;
static int8_t  par_g1, par_g2, par_g3;
static uint8_t res_heat_range, res_heat_val;
static double t_fine = 0.0f;

static i2c_master_dev_handle_t bme_dev_handle = NULL;
static i2c_master_bus_handle_t saved_bus_handle = NULL; 

// --- Funciones I2C ---
static esp_err_t bme_read(uint8_t reg, uint8_t *data, size_t len) {
    if (bme_dev_handle == NULL) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(bme_dev_handle, &reg, 1, data, len, pdMS_TO_TICKS(200));
}
static esp_err_t bme_write(uint8_t reg, uint8_t val) {
    if (bme_dev_handle == NULL) return ESP_ERR_INVALID_STATE;
    uint8_t write_buf[2] = {reg, val};
    return i2c_master_transmit(bme_dev_handle, write_buf, sizeof(write_buf), pdMS_TO_TICKS(200));
}

// --- Calibración ---
static void read_calibration(void) {
    uint8_t buf[40]; uint8_t t_buf[5];
    if(bme_read(0xE9, t_buf, 2)!=ESP_OK) return; 
    par_t1 = (t_buf[1]<<8)|t_buf[0];
    bme_read(0x8A, t_buf, 2); par_t2 = (t_buf[1]<<8)|t_buf[0];
    bme_read(0x8C, t_buf, 1); par_t3 = t_buf[0];
    bme_read(0x8E, buf, 16);
    par_p1 = (buf[1]<<8)|buf[0]; par_p2 = (buf[3]<<8)|buf[2]; par_p3 = buf[4];
    par_p4 = (buf[6]<<8)|buf[5]; par_p5 = (buf[8]<<8)|buf[7]; par_p6 = buf[9];
    par_p7 = buf[10]; par_p8 = (buf[12]<<8)|buf[11]; par_p9 = (buf[14]<<8)|buf[13]; par_p10 = buf[15];
    bme_read(0xE1, buf, 10);
    par_h1 = (uint16_t)(((uint16_t)buf[2] << 4) | (buf[1] & 0x0F));
    par_h2 = (uint16_t)(((uint16_t)buf[0] << 4) | ((buf[1] & 0xF0) >> 4));
    par_h3 = buf[3]; par_h4 = buf[4]; par_h5 = buf[5]; par_h6 = buf[6]; par_h7 = buf[7];
    bme_read(0xED, buf, 1); par_g1 = (int8_t)buf[0];
    bme_read(0xEB, buf, 2); par_g2 = (int16_t)((buf[1]<<8)|buf[0]);
    bme_read(0xEE, buf, 1); par_g3 = (int8_t)buf[0];
    bme_read(0x02, buf, 1); res_heat_range = (buf[0] & 0x30) >> 4;
    bme_read(0x00, buf, 1); res_heat_val = (int8_t)buf[0];
}

// --- Matemáticas ---
static float calc_temp(uint32_t adc_t) {
    double var1 = (((double)adc_t / 16384.0) - ((double)par_t1 / 1024.0)) * (double)par_t2;
    double var2 = ((((double)adc_t / 131072.0) - ((double)par_t1 / 8192.0)) * (((double)adc_t / 131072.0) - ((double)par_t1 / 8192.0))) * ((double)par_t3 * 16.0);
    t_fine = var1 + var2; return (float)(t_fine / 5120.0);
}
static float calc_press(uint32_t adc_p) {
    double var1 = (t_fine / 2.0) - 64000.0;
    double var2 = var1 * var1 * ((double)par_p6 / 131072.0) + (var1 * (double)par_p5 * 2.0);
    var2 = (var2 / 4.0) + ((double)par_p4 * 65536.0);
    var1 = (((double)par_p3 * var1 * var1) / 16384.0 + ((double)par_p2 * var1)) / 524288.0;
    var1 = (1.0 + var1 / 32768.0) * (double)par_p1;
    if (var1 == 0.0) return 0.0f;
    double p = 1048576.0 - (double)adc_p;
    p = ((p - (var2 / 4096.0)) * 6250.0) / var1;
    var1 = ((double)par_p9 * p * p) / 2147483648.0;
    var2 = p * ((double)par_p8) / 32768.0;
    double var3 = (p / 256.0) * (p / 256.0) * (p / 256.0) * (par_p10 / 131072.0);
    return (float)(p + (var1 + var2 + var3 + (double)par_p7 * 128.0) / 16.0);
}
static float calc_hum(uint16_t adc_h) {
    double temp_comp = t_fine / 5120.0;
    double var1 = (double)adc_h - ((double)par_h1 * 16.0 + (((double)par_h3 / 2.0) * temp_comp));
    double var2 = var1 * (((double)par_h2 / 262144.0) * (1.0 + (((double)par_h4 / 16384.0) * temp_comp) + (((double)par_h5 / 1048576.0) * temp_comp * temp_comp)));
    double var3 = (double)par_h6 / 16384.0; double var4 = (double)par_h7 / 2097152.0;
    return (float)(var2 + ((var3 + (var4 * temp_comp)) * var2 * var2));
}

// Cálculo simplificado de resistencia de gas (Ohms)
static float calc_gas(uint16_t adc_g) { 
    if(adc_g == 0) return 0.0; 
    // Fórmula aproximada básica para visualizar cambios
    return 1.0 / (double)adc_g * 1000000.0; 
}

// --- INIT (Publica) ---
esp_err_t bme688_init(i2c_master_bus_handle_t bus_handle) {
    saved_bus_handle = bus_handle; 

    if (bme_dev_handle != NULL) {
        i2c_master_bus_rm_device(bme_dev_handle);
        bme_dev_handle = NULL;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BME_ADDR,
        .scl_speed_hz = 100000, 
    };
    
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &bme_dev_handle));

    uint8_t id = 0;
    if (bme_read(0xD0, &id, 1) != ESP_OK) return ESP_FAIL;
    
    if(id != 0x61) {
        ESP_LOGE(TAG, "ID Incorrecto: 0x%02x", id);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "BME688 Inicializado OK.");
    
    bme_write(0xE0, 0xB6); // Reset
    vTaskDelay(pdMS_TO_TICKS(100));
    read_calibration();
    return ESP_OK;
}

// --- LECTURA CON GAS ACTIVADO ---
esp_err_t bme688_read_data(bme688_data_t *data) {
    esp_err_t ret;

    // 1. CONFIGURACIÓN (Incluyendo Gas)
    bme_write(0x72, 0x01); // Hum x1
    bme_write(0x74, 0x54); // Temp x2, Press x16, Sleep
    
    // Configuración del Gas:
    bme_write(0x64, 0x59); // Gas Wait 0 (~100ms) -> 0x59 es un valor típico
    bme_write(0x5A, 0xAC); // Res Heat 0 (~300°C) -> Valor fijo para prueba
    bme_write(0x71, 0x20); // Ctrl Gas 1 -> 0x20 (RUN_GAS = 1)
    
    // 2. DISPARAR MODO FORZADO
    ret = bme_write(0x74, 0x25); 
    
    // Auto-recuperación si falla el disparo
    if (ret != ESP_OK) {
        if (saved_bus_handle != NULL) bme688_init(saved_bus_handle);
        return ret; 
    }

    // 3. POLLING (Esperar New Data)
    // Aumentamos timeout porque medir gas toma ~100ms extra
    uint8_t status = 0;
    int timeout = 50; 
    bool data_ready = false;
    vTaskDelay(pdMS_TO_TICKS(120)); // Espera base más larga por el gas

    while (timeout > 0) {
        bme_read(0x1D, &status, 1);
        if (status & 0x80) { // New Data
            data_ready = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout--;
    }

    if (!data_ready) return ESP_ERR_TIMEOUT;

    // 4. LEER
    uint8_t raw[15];
    ret = bme_read(0x1F, raw, 15);
    if (ret != ESP_OK) return ret;

    uint32_t adc_p = (uint32_t)(((uint32_t)raw[0] << 12) | ((uint32_t)raw[1] << 4) | ((uint32_t)raw[2] >> 4));
    uint32_t adc_t = (uint32_t)(((uint32_t)raw[3] << 12) | ((uint32_t)raw[4] << 4) | ((uint32_t)raw[5] >> 4));
    uint16_t adc_h = (uint16_t)(((uint16_t)raw[6] << 8) | (uint16_t)raw[7]);
    
    // Lectura del ADC de Gas (bytes 13 y 14)
    // raw[13] son los bits 9:2, raw[14] bits 1:0 y rango
    uint16_t adc_g = (uint16_t)((((uint16_t)raw[13]) << 2) | (((uint16_t)raw[14]) >> 6));
    
    if (adc_t == 0x80000) return ESP_FAIL;

    data->temperature = calc_temp(adc_t);
    data->pressure = calc_press(adc_p);
    data->humidity = calc_hum(adc_h);
    
    // Calcular resistencia
    data->gas_resistance = calc_gas(adc_g);

    return ESP_OK;
}