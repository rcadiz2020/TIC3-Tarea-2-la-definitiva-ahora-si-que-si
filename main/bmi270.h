#ifndef BMI270_H
#define BMI270_H

#include "esp_err.h"
#include "driver/i2c_master.h" // Necesario para el tipo de dato

typedef struct {
    float ax, ay, az; 
    float gx, gy, gz; 
} bmi270_data_t;

esp_err_t bmi270_init(i2c_master_bus_handle_t bus_handle);

esp_err_t bmi270_read_data(bmi270_data_t *data);

#endif // BMI270_H