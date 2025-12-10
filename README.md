## 🧠 1. Arquitectura del Firmware (ESP32)

El firmware está basado en **ESP-IDF (FreeRTOS)** y diseñado para ser tolerante a fallos eléctricos derivados de picos de consumo WiFi.

### ⚙️ Funciones Críticas (`main.c`)

#### `sensor_net_task` (Máquina de Estados)
Esta es la tarea principal del sistema. A diferencia de un bucle `while` simple, implementa concurrencia no bloqueante:
1.  **Gestión de Sockets:** Mantiene un socket TCP abierto para recibir comandos de control en tiempo real.
2.  **Lectura No Bloqueante:** Utiliza `fcntl(sock, F_SETFL, O_NONBLOCK)` para revisar si hay comandos entrantes (`recv`) sin detener el flujo de datos de los sensores.
3.  **Edge Computing:** Si se selecciona el modo `RMS`, realiza un muestreo de `N` iteraciones para calcular la energía de la señal antes de transmitir, reduciendo el ancho de banda necesario.

#### `process_incoming_command(char *json_str)`
Parser JSON ligero (basado en `cJSON`) que permite la **reconfiguración en caliente**.
* **Capacidad:** Cambia el sensor activo, el protocolo de transporte (UDP/TCP) o los parámetros de filtro sin reiniciar el microcontrolador.
* **Estructura Global:** Actualiza la `struct app_config_t` que gobierna el comportamiento de la tarea principal.

### 🛡️ Drivers Robustos (`bme688.c`)

El sensor BME688 es sensible a caídas de tensión (Brownouts) provocadas por la transmisión WiFi. El driver implementa:

* **Auto-Recuperación:** Si la transacción I2C falla con error `0x103` (Timeout), el driver detecta el cuelgue, elimina el dispositivo del bus I2C virtual y reinicia la secuencia de inicialización automáticamente.
* **Polling de Estado:** En lugar de `vTaskDelay` fijos, el driver consulta el registro `0x1D` hasta que el bit `NEW_DATA` está activo, garantizando la integridad de los datos.

---

## 🖥️ Arquitectura de la Interfaz (Python/PyQt5)

La aplicación de escritorio (`gui_raspberry_final.py`) utiliza un patrón de diseño **Productor-Consumidor** para evitar el congelamiento de la interfaz (Lag) ante altas tasas de transferencia.

### 🧵 Hilos y Concurrencia (`NetworkWorker`)

* **Multithreading:** Ejecuta dos hilos demonio separados (`tcp_server_thread` y `udp_server_thread`) para escuchar en el puerto 1234 simultáneamente.
* **Persistencia Atómica:** Escribe los datos entrantes directamente a disco (`JSONL` o `CSV`) utilizando un `threading.Lock` para evitar condiciones de carrera y corrupción de archivos.
* **Desacople de UI:** No emite señales Qt por cada paquete recibido (lo cual saturaría el Event Loop). En su lugar, actualiza una variable atómica compartida (`self.latest_data`).

### 📊 Renderizado Optimizado (`MainWindow`)

* **QTimer (30 FPS):** Un temporizador consulta los datos del Worker cada 33ms. Esto mantiene la interfaz fluida independientemente de si llegan 10 o 1000 paquetes por segundo.
* **Buffers Circulares:** Utiliza `collections.deque` con tamaño fijo para almacenar los puntos de la gráfica, optimizando el uso de memoria RAM.
* **Visualización Dinámica:** La función `setup_graphs()` detecta el tipo de dato entrante y reconstruye los widgets de `PyQtGraph` al vuelo (ej: cambia de 3 ejes para Acelerómetro a 4 gráficos independientes para Temperatura/Humedad/Presión/Gas).

---

## 📡 Protocolo de Comunicación (JSON)

El sistema es agnóstico al transporte (funciona igual sobre TCP o UDP) gracias a una carga útil estandarizada en JSON.

### Telemetría (ESP32 -> Raspberry)
{
  "sensor": "BME688",
  "type": "RAW",
  "temp": 25.4,
  "hum": 60.2,
  "press": 101325,
  "gas": 54000
}
Control (Raspberry -> ESP32)
JSON

{
  "cmd": "config",
  "sensor": "BMI270",
  "protocol": "UDP",
  "type": "RMS",
  "window_size": 50
}


## 📂 Estructura del Proyecto

```text
TIC3-Tarea-2/
├── CMakeLists.txt              # Configuración de compilación global
├── Kconfig.projbuild           # Menú de configuración (SSID, IP, Puerto)
├── README.md                   # Documentación del proyecto
├── main/                       # Código fuente del Firmware (ESP32)
│   ├── CMakeLists.txt          # Configuración del componente main
│   ├── main.c                  # Lógica principal, tareas FreeRTOS y JSON
│   ├── bmi270.c / .h           # Driver para el sensor IMU
│   ├── bme688.c / .h           # Driver robusto para sensor ambiental
│   ├── wifi_tcp.c / .h         # Cliente TCP y gestión de conexión WiFi
│   └── wifi_udp.c / .h         # Cliente UDP para streaming
└── python_scripts/             # Software del Servidor (Raspberry Pi)
    └── gui_raspberry_final.py  # Dashboard de control y visualización
```
![ErOJnebXYAAPMqZ](https://github.com/user-attachments/assets/c1ddbe4e-dcbb-4de1-8404-3e317fbd44d1)
<br>
<div align="center">
  <h1> Gracias por la segunda oportunidad </h1>
</div>
<br>
