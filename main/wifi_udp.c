#include "wifi_udp.h"
#include "sdkconfig.h"
#include <string.h>
#include <sys/param.h>
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "errno.h"

#define TAG "WIFI_UDP"
// Usamos las variables definidas en tu Kconfig.projbuild [cite: 4, 5]
#define SERVER_IP     CONFIG_SERVER_IP
#define SERVER_PORT   CONFIG_SERVER_PORT

int wifi_udp_create_socket(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Error al crear socket UDP: errno %d", errno);
        return -1;
    }
    ESP_LOGI(TAG, "Socket UDP creado, listo para enviar a %s:%d", SERVER_IP, SERVER_PORT);
    return sock;
}

int wifi_udp_send(int sock, const char *payload, size_t len)
{
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(SERVER_PORT);

    // sendto no requiere conexión previa en UDP
    int err = sendto(sock, payload, len, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    
    if (err < 0) {
        ESP_LOGE(TAG, "Error al enviar UDP: errno %d", errno);
        return -1;
    }
    return err; // Retorna bytes enviados
}

void wifi_udp_close(int sock)
{
    if (sock >= 0) {
        close(sock);
        ESP_LOGI(TAG, "Socket UDP cerrado");
    }
}