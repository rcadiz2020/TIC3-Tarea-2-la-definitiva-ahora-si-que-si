#ifndef WIFI_UDP_H
#define WIFI_UDP_H

#include <stddef.h>

// Crea el socket UDP usando la IP/Puerto definidos en Kconfig
int wifi_udp_create_socket(void);

// Envía el payload
int wifi_udp_send(int sock, const char *payload, size_t len);

// Cierra el socket
void wifi_udp_close(int sock);

#endif // WIFI_UDP_H