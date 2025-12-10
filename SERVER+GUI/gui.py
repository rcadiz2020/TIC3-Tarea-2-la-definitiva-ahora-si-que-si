import sys
import socket
import threading
import json
import time
import csv
import os
from collections import deque
from datetime import datetime
from PyQt5.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                             QHBoxLayout, QLabel, QComboBox, QSpinBox, 
                             QPushButton, QGroupBox, QRadioButton, QTextEdit, 
                             QFrame, QScrollArea, QButtonGroup, QSizePolicy)
from PyQt5.QtCore import pyqtSignal, QObject, pyqtSlot, Qt, QTimer
from PyQt5.QtGui import QFont, QColor, QPalette
import pyqtgraph as pg

# --- CONFIGURACIÓN ---
HOST = '0.0.0.0'
PORT = 1234
FILE_JSON = 'sensor_data.jsonl'
FILE_CSV = 'sensor_data.csv'

# Lock para escritura de archivos y acceso a datos compartidos
data_lock = threading.Lock()

# --- WORKER DE RED OPTIMIZADO ---
class WorkerSignals(QObject):
    log_message = pyqtSignal(str)
    connection_status = pyqtSignal(str)

class NetworkWorker(QObject):
    def __init__(self):
        super().__init__()
        self.signals = WorkerSignals()
        self.running = True
        self.tcp_client_socket = None
        self.save_format = "JSON"
        
        # --- DATOS COMPARTIDOS (NO SEÑALES) ---
        # Guardamos el último dato recibido para que la UI lo lea a su ritmo
        self.latest_data = None
        self.new_data_available = False

    def set_save_format(self, fmt):
        self.save_format = fmt

    def process_data(self, data_dict, protocol, source_ip):
        """Procesa, guarda y actualiza el estado compartido"""
        try:
            # 1. Guardar en Disco (Persistencia)
            timestamp = datetime.now().isoformat()
            data_dict['timestamp'] = timestamp
            data_dict['protocol'] = protocol
            
            # Usamos un lock breve para escribir archivo
            with data_lock:
                if self.save_format == "JSON":
                    with open(FILE_JSON, 'a') as f:
                        f.write(json.dumps(data_dict) + "\n")
                elif self.save_format == "CSV":
                    file_exists = os.path.isfile(FILE_CSV)
                    with open(FILE_CSV, 'a', newline='') as f:
                        keys = ['timestamp', 'protocol', 'sensor', 'type'] 
                        for k in data_dict.keys():
                            if k not in keys: keys.append(k)
                        writer = csv.DictWriter(f, fieldnames=keys)
                        if not file_exists: writer.writeheader()
                        writer.writerow(data_dict)
            
            # 2. Actualizar dato compartido para la UI (Sin Bloqueos Fuertes)
            self.latest_data = data_dict
            self.new_data_available = True

        except Exception as e:
            pass # Evitar spam en log si falla escritura rápida

    def start_servers(self):
        threading.Thread(target=self.tcp_server_thread, daemon=True).start()
        threading.Thread(target=self.udp_server_thread, daemon=True).start()
        self.signals.log_message.emit(f"🚀 Servidores iniciados (Puerto {PORT})")

    def tcp_server_thread(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                s.bind((HOST, PORT))
                s.listen()
            except Exception as e:
                self.signals.log_message.emit(f"❌ Error TCP: {e}")
                return

            while self.running:
                try:
                    conn, addr = s.accept()
                    self.tcp_client_socket = conn
                    self.signals.connection_status.emit(f"TCP: {addr[0]}")
                    self.signals.log_message.emit(f"🔵 Conectado: {addr[0]}")
                    
                    with conn:
                        f = conn.makefile('r')
                        while self.running:
                            line = f.readline()
                            if not line: break
                            try:
                                data = json.loads(line.strip())
                                self.process_data(data, "TCP", addr[0])
                            except json.JSONDecodeError:
                                pass
                    self.tcp_client_socket = None
                    self.signals.connection_status.emit("Desconectado")
                except Exception:
                    pass

    def udp_server_thread(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                s.bind((HOST, PORT))
            except Exception:
                return

            while self.running:
                try:
                    data_bytes, addr = s.recvfrom(2048)
                    if data_bytes:
                        try:
                            line = data_bytes.decode('utf-8', errors='replace').strip()
                            data = json.loads(line)
                            self.process_data(data, "UDP", addr[0])
                        except json.JSONDecodeError:
                            pass
                except Exception:
                    pass

    def send_config(self, config):
        if self.tcp_client_socket:
            try:
                msg = json.dumps(config) + "\n"
                self.tcp_client_socket.sendall(msg.encode('utf-8'))
                self.signals.log_message.emit(f"📤 Config enviada: {config['sensor']}")
            except Exception as e:
                self.signals.log_message.emit(f"❌ Error enviando: {e}")
        else:
            self.signals.log_message.emit("⚠️ No hay conexión TCP para control.")

    def stop(self):
        self.running = False

# --- GUI PRINCIPAL ---
class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Monitor IoT - Tarea 2 (Optimizado)")
        self.resize(1280, 800)
        
        # Estilo global para arreglar visibilidad
        self.setStyleSheet("""
            QMainWindow { background-color: #f0f0f0; }
            QLabel { font-size: 14px; }
            QGroupBox { 
                font-weight: bold; 
                border: 1px solid #aaa; 
                border-radius: 5px; 
                margin-top: 10px; 
                background-color: #ffffff;
            }
            QGroupBox::title { 
                subcontrol-origin: margin; 
                left: 10px; 
                padding: 0 3px; 
            }
            QPushButton {
                background-color: #007bff;
                color: white;
                border-radius: 5px;
                padding: 10px;
                font-weight: bold;
                font-size: 14px;
            }
            QPushButton:hover { background-color: #0056b3; }
            QPushButton:pressed { background-color: #004085; }
        """)

        # Worker
        self.worker = NetworkWorker()
        self.worker.signals.log_message.connect(self.log)
        self.worker.signals.connection_status.connect(self.update_status)
        self.worker.start_servers()

        # Buffers
        self.maxlen = 100
        self.data_buffers = {}
        self.plots = {}
        self.curves = {}
        self.current_layout_mode = None 

        self.init_ui()

        # --- TIMER DE ACTUALIZACIÓN (LA CLAVE DEL RENDIMIENTO) ---
        # En lugar de actualizar por señal, actualizamos a 30 FPS fijos
        self.update_timer = QTimer()
        self.update_timer.timeout.connect(self.update_gui_from_data)
        self.update_timer.start(33) # 33ms ~= 30 FPS

    def init_ui(self):
        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        layout = QHBoxLayout(main_widget)

        # === PANEL IZQUIERDO (CONTROL) ===
        control_panel = QVBoxLayout()
        control_panel.setSpacing(15)
        
        # Título
        lbl_title = QLabel("PANEL DE CONTROL")
        lbl_title.setAlignment(Qt.AlignCenter)
        lbl_title.setFont(QFont("Arial", 16, QFont.Bold))
        control_panel.addWidget(lbl_title)

        # 1. Configuración Sensor
        gb_conf = QGroupBox("1. Configuración de Sensor")
        v_conf = QVBoxLayout()
        
        v_conf.addWidget(QLabel("Seleccionar Sensor:"))
        self.cb_sensor = QComboBox()
        self.cb_sensor.addItems(["BMI270", "BME688"])
        self.cb_sensor.setMinimumHeight(30)
        v_conf.addWidget(self.cb_sensor)

        v_conf.addWidget(QLabel("Protocolo:"))
        self.bg_proto = QButtonGroup()
        self.rb_udp = QRadioButton("UDP (Rápido)"); self.rb_udp.setChecked(True)
        self.rb_tcp = QRadioButton("TCP (Seguro)")
        self.bg_proto.addButton(self.rb_udp); self.bg_proto.addButton(self.rb_tcp)
        h_proto = QHBoxLayout()
        h_proto.addWidget(self.rb_udp); h_proto.addWidget(self.rb_tcp)
        v_conf.addLayout(h_proto)

        v_conf.addWidget(QLabel("Procesamiento Edge:"))
        self.cb_type = QComboBox()
        self.cb_type.addItems(["RAW", "RMS", "FFT", "PEAK"])
        self.cb_type.setMinimumHeight(30)
        v_conf.addWidget(self.cb_type)

        v_conf.addWidget(QLabel("Ventana (N):"))
        self.sb_win = QSpinBox(); self.sb_win.setRange(10, 1024); self.sb_win.setValue(50)
        self.sb_win.setMinimumHeight(30)
        v_conf.addWidget(self.sb_win)

        gb_conf.setLayout(v_conf)
        control_panel.addWidget(gb_conf)

        # Botón Aplicar Grande
        self.btn_send = QPushButton("APLICAR CAMBIOS")
        self.btn_send.setCursor(Qt.PointingHandCursor)
        self.btn_send.clicked.connect(self.send_config)
        control_panel.addWidget(self.btn_send)

        # 2. Guardado
        gb_save = QGroupBox("2. Guardado de Datos")
        v_save = QVBoxLayout()
        self.bg_save = QButtonGroup()
        self.rb_json = QRadioButton("JSON"); self.rb_json.setChecked(True)
        self.rb_csv = QRadioButton("CSV")
        self.bg_save.addButton(self.rb_json); self.bg_save.addButton(self.rb_csv)
        self.rb_json.toggled.connect(self.update_save_format)
        h_save = QHBoxLayout()
        h_save.addWidget(self.rb_json); h_save.addWidget(self.rb_csv)
        v_save.addLayout(h_save)
        gb_save.setLayout(v_save)
        control_panel.addWidget(gb_save)

        # Estado Conexión
        self.lbl_status = QLabel("Esperando conexión...")
        self.lbl_status.setAlignment(Qt.AlignCenter)
        self.lbl_status.setStyleSheet("background-color: #607D8B; color: white; padding: 8px; border-radius: 4px; font-weight: bold;")
        control_panel.addWidget(self.lbl_status)

        control_panel.addStretch()
        
        # Envolver panel izquierdo en un widget para controlar ancho
        left_widget = QWidget()
        left_widget.setLayout(control_panel)
        left_widget.setFixedWidth(300) # Ancho fijo para que no se aplaste
        layout.addWidget(left_widget)

        # === PANEL CENTRAL (GRÁFICOS) ===
        right_layout = QVBoxLayout()
        
        # Área de gráficos con scroll
        self.graph_container = QWidget()
        self.graph_layout = QVBoxLayout(self.graph_container)
        self.graph_layout.setContentsMargins(0,0,0,0)
        self.graph_layout.setSpacing(10)
        
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(self.graph_container)
        right_layout.addWidget(scroll, 3)

        # Panel de Alertas
        self.frm_alert = QFrame()
        self.frm_alert.setStyleSheet("background-color: #E0E0E0; border: 2px solid #999; border-radius: 8px;")
        self.frm_alert.setMinimumHeight(80)
        v_alert = QVBoxLayout(self.frm_alert)
        self.lbl_alert_title = QLabel("SISTEMA OK")
        self.lbl_alert_title.setFont(QFont("Arial", 14, QFont.Bold))
        self.lbl_alert_title.setAlignment(Qt.AlignCenter)
        self.lbl_alert_msg = QLabel("--")
        self.lbl_alert_msg.setAlignment(Qt.AlignCenter)
        v_alert.addWidget(self.lbl_alert_title)
        v_alert.addWidget(self.lbl_alert_msg)
        right_layout.addWidget(self.frm_alert, 0)

        # Consola Log
        self.txt_log = QTextEdit()
        self.txt_log.setReadOnly(True)
        self.txt_log.setMaximumHeight(100)
        self.txt_log.setStyleSheet("background-color: #222; color: #00FF00; font-family: monospace;")
        right_layout.addWidget(self.txt_log, 0)

        layout.addLayout(right_layout)

    # --- LÓGICA DE ACTUALIZACIÓN (30 FPS) ---
    def update_gui_from_data(self):
        # 1. Verificar si hay dato nuevo
        if not self.worker.new_data_available:
            return
        
        # 2. Copiar dato y limpiar flag (consumir)
        data = self.worker.latest_data
        self.worker.new_data_available = False # Ya lo consumimos visualmente
        
        if not data: return

        # 3. Detectar modo
        mode = "UNKNOWN"
        if "ax" in data: mode = "BMI270"
        elif "temp" in data: mode = "BME688"
        elif "rms_total" in data: mode = "RMS"
        
        # 4. Configurar gráficas si cambió el modo
        self.setup_graphs(mode)

        # 5. Actualizar Buffers
        for k, buffer in self.data_buffers.items():
            if k in data:
                buffer.append(data[k])
                # Actualizar curva solo si existe
                if k in self.curves:
                    self.curves[k].setData(buffer)

        # 6. Alertas
        if mode == "BME688":
            self.check_risk(data.get('temp', 0), data.get('hum', 0), data.get('gas', 0), data.get('press', 0))
        elif mode == "BMI270":
            self.set_alert_normal("Monitoreo Inercial Activo")

    def setup_graphs(self, mode):
        if mode == self.current_layout_mode: return
        
        # Limpiar
        for i in reversed(range(self.graph_layout.count())): 
            w = self.graph_layout.itemAt(i).widget()
            if w: w.setParent(None)
        
        self.plots = {}
        self.curves = {}
        self.data_buffers = {}

        # Configuración de gráficos
        configs = []
        if mode == "BMI270":
            configs = [
                ("Aceleración", "m/s²", ["ax", "ay", "az"], ["#FF5252", "#4CAF50", "#448AFF"]),
                ("Giroscopio", "rad/s", ["gx", "gy", "gz"], ["#E040FB", "#FFEB3B", "#00BCD4"])
            ]
        elif mode == "BME688":
            # 4 Gráficos separados para consistencia visual
            configs = [
                ("Temperatura", "°C", ["temp"], ["#FF5252"]),
                ("Humedad Relativa", "%", ["hum"], ["#448AFF"]),
                ("Presión Atmosférica", "Pa", ["press"], ["#4CAF50"]),
                ("Resistencia Gas", "Ω", ["gas"], ["#607D8B"])
            ]
        elif mode == "RMS":
            configs = [("Energía RMS", "Magnitud", ["rms_total"], ["#FFFFFF"])]

        # Crear Widgets
        for title, unit, keys, colors in configs:
            pw = pg.PlotWidget(title=title)
            pw.setBackground('w')
            pw.showGrid(x=True, y=True)
            pw.setLabel('left', unit)
            pw.addLegend()
            pw.setMinimumHeight(200) # Altura mínima para que se vea bien
            self.graph_layout.addWidget(pw)
            
            for k, col in zip(keys, colors):
                self.data_buffers[k] = deque([0]*self.maxlen, maxlen=self.maxlen)
                pen = pg.mkPen(color=col, width=2)
                self.curves[k] = pw.plot(pen=pen, name=k.upper())

        self.current_layout_mode = mode

    # --- ALERTAS ---
    def check_risk(self, temp, hum, gas, press):
        if temp == 0 and hum == 0: return

        risks = []
        if temp > 30: risks.append(f"CALOR: {temp:.1f}°C")
        if temp < 10: risks.append(f"FRÍO: {temp:.1f}°C")
        if hum > 70: risks.append(f"HUMEDAD: {hum:.1f}%")
        if hum < 20: risks.append(f"SECO: {hum:.1f}%")
        if 0 < gas < 5000: risks.append("AIRE SUCIO")

        if risks:
            self.set_alert_danger(" | ".join(risks))
        else:
            self.set_alert_normal(f"T:{temp:.1f}°C H:{hum:.0f}% P:{press:.0f}Pa G:{gas:.0f}Ω")

    def set_alert_danger(self, msg):
        self.frm_alert.setStyleSheet("background-color: #FFCDD2; border: 2px solid #F44336; border-radius: 8px;")
        self.lbl_alert_title.setText("⚠️ ALERTA")
        self.lbl_alert_title.setStyleSheet("color: #D32F2F;")
        self.lbl_alert_msg.setText(msg)

    def set_alert_normal(self, msg):
        self.frm_alert.setStyleSheet("background-color: #C8E6C9; border: 2px solid #4CAF50; border-radius: 8px;")
        self.lbl_alert_title.setText("✅ NOMINAL")
        self.lbl_alert_title.setStyleSheet("color: #2E7D32;")
        self.lbl_alert_msg.setText(msg)

    # --- CONTROL ---
    def update_save_format(self):
        fmt = "JSON" if self.rb_json.isChecked() else "CSV"
        self.worker.set_save_format(fmt)
        self.log(f"Formato guardado: {fmt}")

    def send_config(self):
        cfg = {
            "cmd": "config",
            "sensor": self.cb_sensor.currentText(),
            "protocol": "UDP" if self.rb_udp.isChecked() else "TCP",
            "type": self.cb_type.currentText(),
            "window_size": self.sb_win.value(),
            "threshold": 0
        }
        self.worker.send_config(cfg)

    def update_status(self, status):
        self.lbl_status.setText(status)
        if "Conectado" in status or "TCP" in status:
            self.lbl_status.setStyleSheet("background-color: #4CAF50; color: white; padding: 8px; border-radius: 4px; font-weight: bold;")
        else:
            self.lbl_status.setStyleSheet("background-color: #F44336; color: white; padding: 8px; border-radius: 4px; font-weight: bold;")

    def log(self, msg):
        self.txt_log.append(f"[{datetime.now().strftime('%H:%M:%S')}] {msg}")

    def closeEvent(self, event):
        self.worker.stop()
        event.accept()

if __name__ == '__main__':
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec_())