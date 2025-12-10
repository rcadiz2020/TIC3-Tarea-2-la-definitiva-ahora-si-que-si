#ifndef WIFI_TCP_H
#define WIFI_TCP_H

#include "esp_err.h"

/**
 * @brief Inicializa el NVS y el Wi-Fi en modo Estación (STA).
 *
 * Se conectará al SSID y PASS definidos en el menuconfig.
 */
void wifi_tcp_init_sta(void);

/**
 * @brief Intenta conectarse al servidor TCP.
 *
 * Se conectará al SERVER_IP y SERVER_PORT definidos en el menuconfig.
 *
 * @return int El descriptor del socket (>0) si tiene éxito, o -1 si falla.
 */
int wifi_tcp_connect(void);

/**
 * @brief Cierra un socket TCP.
 *
 * @param sock El descriptor del socket a cerrar.
 */
void wifi_tcp_disconnect(int sock);


#endif // WIFI_TCP_H