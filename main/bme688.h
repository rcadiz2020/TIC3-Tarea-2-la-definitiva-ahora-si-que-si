#ifndef BME688_H
#define BME688_H

#include "esp_err.h"
#include "driver/i2c_master.h" // Necesario para el tipo de dato

typedef struct {
    float temperature;
    float pressure;
    float humidity;
    float gas_resistance;
} bme688_data_t;

// Init recibe el bus compartido
esp_err_t bme688_init(i2c_master_bus_handle_t bus_handle);

esp_err_t bme688_read_data(bme688_data_t *data);

#endif // BME688_H