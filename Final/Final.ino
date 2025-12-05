// --- PINOUT DEL SISTEMA ---
const int PIN_SENSOR_FLUJO = 2;   
const int PIN_CINTA_IN2 = 4;
const int PIN_CINTA_IN1 = 5;
const int PIN_CINTA_ENA = 6;
const int PIN_SENSOR_BOTELLA = 7; 
const int PIN_EMERGENCIA = 8;
const int PIN_TRIG = 9;
const int PIN_ECHO = 10;
const int PIN_BOMBA_ENB = 11;
const int PIN_BOMBA_IN3 = 12;
const int PIN_BOMBA_IN4 = 13;
const int PIN_LED_VERDE = A1;
const int PIN_LED_ROJO = A2;

// --- VARIABLES ---
volatile int pulsosFlujo = 0;
volatile bool flagNuevoPulso = false;
volatile bool permitirConteo = false;
long distanciaTanque = 0;
bool sistemaActivo = true; 
String estadoActual = "INICIANDO"; 
String estadoAnterior = "";
String comandoInput = "";
int cantidadMeta = 20; 

void setup() {
  Serial.begin(9600); 
  
  pinMode(PIN_CINTA_ENA, OUTPUT); pinMode(PIN_CINTA_IN1, OUTPUT); pinMode(PIN_CINTA_IN2, OUTPUT);
  pinMode(PIN_BOMBA_ENB, OUTPUT); pinMode(PIN_BOMBA_IN3, OUTPUT); pinMode(PIN_BOMBA_IN4, OUTPUT);
  pinMode(PIN_TRIG, OUTPUT); pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_SENSOR_BOTELLA, INPUT);
  pinMode(PIN_EMERGENCIA, INPUT_PULLUP);
  pinMode(PIN_LED_VERDE, OUTPUT); pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_SENSOR_FLUJO, INPUT_PULLUP);
  
  attachInterrupt(digitalPinToInterrupt(PIN_SENSOR_FLUJO), isrContarPulso, RISING);
  
  // Arranque seguro
  if (digitalRead(PIN_SENSOR_BOTELLA) == HIGH) {
      estadoActual = "BOTELLA_DETECTADA";
      pararCinta();
      digitalWrite(PIN_LED_VERDE, LOW); digitalWrite(PIN_LED_ROJO, HIGH);
  } else {
      estadoActual = "BUSCANDO_BOTELLA";
      digitalWrite(PIN_LED_VERDE, HIGH); digitalWrite(PIN_LED_ROJO, LOW);
      moverCinta(); 
  }
  
  reportarEstado(); 
}

void loop() {
  verificarEntradas();

  if (!sistemaActivo || estadoActual == "EMERGENCIA_STOP" || estadoActual == "PAUSA_REMOTA") return;

  // --- LÓGICA AUTOMÁTICA ---
  if (estadoActual == "BUSCANDO_BOTELLA") {
      moverCinta(); 
      digitalWrite(PIN_LED_VERDE, HIGH); digitalWrite(PIN_LED_ROJO, LOW);
      
      if (digitalRead(PIN_SENSOR_BOTELLA) == HIGH) {
          pararCinta();
          digitalWrite(PIN_LED_VERDE, LOW); digitalWrite(PIN_LED_ROJO, HIGH);
          estadoActual = "BOTELLA_DETECTADA";
          reportarEstado();
          delay(1000);
          pulsosFlujo = 0; 
          estadoActual = "LLENANDO"; 
      }
  }
  else if (estadoActual == "LLENANDO") {
      llenarBotella(); 
      if (sistemaActivo) {
          estadoActual = "BOTELLA_LLENA";
          reportarEstado();
          delay(500);
          estadoActual = "TRANSPORTANDO";
      }
  }
  else if (estadoActual == "TRANSPORTANDO") {
      reportarEstado();
      digitalWrite(PIN_LED_VERDE, HIGH); digitalWrite(PIN_LED_ROJO, LOW);
      moverCinta();
      while(digitalRead(PIN_SENSOR_BOTELLA) == HIGH) {
         verificarEntradas(); 
         if(!sistemaActivo) return; 
         delay(50);
      }
      estadoActual = "BUSCANDO_BOTELLA";
      reportarEstado();
  }

  static unsigned long lastMedicion = 0;
  if (millis() - lastMedicion > 1000) {
    medirNivelTanque();
    // Reportamos siempre para mantener el dashboard actualizado
    if(estadoActual == "BUSCANDO_BOTELLA" || estadoActual == "PAUSA_REMOTA") reportarEstado(); 
    lastMedicion = millis();
  }
}

void verificarEntradas() {
    escucharSerial(); 
    if (digitalRead(PIN_EMERGENCIA) == LOW) {
        if (estadoActual != "EMERGENCIA_STOP") {
            estadoAnterior = estadoActual; 
            detenerTodo();
            estadoActual = "EMERGENCIA_STOP";
            sistemaActivo = false;
            digitalWrite(PIN_LED_ROJO, HIGH);
            reportarEstado();
            while(digitalRead(PIN_EMERGENCIA) == LOW); 
        }
    }
}

void escucharSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') { procesarComando(comandoInput); comandoInput = ""; } 
    else { comandoInput += c; }
  }
}

void procesarComando(String cmd) {
  cmd.trim(); 
  
  // --- COMANDOS DE SISTEMA ---
  if (cmd == "CMD:STOP") { 
      if (sistemaActivo) estadoAnterior = estadoActual; 
      sistemaActivo = false; detenerTodo(); permitirConteo = false;
      estadoActual = "PAUSA_REMOTA"; reportarEstado();
  }
  else if (cmd == "CMD:START") { 
      sistemaActivo = true; pulsosFlujo = 0; 
      if (digitalRead(PIN_SENSOR_BOTELLA) == HIGH) estadoActual = "BOTELLA_DETECTADA"; 
      else { estadoActual = "BUSCANDO_BOTELLA"; moverCinta(); }
      reportarEstado();
  }
  else if (cmd == "CMD:RESUME") {
      if (!sistemaActivo) { 
          sistemaActivo = true; estadoActual = estadoAnterior; reportarEstado();
          if (estadoActual == "LLENANDO") permitirConteo = true; 
          else if (estadoActual == "BUSCANDO_BOTELLA" || estadoActual == "TRANSPORTANDO") moverCinta();
      }
  }
  
  // --- CONTROL MANUAL DE ACTUADORES (NUEVO) ---
  // Formato: CMD:MANUAL_CINTA:1 (ON) o CMD:MANUAL_CINTA:0 (OFF)
  else if (cmd.startsWith("CMD:MANUAL_")) {
      // Solo permitimos manual si NO está en un proceso crítico automático
      bool seguroParaManual = (estadoActual == "PAUSA_REMOTA" || estadoActual == "BUSCANDO_BOTELLA" || estadoActual == "INICIANDO");
      
      if (seguroParaManual) {
          int val = cmd.substring(cmd.lastIndexOf(':') + 1).toInt();
          
          if (cmd.startsWith("CMD:MANUAL_CINTA")) {
              if (val == 1) moverCinta(); else pararCinta();
          }
          else if (cmd.startsWith("CMD:MANUAL_BOMBA")) {
               if (val == 1) { 
                   digitalWrite(PIN_BOMBA_IN3, HIGH); digitalWrite(PIN_BOMBA_IN4, LOW); analogWrite(PIN_BOMBA_ENB, 255); 
               } else { 
                   digitalWrite(PIN_BOMBA_IN3, LOW); digitalWrite(PIN_BOMBA_IN4, LOW); analogWrite(PIN_BOMBA_ENB, 0); 
               }
          }
          else if (cmd.startsWith("CMD:MANUAL_LED_G")) {
               digitalWrite(PIN_LED_VERDE, val);
          }
          else if (cmd.startsWith("CMD:MANUAL_LED_R")) {
               digitalWrite(PIN_LED_ROJO, val);
          }
          reportarEstado();
      }
  }

  // --- CONFIGURACIÓN ---
  else if (cmd.startsWith("CMD:SET_META:")) {
      cantidadMeta = cmd.substring(13).toInt();
  }
}

void reportarEstado() {
  Serial.print("{");
  Serial.print("\"ESTADO\":\"" + estadoActual + "\",");
  Serial.print("\"PULSOS\":" + String(pulsosFlujo) + ",");
  Serial.print("\"META\":" + String(cantidadMeta) + ",");
  Serial.print("\"TANQUE\":" + String(distanciaTanque) + ",");
  
  // Sensores
  Serial.print("\"S_BOTELLA\":" + String(digitalRead(PIN_SENSOR_BOTELLA)) + ",");
  Serial.print("\"S_EMERG\":" + String(digitalRead(PIN_EMERGENCIA)) + ",");
  
  // [NUEVO] Estado real de Actuadores
  // Para la cinta/bomba leemos el pin de habilitación o dirección
  Serial.print("\"M_CINTA\":" + String(digitalRead(PIN_CINTA_IN1)) + ","); 
  Serial.print("\"M_BOMBA\":" + String(digitalRead(PIN_BOMBA_IN3)) + ",");
  Serial.print("\"L_VERDE\":" + String(digitalRead(PIN_LED_VERDE)) + ",");
  Serial.print("\"L_ROJO\":" + String(digitalRead(PIN_LED_ROJO)));
  
  Serial.println("}");
}

void llenarBotella() {
  permitirConteo = true; reportarEstado();
  digitalWrite(PIN_BOMBA_IN3, HIGH); digitalWrite(PIN_BOMBA_IN4, LOW); analogWrite(PIN_BOMBA_ENB, 255);
  while (pulsosFlujo < cantidadMeta) { 
    verificarEntradas(); 
    if (!sistemaActivo) { detenerTodo(); permitirConteo = false; return; }
    if (flagNuevoPulso) { reportarEstado(); flagNuevoPulso = false; }
  }
  detenerTodo(); permitirConteo = false; 
}

void isrContarPulso() { if (permitirConteo) { pulsosFlujo++; flagNuevoPulso = true; } }
void medirNivelTanque() { digitalWrite(PIN_TRIG, LOW); delayMicroseconds(2); digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10); digitalWrite(PIN_TRIG, LOW); long dur = pulseIn(PIN_ECHO, HIGH); distanciaTanque = dur * 0.034 / 2; }
void moverCinta() { digitalWrite(PIN_CINTA_IN1, HIGH); digitalWrite(PIN_CINTA_IN2, LOW); analogWrite(PIN_CINTA_ENA, 255); }
void pararCinta() { digitalWrite(PIN_CINTA_IN1, LOW); digitalWrite(PIN_CINTA_IN2, LOW); analogWrite(PIN_CINTA_ENA, 0); }
void detenerTodo() { pararCinta(); digitalWrite(PIN_BOMBA_IN3, LOW); digitalWrite(PIN_BOMBA_IN4, LOW); analogWrite(PIN_BOMBA_ENB, 0); }