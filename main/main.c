#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include <fcntl.h>      // <-- CORRECCIÓN: Usar librería estándar en lugar de lwip/fcntl.h
#include "driver/i2c_master.h"
#include "cJSON.h"      
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>       
#include "wifi_tcp.h"
#include "wifi_udp.h" 
#include "sdkconfig.h"

// Incluimos ambos drivers
#include "bmi270.h"
#include "bme688.h"

#define I2C_SCL_IO GPIO_NUM_47
#define I2C_SDA_IO GPIO_NUM_48

static const char *TAG = "APP_MAIN";
#define SEND_BUFFER_SIZE 512 
#define RX_BUFFER_SIZE 256
#define MAX_CONNECTION_RETRIES 10

// --- ESTRUCTURA DE ESTADO DEL SISTEMA ---
typedef enum { SENSOR_BMI270, SENSOR_BME688 } sensor_type_t;
typedef enum { PROTOCOL_TCP, PROTOCOL_UDP } protocol_type_t;
typedef enum { TYPE_RAW, TYPE_RMS, TYPE_FFT, TYPE_PEAK } data_type_t;

typedef struct {
    sensor_type_t current_sensor;
    protocol_type_t current_protocol;
    data_type_t current_datatype;
    int window_size; // "N" para RMS/FFT
    int threshold;   // Umbral para picos
} app_config_t;

// Configuración inicial por defecto
static app_config_t global_config = {
    .current_sensor = SENSOR_BMI270,
    .current_protocol = PROTOCOL_UDP, // UDP por defecto para streaming rápido
    .current_datatype = TYPE_RAW,
    .window_size = 50,
    .threshold = 1000
};

static i2c_master_bus_handle_t main_bus_handle;

// --- INICIALIZACIÓN HARDWARE ---
static void init_i2c_master_bus(void) {
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL_IO,
        .sda_io_num = I2C_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &main_bus_handle));
    ESP_LOGI(TAG, "Bus I2C Maestro iniciado.");
}

// --- LÓGICA DE CONTROL (RECEPCIÓN JSON) ---
void process_incoming_command(char *json_str) {
    // Ejemplo recibido: {"cmd":"config", "sensor":"BME688", "protocol":"TCP", "window": 100}
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        ESP_LOGW(TAG, "JSON inválido recibido");
        return;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if (cJSON_IsString(cmd) && strcmp(cmd->valuestring, "config") == 0) {
        
        // 1. Cambiar Sensor
        cJSON *sensor = cJSON_GetObjectItem(root, "sensor");
        if (cJSON_IsString(sensor)) {
            if (strcmp(sensor->valuestring, "BMI270") == 0) global_config.current_sensor = SENSOR_BMI270;
            else if (strcmp(sensor->valuestring, "BME688") == 0) global_config.current_sensor = SENSOR_BME688;
        }

        // 2. Cambiar Protocolo
        cJSON *prot = cJSON_GetObjectItem(root, "protocol");
        if (cJSON_IsString(prot)) {
            if (strcmp(prot->valuestring, "TCP") == 0) global_config.current_protocol = PROTOCOL_TCP;
            else if (strcmp(prot->valuestring, "UDP") == 0) global_config.current_protocol = PROTOCOL_UDP;
        }

        // 3. Tipo de Procesamiento
        cJSON *type = cJSON_GetObjectItem(root, "type");
        if (cJSON_IsString(type)) {
            if (strcmp(type->valuestring, "RAW") == 0) global_config.current_datatype = TYPE_RAW;
            else if (strcmp(type->valuestring, "RMS") == 0) global_config.current_datatype = TYPE_RMS;
            else if (strcmp(type->valuestring, "FFT") == 0) global_config.current_datatype = TYPE_FFT;
            else if (strcmp(type->valuestring, "PEAK") == 0) global_config.current_datatype = TYPE_PEAK;
        }

        // 4. Parámetros Numéricos
        cJSON *win = cJSON_GetObjectItem(root, "window_size");
        if (cJSON_IsNumber(win)) global_config.window_size = win->valueint;
        
        cJSON *th = cJSON_GetObjectItem(root, "threshold");
        if (cJSON_IsNumber(th)) global_config.threshold = th->valueint;

        ESP_LOGW(TAG, "CONFIG ACTUALIZADA: Sensor=%d, Proto=%d, Tipo=%d", 
                 global_config.current_sensor, global_config.current_protocol, global_config.current_datatype);
    }
    cJSON_Delete(root);
}

// --- TAREA PRINCIPAL ---
static void sensor_net_task(void *arg) {
    char send_buf[SEND_BUFFER_SIZE];
    char rx_buf[RX_BUFFER_SIZE];
    
    // Buffers para datos de sensores
    bmi270_data_t bmi_data;
    bme688_data_t bme_data = {0};

    int tcp_sock = -1;
    int udp_sock = -1;

    while (1) {
        // --- GESTIÓN DE CONEXIÓN ---
        // 1. Socket TCP (Canal de Control + Datos opcional)
        if (tcp_sock < 0) {
            ESP_LOGI(TAG, "Conectando al servidor TCP (Control)...");
            tcp_sock = wifi_tcp_connect();
            
            if (tcp_sock >= 0) {
                // Configurar socket como NO BLOQUEANTE para poder leer sin detener el loop
                int flags = fcntl(tcp_sock, F_GETFL, 0);
                fcntl(tcp_sock, F_SETFL, flags | O_NONBLOCK);
                ESP_LOGI(TAG, "Canal de control TCP listo (No bloqueante).");
            } else {
                ESP_LOGE(TAG, "Fallo al conectar TCP. Reintentando en 3s...");
                vTaskDelay(pdMS_TO_TICKS(3000));
                continue;
            }
        }

        // 2. Socket UDP (Siempre listo para streaming rápido)
        if (udp_sock < 0) {
            udp_sock = wifi_udp_create_socket();
        }

        // --- BUCLE DE PROCESAMIENTO ---
        while (1) {
            // A. VERIFICAR COMANDOS (Recv No Bloqueante)
            int len = recv(tcp_sock, rx_buf, sizeof(rx_buf) - 1, 0);
            if (len > 0) {
                rx_buf[len] = 0; // Null-terminate string
                ESP_LOGI(TAG, "Comando recibido: %s", rx_buf);
                process_incoming_command(rx_buf);
            } else if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                // Error real de conexión (no es solo que no haya datos)
                ESP_LOGE(TAG, "Conexión TCP perdida (errno %d). Reconectando...", errno);
                close(tcp_sock);
                tcp_sock = -1;
                break; // Salir del while interno para reconectar
            }

            // B. LEER SENSOR ACTIVO
            esp_err_t ret = ESP_FAIL;
            int json_len = 0;

            if (global_config.current_sensor == SENSOR_BMI270) {
                
                // --- LÓGICA RMS REAL ---
                if (global_config.current_datatype == TYPE_RMS) {
                    float sum_x = 0, sum_y = 0, sum_z = 0;
                    int N = global_config.window_size; 
                    if (N <= 0) N = 10; // Protección
                    int samples_taken = 0;

                    for (int i = 0; i < N; i++) {
                        ret = bmi270_read_data(&bmi_data);
                        if (ret == ESP_OK) {
                            sum_x += (bmi_data.ax * bmi_data.ax);
                            sum_y += (bmi_data.ay * bmi_data.ay);
                            sum_z += (bmi_data.az * bmi_data.az);
                            samples_taken++;
                        }
                        vTaskDelay(pdMS_TO_TICKS(10)); 
                    }

                    if (samples_taken > 0) {
                        float rms_x = sqrt(sum_x / samples_taken);
                        float rms_y = sqrt(sum_y / samples_taken);
                        float rms_z = sqrt(sum_z / samples_taken);
                        float rms_total = sqrt(rms_x*rms_x + rms_y*rms_y + rms_z*rms_z);

                        json_len = snprintf(send_buf, SEND_BUFFER_SIZE,
                            "{\"sensor\":\"BMI270\", \"type\":\"RMS\", \"rms\":%.3f, \"N\":%d}\n",
                            rms_total, samples_taken);
                        ret = ESP_OK; 
                    } else {
                        ret = ESP_FAIL;
                    }

                } else {
                    // --- MODO RAW ---
                    ret = bmi270_read_data(&bmi_data);
                    if (ret == ESP_OK) {
                        json_len = snprintf(send_buf, SEND_BUFFER_SIZE,
                            "{\"sensor\":\"BMI270\", \"type\":\"RAW\", \"ax\":%.2f, \"ay\":%.2f, \"az\":%.2f, \"gx\":%.2f, \"gy\":%.2f, \"gz\":%.2f}\n",
                            bmi_data.ax, bmi_data.ay, bmi_data.az, bmi_data.gx, bmi_data.gy, bmi_data.gz);
                    }
                }
            } 
            else if (global_config.current_sensor == SENSOR_BME688) {
                ret = bme688_read_data(&bme_data);
                
                if (ret == ESP_OK) {
                    json_len = snprintf(send_buf, SEND_BUFFER_SIZE,
                        "{\"sensor\":\"BME688\", \"type\":\"RAW\", \"temp\":%.2f, \"press\":%.2f, \"hum\":%.2f, \"gas\":%.2f}\n",
                        bme_data.temperature, bme_data.pressure, bme_data.humidity, bme_data.gas_resistance);
                }
            }

            // C. ENVIAR DATOS
            if (ret == ESP_OK && json_len > 0) {
                
                if (global_config.current_protocol == PROTOCOL_UDP) {
                    wifi_udp_send(udp_sock, send_buf, json_len);
                } else {
                    // TCP
                    if (tcp_sock >= 0) {
                        int err = send(tcp_sock, send_buf, json_len, 0);
                        if (err < 0) {
                            ESP_LOGE(TAG, "Fallo al enviar TCP");
                            close(tcp_sock);
                            tcp_sock = -1;
                            break; 
                        }
                    }
                }
                
                // Tasa de envío (10Hz para RAW, RMS ya tiene su propio delay interno)
                if (global_config.current_datatype == TYPE_RAW) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            } else {
                // Si falla la lectura, esperamos un poco
                ESP_LOGW(TAG, "Error lectura sensor (0x%x)", ret);
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
        
        // Limpieza si se sale del bucle principal
        if (udp_sock >= 0) { close(udp_sock); udp_sock = -1; }
        if (tcp_sock >= 0) { close(tcp_sock); tcp_sock = -1; }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== SISTEMA EDGE COMPUTING INICIADO ===");

    // 1. Inicializar Red
    wifi_tcp_init_sta();

    // 2. Inicializar Bus I2C (Compartido)
    init_i2c_master_bus();

    // 3. Inicializar AMBOS Sensores
    ESP_LOGI(TAG, "Inicializando BMI270...");
    if (bmi270_init(main_bus_handle) == ESP_OK) {
        ESP_LOGI(TAG, "BMI270 -> OK");
    } else {
        ESP_LOGE(TAG, "BMI270 -> FALLO");
    }

    ESP_LOGI(TAG, "Inicializando BME688...");
    // Usamos el init que retorna error sin crashear
    if (bme688_init(main_bus_handle) == ESP_OK) {
        ESP_LOGI(TAG, "BME688 -> OK");
    } else {
        ESP_LOGE(TAG, "BME688 -> FALLO (Verifica dirección I2C)");
    }

    // 4. Iniciar Tarea Principal
    xTaskCreate(sensor_net_task, "sensor_net_task", 8192, NULL, 5, NULL);
}