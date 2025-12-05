import serial
import time
import json
import threading
import requests
from datetime import datetime

# --- CONFIGURACIÓN ---
SERIAL_PORT = 'COM7'       
BAUD_RATE = 9600
# API_URL = "http://localhost:8080/api" 
API_URL = "http://bottle-filler.vercel.app/api" 

ser = None
running = True
ultimo_estado_json = {} 

def listar_puertos():
    """Lista todos los puertos seriales disponibles"""
    import serial.tools.list_ports
    puertos = serial.tools.list_ports.comports()
    if puertos:
        print("\n📌 Puertos COM disponibles:")
        for puerto in puertos:
            print(f"  ➤ {puerto.device} - {puerto.description}")
        return [p.device for p in puertos]
    else:
        print("❌ No se encontraron puertos COM disponibles")
        return []

def conectar_arduino():
    global ser
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
        print(f"✅ Conectado a Arduino en {SERIAL_PORT}")
        return True
    except Exception as e:
        print(f"❌ Error conectando a Arduino en {SERIAL_PORT}: {e}")
        print("\n💡 Sugerencia: Verifica el puerto COM correcto")
        listar_puertos()
        print(f"\n   Edita SERIAL_PORT en el archivo (línea 9) con el puerto correcto")
        return False

# --- VISUALIZACIÓN EN CONSOLA ---
def mostrar_dashboard(datos):
    ahora = datetime.now().strftime("%H:%M:%S")
    print("\n" + "="*60)
    print(f" 🏭 ESTADO PLANTA - {ahora}")
    print("="*60)
    
    print(f" ➤ ESTADO:    {datos.get('ESTADO', '---')}")
    print(f" ➤ PROGRESO:  {datos.get('PULSOS', 0)} / {datos.get('META', 0)} pulsos")
    print(f" ➤ TANQUE:    {datos.get('TANQUE', 0)} cm")
    
    # Visualización de booleanos (1/0)
    def on_off(val): return "🟢 ON" if int(val) == 1 else "🔴 OFF"
    
    print("-" * 30)
    print(" 🔧 ACTUADORES & SENSORES")
    print(f" [M] CINTA: {on_off(datos.get('M_CINTA', 0))}    |  [S] BOTELLA: {on_off(datos.get('S_BOTELLA', 0))}")
    print(f" [M] BOMBA: {on_off(datos.get('M_BOMBA', 0))}    |  [S] EMERG:   {'🚨' if int(datos.get('S_EMERG', 1))==0 else '✅ OK'}")
    print(f" [L] VERDE: {on_off(datos.get('L_VERDE', 0))}    |  [L] ROJO:    {on_off(datos.get('L_ROJO', 0))}")
    print("="*60)
    print(" Comandos: START, STOP, RESUME, META 50, CINTA ON, BOMBA OFF...")

# --- HILO 1: ESCUCHAR ARDUINO ---
def escuchar_arduino():
    global ultimo_estado_json
    buffer = ""
    while running:
        if ser and ser.is_open:
            try:
                if ser.in_waiting > 0:
                    char = ser.read().decode('utf-8', errors='ignore')
                    if char == '\n':
                        linea = buffer.strip()
                        if linea.startswith("{") and linea.endswith("}"):
                            try:
                                ultimo_estado_json = json.loads(linea)
                                mostrar_dashboard(ultimo_estado_json) # Actualiza consola
                            except: pass
                        buffer = ""
                    else:
                        buffer += char
            except: pass
        time.sleep(0.001)

# --- HILO 2: NUBE (Igual que antes) ---
def hilo_nube():
    while running:
        if ultimo_estado_json:
            try:
                requests.post(f"{API_URL}/update", json=ultimo_estado_json, timeout=2)
            except: pass

        try:
            resp = requests.get(f"{API_URL}/commands", timeout=2)
            if resp.status_code == 200:
                data = resp.json()
                cmd = data.get("command")
                if cmd:
                    print(f"📥 NUBE: {cmd}")
                    enviar_raw(cmd)
        except: pass
        time.sleep(2)

def enviar_raw(cmd):
    if ser and ser.is_open:
        ser.write((cmd.strip() + "\n").encode('utf-8'))

# --- MAIN ---
if __name__ == "__main__":
    if conectar_arduino():
        threading.Thread(target=escuchar_arduino, daemon=True).start()
        threading.Thread(target=hilo_nube, daemon=True).start()

        try:
            while True:
                entrada = input().strip().upper()
                msg = ""
                
                if entrada == "SALIR": break
                elif entrada == "START": msg = "CMD:START"
                elif entrada == "STOP": msg = "CMD:STOP"
                elif entrada == "RESUME": msg = "CMD:RESUME"
                
                # Comandos Manuales Locales
                elif "CINTA ON" in entrada: msg = "CMD:MANUAL_CINTA:1"
                elif "CINTA OFF" in entrada: msg = "CMD:MANUAL_CINTA:0"
                elif "BOMBA ON" in entrada: msg = "CMD:MANUAL_BOMBA:1"
                elif "BOMBA OFF" in entrada: msg = "CMD:MANUAL_BOMBA:0"
                elif "VERDE ON" in entrada: msg = "CMD:MANUAL_LED_G:1"
                elif "VERDE OFF" in entrada: msg = "CMD:MANUAL_LED_G:0"
                
                elif "META" in entrada: msg = f"CMD:SET_META:{entrada.split()[-1]}"
                
                if msg: enviar_raw(msg)
                
        except KeyboardInterrupt: pass
        finally: 
            running = False
            ser.close()