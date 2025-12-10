import socket
import threading
import json
import time
from datetime import datetime

# --- CONFIGURACIÓN ---
HOST = '0.0.0.0'  
PORT = 1234       
FILENAME = 'sensor_data.jsonl' 

# --- FUNCIÓN DE GUARDADO ---

file_lock = threading.Lock()

def save_to_file(data_dict, protocol, source_ip):


    data_dict['timestamp_server'] = datetime.now().isoformat()
    data_dict['protocol'] = protocol
    data_dict['source_ip'] = source_ip

    json_str = json.dumps(data_dict)

    # Sección crítica: Solo un hilo escribe a la vez
    with file_lock:
        with open(FILENAME, 'a') as f:
            f.write(json_str + "\n")
    
    # Imprimir en consola para ver en vivo
    print(f"[{protocol}] De {source_ip}: {json_str}")

# --- SERVIDOR TCP ---
def tcp_server_thread():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind((HOST, PORT))
            s.listen()
            print(f"✅ Hilo TCP escuchando en {HOST}:{PORT}")
        except Exception as e:
            print(f"❌ Error iniciando TCP: {e}")
            return

        while True:
            try:
                conn, addr = s.accept()
                with conn:
                    f = conn.makefile('r')
                    while True:
                        line = f.readline()
                        if not line:
                            break # Fin de conexión
                        
                        try:
                            # Limpiamos espacios y decodificamos
                            data = json.loads(line.strip())
                            save_to_file(data, "TCP", addr[0])
                        except json.JSONDecodeError:
                            print(f"⚠️ [TCP] Error decodificando JSON: {line.strip()}")
            except Exception as e:
                print(f"⚠️ [TCP] Error en conexión: {e}")

# --- SERVIDOR UDP ---
def udp_server_thread():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind((HOST, PORT))
            print(f"✅ Hilo UDP escuchando en {HOST}:{PORT}")
        except Exception as e:
            print(f"❌ Error iniciando UDP: {e}")
            return

        while True:
            try:
                # Buffer de 1024 bytes es suficiente para tus mensajes JSON
                data_bytes, addr = s.recvfrom(1024)
                
                if data_bytes:
                    try:
                        line = data_bytes.decode('utf-8').strip()
                        data = json.loads(line)
                        save_to_file(data, "UDP", addr[0])
                    except json.JSONDecodeError:
                        print(f"⚠️ [UDP] Error decodificando JSON: {data_bytes}")
                        
            except Exception as e:
                print(f"⚠️ [UDP] Error recibiendo: {e}")

# --- MAIN ---
if __name__ == "__main__":
    print(f"🚀 Iniciando Servidor Unificado (TCP + UDP) en puerto {PORT}...")
    print(f"📂 Los datos se guardarán en: {FILENAME}")
    print("Presiona Ctrl+C para detener.")

    # Creamos los hilos
    thread_tcp = threading.Thread(target=tcp_server_thread, daemon=True)
    thread_udp = threading.Thread(target=udp_server_thread, daemon=True)

    # Iniciamos los hilos
    thread_tcp.start()
    thread_udp.start()

    # Mantenemos el programa principal vivo
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n🛑 Servidor detenido.")